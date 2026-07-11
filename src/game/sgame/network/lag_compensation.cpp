// Copyright (c) 2026 The WORR Project
// Licensed under the GNU General Public License 2.0.

#include "lag_compensation.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

// The history is static and fixed-capacity so firing and end-frame capture do
// not allocate.  512 samples cover the policy maximum of 500 ms even at a
// 1 ms simulation tick, while ordinary servers use only the active prefix.
constexpr std::size_t kHistoryCapacity = 512;
constexpr int kDefaultMaxRewindMs = 200;
constexpr int kPolicyMaxRewindMs = 500;
constexpr float kTeleportDistance = 256.0f;
constexpr float kTeleportDistanceSquared =
    kTeleportDistance * kTeleportDistance;

struct HistoricalPose {
  GameTime time{};
  uint32_t serverFrame = 0;
  int32_t spawnCount = 0;
  Vector3 origin{};
  Vector3 angles{};
  Vector3 mins{};
  Vector3 maxs{};
  Vector3 velocity{};
  solid_t solid = SOLID_NOT;
  contents_t clipMask = CONTENTS_NONE;
  svflags_t svFlags{};
  bool linked = false;
  bool damageable = false;
  bool dead = false;
  bool discontinuity = false;
};

struct PoseTrack {
  std::array<HistoricalPose, kHistoryCapacity> samples{};
  std::size_t next = 0;
  std::size_t count = 0;
  uint32_t lastServerFrame = 0;
  int32_t spawnCount = 0;
  bool lastDamageable = false;
  bool lastDead = false;
};

struct Diagnostics {
  uint64_t requests = 0;
  uint64_t sessions = 0;
  uint64_t targets = 0;
  uint64_t interpolated = 0;
  uint64_t discontinuities = 0;
  uint64_t capped = 0;
  uint64_t rejectedClock = 0;
  uint64_t missingHistory = 0;
  GameTime nextReport{};
};

std::array<PoseTrack, MAX_CLIENTS_KEX> poseTracks{};
Diagnostics diagnostics{};
// Sgame collision queries are single-threaded.  Reuse fixed scratch storage to
// keep a compensated pellet from consuming a 64 KiB stack frame or repeatedly
// value-initializing the full private gentity_t payload.
std::array<gentity_t *, MAX_ENTITIES> traceCandidates{};
gentity_t historicalProxy{};

cvar_t *sg_lag_compensation_max_ms = nullptr;
cvar_t *sg_lag_compensation_interp_ms = nullptr;
cvar_t *sg_lag_compensation_debug = nullptr;

[[nodiscard]] std::size_t ClientIndex(const gentity_t *entity) {
  if (!entity || entity->s.number <= 0)
    return MAX_CLIENTS_KEX;

  const auto index = static_cast<std::size_t>(entity->s.number - 1);
  if (index >= static_cast<std::size_t>(game.maxClients) ||
      index >= static_cast<std::size_t>(MAX_CLIENTS_KEX)) {
    return MAX_CLIENTS_KEX;
  }

  return index;
}

void ClearTrack(PoseTrack &track) {
  track.next = 0;
  track.count = 0;
  track.lastServerFrame = 0;
  track.spawnCount = 0;
  track.lastDamageable = false;
  track.lastDead = false;
}

[[nodiscard]] const HistoricalPose &Newest(const PoseTrack &track,
                                           std::size_t age) {
  const std::size_t index =
      (track.next + kHistoryCapacity - 1 - age) % kHistoryCapacity;
  return track.samples[index];
}

[[nodiscard]] Vector3 InterpolateVector(const Vector3 &older,
                                        const Vector3 &newer, float fraction) {
  return older + (newer - older) * fraction;
}

[[nodiscard]] Vector3 InterpolateAngles(const Vector3 &older,
                                        const Vector3 &newer, float fraction) {
  return {LerpAngle(older.x, newer.x, fraction),
          LerpAngle(older.y, newer.y, fraction),
          LerpAngle(older.z, newer.z, fraction)};
}

[[nodiscard]] bool FindHistoricalPose(const PoseTrack &track,
                                      GameTime targetTime,
                                      HistoricalPose &result,
                                      bool &interpolated,
                                      bool &hitDiscontinuity) {
  interpolated = false;
  hitDiscontinuity = false;

  if (!track.count)
    return false;

  const HistoricalPose *newer = &Newest(track, 0);
  if (targetTime >= newer->time) {
    result = *newer;
    return true;
  }

  for (std::size_t age = 1; age < track.count; ++age) {
    const HistoricalPose *older = &Newest(track, age);
    if (targetTime < older->time) {
      newer = older;
      continue;
    }

    if (older->spawnCount != newer->spawnCount)
      return false;

    const int64_t spanMs =
        (newer->time - older->time).milliseconds();
    if (spanMs <= 0) {
      result = *older;
      return true;
    }

    if (newer->discontinuity) {
      // Never synthesize a collision pose through a teleport/respawn/large
      // correction.  Select the side of the discontinuity containing the
      // requested time instead.
      hitDiscontinuity = true;
      result = targetTime < newer->time ? *older : *newer;
      return true;
    }

    const float fraction = std::clamp(
        static_cast<float>((targetTime - older->time).milliseconds()) /
            static_cast<float>(spanMs),
        0.0f, 1.0f);
    const HistoricalPose &nearest = fraction < 0.5f ? *older : *newer;

    result = nearest;
    result.time = targetTime;
    result.origin = InterpolateVector(older->origin, newer->origin, fraction);
    result.angles = InterpolateAngles(older->angles, newer->angles, fraction);
    result.velocity =
        InterpolateVector(older->velocity, newer->velocity, fraction);
    // Stance/bounds are discrete simulation state.  Selecting the nearest
    // sample avoids inventing a half-crouched hitbox.
    result.mins = nearest.mins;
    result.maxs = nearest.maxs;
    interpolated = fraction > 0.0f && fraction < 1.0f;
    return true;
  }

  // The requested time predates the retained history.  Do not silently clamp
  // a newly spawned target to an unrelated oldest pose.
  return false;
}

[[nodiscard]] int MaxRewindMs() {
  const int configured = sg_lag_compensation_max_ms
                             ? sg_lag_compensation_max_ms->integer
                             : kDefaultMaxRewindMs;
  return std::clamp(configured, 0, kPolicyMaxRewindMs);
}

[[nodiscard]] int InterpolationBiasMs(int estimatedSnapshotIntervalMs) {
  const int configured = sg_lag_compensation_interp_ms
                             ? sg_lag_compensation_interp_ms->integer
                             : -1;
  if (configured >= 0)
    return std::clamp(configured, 0, MaxRewindMs());

  // Legacy commands only carry an acknowledged snapshot watermark, not the
  // precise client render time.  Use half the validated interval between
  // contiguous snapshots until FR-10-T09 adds an explicit per-command render
  // timestamp and consumed-input watermark.  Zero is a deliberate
  // no-interpolation sentinel for first snapshots and suppressed wire gaps.
  if (estimatedSnapshotIntervalMs <= 0)
    return 0;

  return std::clamp(estimatedSnapshotIntervalMs / 2, 0, MaxRewindMs());
}

void MaybeReportDiagnostics() {
  if (!sg_lag_compensation_debug || !sg_lag_compensation_debug->integer)
    return;

  if (diagnostics.nextReport && level.time < diagnostics.nextReport)
    return;

  diagnostics.nextReport = level.time + 1_sec;
  gi.Com_PrintFmt(
      "lagcomp: requests={} sessions={} targets={} interpolated={} "
      "discontinuities={} capped={} rejected_clock={} missing_history={}\n",
      diagnostics.requests, diagnostics.sessions, diagnostics.targets,
      diagnostics.interpolated, diagnostics.discontinuities,
      diagnostics.capped, diagnostics.rejectedClock,
      diagnostics.missingHistory);
}

[[nodiscard]] bool FindSnapshotTime(const PoseTrack &track,
                                    uint32_t serverFrame,
                                    uint32_t serverFrameDelta,
                                    GameTime &snapshotTime,
                                    int &estimatedIntervalMs) {
  const HistoricalPose *matched = nullptr;
  std::size_t matchedAge = 0;

  for (std::size_t age = 0; age < track.count; ++age) {
    const HistoricalPose &sample = Newest(track, age);
    if (sample.serverFrame == serverFrame) {
      // Samples are searched newest-first.  When g_frames_per_frame produces
      // multiple simulation ticks under one engine frame, the newest sample
      // is the state that the outgoing snapshot observed.
      matched = &sample;
      matchedAge = age;
      break;
    }
    if (sample.serverFrame < serverFrame)
      break;
  }

  if (!matched)
    return false;

  snapshotTime = matched->time;
  estimatedIntervalMs = 0;

  // First snapshots and discontinuous per-client wire sequences do not have
  // an interpolation predecessor.  Do not manufacture bias from the nearest
  // simulation sample: that would over-rewind across rate drops/fragment
  // stalls and would bias a client's first command.
  if (!serverFrameDelta || serverFrameDelta >= serverFrame)
    return true;

  // Prefer the interval between snapshots actually built for this client.
  // Reduced client rates may span multiple engine frames even though sgame
  // retained samples for each simulation step.
  const uint32_t previousSentFrame = serverFrame - serverFrameDelta;
  for (std::size_t age = matchedAge + 1; age < track.count; ++age) {
    const HistoricalPose &older = Newest(track, age);
    if (older.serverFrame > previousSentFrame)
      continue;
    if (older.serverFrame < previousSentFrame)
      break;

    const int64_t interval = (matched->time - older.time).milliseconds();
    if (interval > 0) {
      estimatedIntervalMs = static_cast<int>(std::min<int64_t>(
          interval, static_cast<int64_t>(kPolicyMaxRewindMs)));
    }
    break;
  }

  // First-snapshot/history-warmup fallback: use the nearest preceding engine
  // frame until an exact prior sent snapshot is retained.
  if (estimatedIntervalMs > 0)
    return true;

  for (std::size_t age = matchedAge + 1; age < track.count; ++age) {
    const HistoricalPose &older = Newest(track, age);
    if (older.serverFrame >= serverFrame)
      continue;

    const int64_t interval = (matched->time - older.time).milliseconds();
    if (interval > 0)
      estimatedIntervalMs = static_cast<int>(std::min<int64_t>(
          interval, static_cast<int64_t>(kPolicyMaxRewindMs)));
    break;
  }

  return true;
}

[[nodiscard]] bool ResolveTargetTime(gentity_t *fromPlayer,
                                     GameTime &targetTime) {
  ++diagnostics.requests;

  if (!fromPlayer || !fromPlayer->client || !deathmatch ||
      !deathmatch->integer || !g_lagCompensation ||
      !g_lagCompensation->integer || (fromPlayer->svFlags & SVF_BOT)) {
    return false;
  }

  const uint32_t currentFrame = gi.ServerFrame();
  const uint32_t acknowledgedFrame = fromPlayer->client->cmd.serverFrame;
  if (!acknowledgedFrame || acknowledgedFrame >= currentFrame) {
    ++diagnostics.rejectedClock;
    MaybeReportDiagnostics();
    return false;
  }

  const std::size_t shooterIndex = ClientIndex(fromPlayer);
  if (shooterIndex >= poseTracks.size()) {
    ++diagnostics.rejectedClock;
    return false;
  }

  GameTime snapshotTime{};
  int estimatedIntervalMs = 0;
  if (!FindSnapshotTime(poseTracks[shooterIndex], acknowledgedFrame,
                        fromPlayer->client->cmd.serverFrameDelta,
                        snapshotTime, estimatedIntervalMs)) {
    ++diagnostics.missingHistory;
    MaybeReportDiagnostics();
    return false;
  }

  const int64_t ageMs = (level.time - snapshotTime).milliseconds();
  if (ageMs < 0) {
    ++diagnostics.rejectedClock;
    MaybeReportDiagnostics();
    return false;
  }

  const uint64_t rawAgeMs =
      static_cast<uint64_t>(ageMs) +
      static_cast<uint64_t>(InterpolationBiasMs(estimatedIntervalMs));
  const uint64_t maxRewindMs = static_cast<uint64_t>(MaxRewindMs());
  if (!maxRewindMs)
    return false;

  const uint64_t rewindMs = std::min(rawAgeMs, maxRewindMs);
  if (rewindMs != rawAgeMs)
    ++diagnostics.capped;

  targetTime =
      level.time - GameTime::from_ms(static_cast<int64_t>(rewindMs));
  ++diagnostics.sessions;
  return true;
}

void MergeTrace(trace_t &destination, const trace_t &candidate,
                gentity_t *entity) {
  destination.allSolid |= candidate.allSolid;
  destination.startSolid |= candidate.startSolid;

  if (candidate.fraction < destination.fraction) {
    destination.fraction = candidate.fraction;
    destination.endPos = candidate.endPos;
    destination.plane = candidate.plane;
    destination.surface = candidate.surface;
    destination.contents = candidate.contents;
    destination.ent = entity;
  }

  if (candidate.allSolid || candidate.startSolid)
    destination.ent = entity;
}

[[nodiscard]] bool ShouldSkipCurrentEntity(const gentity_t *entity,
                                           const gentity_t *passEntity,
                                           contents_t contentMask) {
  if (!entity || !entity->inUse || !entity->linked ||
      entity->solid == SOLID_NOT || entity == passEntity) {
    return true;
  }

  if (passEntity &&
      (entity->owner == passEntity || passEntity->owner == entity)) {
    return true;
  }

  if (!(contentMask & CONTENTS_DEADMONSTER) &&
      (entity->svFlags & SVF_DEADMONSTER)) {
    return true;
  }
  if (!(contentMask & CONTENTS_PROJECTILE) &&
      (entity->svFlags & SVF_PROJECTILE)) {
    return true;
  }

  return false;
}

trace_t ClipEntity(gentity_t *clipEntity, const Vector3 &start,
                   const Vector3 *mins, const Vector3 *maxs,
                   const Vector3 &end, contents_t contentMask) {
  if (mins && maxs)
    return gi.clip(clipEntity, start, *mins, *maxs, end, contentMask);
  return gi.clip(clipEntity, start, end, contentMask);
}

trace_t TraceHistoricalScene(gentity_t *fromPlayer, const Vector3 &start,
                             const Vector3 *mins, const Vector3 *maxs,
                             const Vector3 &end, gentity_t *passEntity,
                             contents_t contentMask) {
  GameTime targetTime{};
  if (!(contentMask & CONTENTS_PLAYER) ||
      !ResolveTargetTime(fromPlayer, targetTime)) {
    if (mins && maxs)
      return gi.trace(start, *mins, *maxs, end, passEntity, contentMask);
    return gi.traceLine(start, end, passEntity, contentMask);
  }

  // Build a non-mutating collision scene: static world and current non-player
  // entities are clipped directly, while current player links are replaced by
  // unlinked full-pose historical proxies.  No live entity is moved/relinked.
  trace_t result = ClipEntity(world, start, mins, maxs, end, contentMask);
  if (result.fraction == 0.0f) {
    MaybeReportDiagnostics();
    return result;
  }

  const Vector3 traceMins = mins ? *mins : vec3_origin;
  const Vector3 traceMaxs = maxs ? *maxs : vec3_origin;
  Vector3 broadphaseMins{};
  Vector3 broadphaseMaxs{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (end[axis] > start[axis]) {
      broadphaseMins[axis] = start[axis] + traceMins[axis] - 1.0f;
      broadphaseMaxs[axis] = end[axis] + traceMaxs[axis] + 1.0f;
    } else {
      broadphaseMins[axis] = end[axis] + traceMins[axis] - 1.0f;
      broadphaseMaxs[axis] = start[axis] + traceMaxs[axis] + 1.0f;
    }
  }

  const std::size_t candidateCount =
      gi.BoxEntities(broadphaseMins, broadphaseMaxs, traceCandidates.data(),
                     traceCandidates.size(), AREA_SOLID, nullptr, nullptr);

  for (std::size_t i = 0; i < candidateCount && !result.allSolid; ++i) {
    gentity_t *entity = traceCandidates[i];
    if (ShouldSkipCurrentEntity(entity, passEntity, contentMask))
      continue;

    // Eligible living players are replaced below, even when the engine is
    // serving a legacy protocol whose trace filtering predates
    // CONTENTS_PLAYER. Dead/non-damageable client bodies remain in the current
    // scene so corpse occlusion matches native SV_Trace without resurrecting an
    // older life for damage.
    if ((entity->client || (entity->svFlags & SVF_PLAYER)) &&
        entity->takeDamage && !entity->deadFlag) {
      continue;
    }

    const trace_t clipped =
        ClipEntity(entity, start, mins, maxs, end, contentMask);
    MergeTrace(result, clipped, entity);
  }

  for (gentity_t *player : active_clients()) {
    if (result.allSolid)
      break;

    if (ShouldSkipCurrentEntity(player, passEntity, contentMask) ||
        !player->client || !player->takeDamage || player->deadFlag) {
      continue;
    }

    const std::size_t playerIndex = ClientIndex(player);
    if (playerIndex >= poseTracks.size())
      continue;

    HistoricalPose historical{};
    bool interpolated = false;
    bool discontinuity = false;
    const bool found = FindHistoricalPose(
        poseTracks[playerIndex], targetTime, historical, interpolated,
        discontinuity);

    if (!found) {
      // The target did not have a compatible life/pose at the requested time.
      // Never substitute a newly spawned current life for an older command.
      ++diagnostics.missingHistory;
      continue;
    }

    if (!historical.linked || !historical.damageable || historical.dead ||
        historical.solid == SOLID_NOT ||
        historical.spawnCount != player->spawn_count) {
      continue;
    }

    const Vector3 historicalAbsMin = historical.origin + historical.mins;
    const Vector3 historicalAbsMax = historical.origin + historical.maxs;
    if (historicalAbsMin.x > broadphaseMaxs.x ||
        historicalAbsMin.y > broadphaseMaxs.y ||
        historicalAbsMin.z > broadphaseMaxs.z ||
        historicalAbsMax.x < broadphaseMins.x ||
        historicalAbsMax.y < broadphaseMins.y ||
        historicalAbsMax.z < broadphaseMins.z) {
      continue;
    }

    gentity_t &proxy = historicalProxy;
    proxy.inUse = true;
    proxy.client = nullptr;
    proxy.owner = nullptr;
    proxy.linked = false;
    proxy.s.modelIndex = 0;
    proxy.s.origin = historical.origin;
    proxy.s.angles = historical.angles;
    proxy.mins = historical.mins;
    proxy.maxs = historical.maxs;
    proxy.solid = historical.solid;
    proxy.clipMask = historical.clipMask;
    proxy.svFlags = historical.svFlags;

    const trace_t clipped =
        ClipEntity(&proxy, start, mins, maxs, end, contentMask);
    MergeTrace(result, clipped, player);

    ++diagnostics.targets;
    if (interpolated)
      ++diagnostics.interpolated;
    if (discontinuity)
      ++diagnostics.discontinuities;
  }

  MaybeReportDiagnostics();
  return result;
}

} // namespace

void LagCompensation_Init() {
  sg_lag_compensation_max_ms = gi.cvar(
      "sg_lag_compensation_max_ms", "200", CVAR_NOFLAGS);
  sg_lag_compensation_interp_ms = gi.cvar(
      "sg_lag_compensation_interp_ms", "-1", CVAR_NOFLAGS);
  sg_lag_compensation_debug =
      gi.cvar("sg_lag_compensation_debug", "0", CVAR_NOFLAGS);

  for (PoseTrack &track : poseTracks)
    ClearTrack(track);
  diagnostics = {};
}

void LagCompensation_Shutdown() {
  for (PoseTrack &track : poseTracks)
    ClearTrack(track);
}

void LagCompensation_RecordFrame(gentity_t *entity) {
  const std::size_t index = ClientIndex(entity);
  if (index >= poseTracks.size() || !entity || !entity->client)
    return;

  PoseTrack &track = poseTracks[index];
  const uint32_t serverFrame = gi.ServerFrame();
  const bool damageable = entity->takeDamage;
  const bool dead = entity->deadFlag;

  bool discontinuity = false;
  if (track.count) {
    const HistoricalPose &previous = Newest(track, 0);
    // g_frames_per_frame may emit multiple monotonically timed simulation
    // samples under one engine frame.  Only a backwards engine frame or a
    // non-advancing sgame clock denotes a map/restart discontinuity.
    const bool clockReset = serverFrame < track.lastServerFrame ||
                            level.time <= previous.time;
    const bool lifecycleChanged =
        track.spawnCount != entity->spawn_count ||
        track.lastDamageable != damageable || track.lastDead != dead;
    const bool teleported = entity->s.event == EV_PLAYER_TELEPORT ||
                            entity->s.event == EV_OTHER_TELEPORT;
    const bool largeCorrection =
        (entity->s.origin - previous.origin).lengthSquared() >
        kTeleportDistanceSquared;

    if (clockReset || lifecycleChanged) {
      ClearTrack(track);
    } else {
      discontinuity = teleported || largeCorrection;
    }
  }

  HistoricalPose &sample = track.samples[track.next];
  sample.time = level.time;
  sample.serverFrame = serverFrame;
  sample.spawnCount = entity->spawn_count;
  sample.origin = entity->s.origin;
  sample.angles = entity->s.angles;
  sample.mins = entity->mins;
  sample.maxs = entity->maxs;
  sample.velocity = entity->velocity;
  sample.solid = entity->solid;
  sample.clipMask = entity->clipMask;
  sample.svFlags = entity->svFlags;
  sample.linked = entity->linked;
  sample.damageable = damageable;
  sample.dead = dead;
  sample.discontinuity = discontinuity;

  track.next = (track.next + 1) % kHistoryCapacity;
  track.count = std::min(track.count + 1, kHistoryCapacity);
  track.lastServerFrame = serverFrame;
  track.spawnCount = entity->spawn_count;
  track.lastDamageable = damageable;
  track.lastDead = dead;
}

trace_t LagCompensation_TraceLine(gentity_t *fromPlayer,
                                  const Vector3 &start,
                                  const Vector3 &end,
                                  gentity_t *passEntity,
                                  contents_t contentMask) {
  return TraceHistoricalScene(fromPlayer, start, nullptr, nullptr, end,
                              passEntity, contentMask);
}

trace_t LagCompensation_Trace(gentity_t *fromPlayer,
                              const Vector3 &start,
                              const Vector3 &mins,
                              const Vector3 &maxs,
                              const Vector3 &end,
                              gentity_t *passEntity,
                              contents_t contentMask) {
  return TraceHistoricalScene(fromPlayer, start, &mins, &maxs, end,
                              passEntity, contentMask);
}
