// Copyright (c) 2026 The WORR Project
// Licensed under the GNU General Public License 2.0.

#include "lag_compensation.hpp"
#include "shared/command_context.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

// Fixed storage keeps authoritative capture and weapon traces allocation-free.
// At the one-millisecond extreme this still covers twice the 250 ms hard cap.
constexpr uint32_t kHistoryCapacity = 512;
constexpr uint32_t kIgnoreCapacity = static_cast<uint32_t>(MAX_PIERCE + 2);
constexpr uint32_t kObservationCapacity = 256;
constexpr int kDefaultMaxRewindMs = 200;
constexpr int kPolicyMaxRewindMs = 250;

struct LegacyFrameSample {
  uint64_t timeUs = 0;
  uint32_t serverFrame = 0;
};

struct LegacyFrameTrack {
  std::array<LegacyFrameSample, kHistoryCapacity> samples{};
  uint32_t next = 0;
  uint32_t count = 0;
};

struct PoseTrack {
  std::array<worr_rewind_pose_v1, kHistoryCapacity> storage{};
  worr_rewind_history_v1 history{};
  LegacyFrameTrack legacyFrames{};
  bool initialized = false;
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
  uint64_t appendRejected = 0;
  uint64_t canonicalAccepted = 0;
  uint64_t canonicalRejected = 0;
  uint64_t canonicalReused = 0;
  uint64_t scenesBuilt = 0;
  uint64_t scenesReused = 0;
  uint64_t scenesRejected = 0;
  uint64_t ignoredIdentities = 0;
  GameTime nextReport{};
};

struct CommandDecisionCache {
  worr_command_id_v1 commandId{};
  worr_authoritative_command_context_v1 authority{};
  worr_rewind_policy_decision_v1 decision{};
  bool valid = false;
  bool accepted = false;
};

struct FrozenSceneCache {
  worr_command_id_v1 commandId{};
  worr_rewind_policy_decision_v1 decision{};
  std::array<worr_event_entity_ref_v1, MAX_CLIENTS_KEX> eligibleRoster{};
  std::array<worr_rewind_scene_candidate_v1, MAX_CLIENTS_KEX> storage{};
  worr_rewind_scene_v1 scene{};
  uint32_t eligibleRosterCount = 0;
  bool valid = false;
};

struct RewindTarget {
  uint64_t timeUs = 0;
  uint32_t mapEpoch = 0;
  bool canonical = false;
  worr_rewind_policy_decision_v1 decision{};
};

struct TraceBounds {
  Vector3 mins{};
  Vector3 maxs{};
};

std::array<PoseTrack, MAX_CLIENTS_KEX> poseTracks{};
std::array<uint32_t, MAX_CLIENTS_KEX> clientLifeGenerations{};
std::array<bool, MAX_CLIENTS_KEX> clientLifeGenerationExhausted{};
std::array<worr_rewind_policy_state_v1, MAX_CLIENTS_KEX> policyStates{};
std::array<CommandDecisionCache, MAX_CLIENTS_KEX> decisionCaches{};
std::array<FrozenSceneCache, MAX_CLIENTS_KEX> sceneCaches{};
std::array<worr_rewind_pose_v1, kHistoryCapacity> relabelScratch{};
std::array<gentity_t *, MAX_ENTITIES> traceCandidates{};
gentity_t historicalProxy{};
Diagnostics diagnostics{};
std::array<worr_rewind_observation_v1, kObservationCapacity>
    observationStorage{};
worr_rewind_observation_journal_v1 observationJournal{};

const worr_command_context_import_v1 *commandContextImport = nullptr;
cvar_t *sg_lag_compensation_max_ms = nullptr;
cvar_t *sg_lag_compensation_interp_ms = nullptr;
cvar_t *sg_lag_compensation_debug = nullptr;
cvar_t *sg_lag_compensation_legacy_error_ms = nullptr;

uint32_t provisionalMapEpoch = 0;
uint32_t historyMapEpoch = 1;
uint32_t authoritativeMapEpoch = 0;
uint32_t captureTick = 0;
uint64_t lastCaptureTimeUs = std::numeric_limits<uint64_t>::max();
bool awaitingAuthoritativeMapEpoch = true;
bool initialized = false;

[[nodiscard]] bool SameCommand(worr_command_id_v1 lhs,
                               worr_command_id_v1 rhs) {
  return lhs.epoch == rhs.epoch && lhs.sequence == rhs.sequence;
}

template <typename T>
[[nodiscard]] bool SameCanonicalBytes(const T &lhs, const T &rhs) {
  return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

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

[[nodiscard]] bool TimeToMicroseconds(GameTime time, uint64_t &timeUs) {
  const int64_t milliseconds = time.milliseconds();
  if (milliseconds < 0)
    return false;
  const uint64_t value = static_cast<uint64_t>(milliseconds);
  if (value > std::numeric_limits<uint64_t>::max() / UINT64_C(1000))
    return false;
  timeUs = value * UINT64_C(1000);
  return true;
}

[[nodiscard]] worr_event_entity_ref_v1 AbsentEntityRef() {
  return {WORR_EVENT_NO_ENTITY, 0};
}

[[nodiscard]] bool EntityRef(const gentity_t *entity,
                             worr_event_entity_ref_v1 &ref) {
  if (!entity || entity->s.number <= 0 || entity->s.number >= MAX_ENTITIES)
    return false;

  uint32_t generation = 0;
  if (entity->client) {
    const std::size_t clientIndex = ClientIndex(entity);
    if (clientIndex >= clientLifeGenerations.size())
      return false;
    generation = clientLifeGenerations[clientIndex];
  } else {
    generation = static_cast<uint32_t>(entity->spawn_count) + 1u;
  }
  if (generation == 0)
    return false;
  ref.index = static_cast<uint32_t>(entity->s.number);
  ref.generation = generation;
  return true;
}

[[nodiscard]] gentity_t *EntityFromRef(worr_event_entity_ref_v1 ref) {
  if (ref.index == WORR_EVENT_NO_ENTITY || ref.index >= MAX_ENTITIES ||
      ref.index >= static_cast<uint32_t>(globals.numEntities) ||
      ref.generation == 0) {
    return nullptr;
  }
  gentity_t *entity = &g_entities[ref.index];
  worr_event_entity_ref_v1 currentRef{};
  if (!entity->inUse || !EntityRef(entity, currentRef) ||
      currentRef.index != ref.index ||
      currentRef.generation != ref.generation) {
    return nullptr;
  }
  return entity;
}

void CopyVector(float destination[3], const Vector3 &source) {
  destination[0] = source.x;
  destination[1] = source.y;
  destination[2] = source.z;
}

[[nodiscard]] Vector3 ToVector(const float source[3]) {
  return {source[0], source[1], source[2]};
}

[[nodiscard]] bool InitTrack(PoseTrack &track, uint32_t entityIndex) {
  worr_rewind_history_config_v1 config{};
  Worr_RewindHistoryConfigDefaultsV1(&config);
  track.legacyFrames = {};
  track.initialized = Worr_RewindHistoryInitV1(
      &track.history, track.storage.data(), kHistoryCapacity, entityIndex,
      &config);
  return track.initialized;
}

void ResetHistories(uint32_t mapEpoch) {
  historyMapEpoch = mapEpoch ? mapEpoch : 1u;
  captureTick = 0;
  lastCaptureTimeUs = std::numeric_limits<uint64_t>::max();
  for (std::size_t i = 0; i < poseTracks.size(); ++i)
    (void)InitTrack(poseTracks[i], static_cast<uint32_t>(i + 1u));
  decisionCaches = {};
  sceneCaches = {};
}

[[nodiscard]] bool RelabelTrack(PoseTrack &track, uint32_t mapEpoch) {
  if (!track.initialized ||
      !Worr_RewindHistoryValidateV1(&track.history)) {
    return false;
  }

  const uint32_t count = track.history.count;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t index = static_cast<uint32_t>(
        (static_cast<uint64_t>(track.history.head) + i) %
        track.history.capacity);
    relabelScratch[i] = track.history.slots[index];
  }

  const uint32_t entityIndex = track.history.entity_index;
  const LegacyFrameTrack legacyFrames = track.legacyFrames;
  if (!InitTrack(track, entityIndex))
    return false;
  // Epoch adoption relabels canonical pose provenance only.  Preserve the
  // independently validated ack-to-time ring used by mixed legacy clients;
  // map/client resets still clear it through InitTrack.
  track.legacyFrames = legacyFrames;
  for (uint32_t i = 0; i < count; ++i) {
    worr_rewind_pose_v1 pose = relabelScratch[i];
    pose.map_epoch = mapEpoch;
    pose.flags &= ~static_cast<uint32_t>(WORR_REWIND_POSE_DISCONTINUITY_MAP);
    uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
    if (!Worr_RewindHistoryAppendV1(&track.history, &pose, &reason) ||
        reason != WORR_REWIND_APPEND_ACCEPTED) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool AdoptAuthoritativeMapEpoch(uint32_t mapEpoch) {
  if (mapEpoch == 0)
    return false;
  if (authoritativeMapEpoch == mapEpoch && historyMapEpoch == mapEpoch)
    return true;

  if (awaitingAuthoritativeMapEpoch) {
    for (PoseTrack &track : poseTracks) {
      if (!RelabelTrack(track, mapEpoch)) {
        ResetHistories(mapEpoch);
        break;
      }
    }
  } else {
    // An epoch transition without the map reset hook is not safe to relabel:
    // discard history rather than alias poses from two maps.
    ResetHistories(mapEpoch);
  }

  historyMapEpoch = mapEpoch;
  authoritativeMapEpoch = mapEpoch;
  awaitingAuthoritativeMapEpoch = false;
  sceneCaches = {};
  return true;
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
      "discontinuities={} capped={} rejected_clock={} missing_history={} "
      "append_rejected={} canonical_accepted={} canonical_rejected={} "
      "canonical_reused={} scenes_built={} scenes_reused={} "
      "scenes_rejected={} ignored_identities={}\n",
      diagnostics.requests, diagnostics.sessions, diagnostics.targets,
      diagnostics.interpolated, diagnostics.discontinuities,
      diagnostics.capped, diagnostics.rejectedClock,
      diagnostics.missingHistory, diagnostics.appendRejected,
      diagnostics.canonicalAccepted, diagnostics.canonicalRejected,
      diagnostics.canonicalReused, diagnostics.scenesBuilt,
      diagnostics.scenesReused, diagnostics.scenesRejected,
      diagnostics.ignoredIdentities);
}

[[nodiscard]] const LegacyFrameSample &NewestLegacyFrame(
    const LegacyFrameTrack &track, uint32_t age) {
  const uint32_t index =
      (track.next + kHistoryCapacity - 1u - age) % kHistoryCapacity;
  return track.samples[index];
}

void RecordLegacyFrame(PoseTrack &track, uint32_t serverFrame,
                       uint64_t timeUs) {
  LegacyFrameTrack &legacy = track.legacyFrames;
  if (legacy.count) {
    const LegacyFrameSample &newest = NewestLegacyFrame(legacy, 0);
    if (newest.serverFrame == serverFrame && newest.timeUs == timeUs)
      return;
  }
  legacy.samples[legacy.next] = {timeUs, serverFrame};
  legacy.next = (legacy.next + 1u) % kHistoryCapacity;
  legacy.count = std::min(legacy.count + 1u, kHistoryCapacity);
}

[[nodiscard]] bool FindLegacySnapshotTime(const PoseTrack &track,
                                          uint32_t serverFrame,
                                          uint32_t serverFrameDelta,
                                          uint64_t &snapshotTimeUs,
                                          int &estimatedIntervalMs) {
  const LegacyFrameTrack &legacy = track.legacyFrames;
  const LegacyFrameSample *matched = nullptr;
  uint32_t matchedAge = 0;
  for (uint32_t age = 0; age < legacy.count; ++age) {
    const LegacyFrameSample &sample = NewestLegacyFrame(legacy, age);
    if (sample.serverFrame == serverFrame) {
      matched = &sample;
      matchedAge = age;
      break;
    }
    if (sample.serverFrame < serverFrame)
      break;
  }
  if (!matched)
    return false;

  snapshotTimeUs = matched->timeUs;
  estimatedIntervalMs = 0;
  if (!serverFrameDelta || serverFrameDelta >= serverFrame)
    return true;

  const uint32_t previousSentFrame = serverFrame - serverFrameDelta;
  for (uint32_t age = matchedAge + 1u; age < legacy.count; ++age) {
    const LegacyFrameSample &older = NewestLegacyFrame(legacy, age);
    if (older.serverFrame > previousSentFrame)
      continue;
    if (older.serverFrame < previousSentFrame)
      break;
    if (matched->timeUs > older.timeUs) {
      estimatedIntervalMs = static_cast<int>(std::min<uint64_t>(
          (matched->timeUs - older.timeUs) / UINT64_C(1000),
          static_cast<uint64_t>(kPolicyMaxRewindMs)));
    }
    break;
  }
  if (estimatedIntervalMs > 0)
    return true;

  for (uint32_t age = matchedAge + 1u; age < legacy.count; ++age) {
    const LegacyFrameSample &older = NewestLegacyFrame(legacy, age);
    if (older.serverFrame >= serverFrame)
      continue;
    if (matched->timeUs > older.timeUs) {
      estimatedIntervalMs = static_cast<int>(std::min<uint64_t>(
          (matched->timeUs - older.timeUs) / UINT64_C(1000),
          static_cast<uint64_t>(kPolicyMaxRewindMs)));
    }
    break;
  }
  return true;
}

[[nodiscard]] bool ResolveCanonicalDecision(
    std::size_t shooterIndex, worr_rewind_policy_decision_v1 &decision,
    bool &contextAvailable) {
  contextAvailable = false;
  if (!commandContextImport || !commandContextImport->GetCurrent ||
      !commandContextImport->GetScopeState)
    return false;

  const uint32_t scopeState = commandContextImport->GetScopeState();
  if (scopeState == WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY)
    return false;
  contextAvailable = true;
  if (scopeState != WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  worr_authoritative_command_context_v1 context{};
  if (!commandContextImport->GetCurrent(&context)) {
    ++diagnostics.canonicalRejected;
    return false;
  }
  if (shooterIndex >= policyStates.size() ||
      context.struct_size != sizeof(context) ||
      context.schema_version != WORR_COMMAND_CONTEXT_API_VERSION ||
      context.client_index != shooterIndex ||
      !AdoptAuthoritativeMapEpoch(context.current_snapshot.snapshot_id.epoch)) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  // A synthesized gap command has no client-observed render instant.  Until
  // the server supplies bounded per-gap timing evidence, it is never eligible
  // for historical hit validation (and must not inherit a zero-error proof).
  if (context.command.render_watermark.provenance ==
      WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  CommandDecisionCache &cache = decisionCaches[shooterIndex];
  if (cache.valid && SameCommand(cache.commandId, context.command.command_id)) {
    if (SameCanonicalBytes(cache.authority, context)) {
      ++diagnostics.canonicalReused;
      decision = cache.decision;
      return cache.accepted;
    }
    // The server must never mutate authority behind a command identity.  Do
    // not return stale authority or allow a previously rejected same-ID record
    // to become eligible through mutation.
    cache = {};
    cache.valid = true;
    cache.commandId = context.command.command_id;
    cache.authority = context;
    ++diagnostics.canonicalRejected;
    return false;
  }

  cache = {};
  cache.valid = true;
  cache.commandId = context.command.command_id;
  cache.authority = context;
  if (MaxRewindMs() <= 0) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  worr_rewind_policy_config_v1 config{};
  Worr_RewindPolicyConfigDefaultsV1(&config);
  config.target_window_us =
      static_cast<uint64_t>(MaxRewindMs()) * UINT64_C(1000);
  const int legacyErrorMs = std::clamp(
      sg_lag_compensation_legacy_error_ms
          ? sg_lag_compensation_legacy_error_ms->integer
          : 50,
      0, kPolicyMaxRewindMs);
  config.max_legacy_error_us =
      static_cast<uint64_t>(legacyErrorMs) * UINT64_C(1000);

  const bool evaluated = Worr_RewindPolicyResolveV1(
      &policyStates[shooterIndex], &config, &context.command,
      &context.current_snapshot, &context.mapping_proof, &cache.decision);
  if (!evaluated) {
    ++diagnostics.canonicalRejected;
    return false;
  }
  cache.accepted =
      (cache.decision.flags & WORR_REWIND_DECISION_ACCEPTED) != 0;
  decision = cache.decision;
  if (!cache.accepted) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  ++diagnostics.canonicalAccepted;
  ++diagnostics.sessions;
  if ((cache.decision.flags & WORR_REWIND_DECISION_CLAMPED) != 0)
    ++diagnostics.capped;
  return true;
}

[[nodiscard]] bool ResolveTarget(gentity_t *fromPlayer, RewindTarget &target,
                                 bool &canonicalContextAvailable,
                                 uint32_t &fallbackReason) {
  ++diagnostics.requests;
  canonicalContextAvailable = false;
  fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_SHOOTER_INELIGIBLE;
  if (!deathmatch || !deathmatch->integer || !g_lagCompensation ||
      !g_lagCompensation->integer) {
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_MASTER_DISABLED;
    return false;
  }
  if (!fromPlayer || !fromPlayer->client || (fromPlayer->svFlags & SVF_BOT)) {
    return false;
  }

  const std::size_t shooterIndex = ClientIndex(fromPlayer);
  if (shooterIndex >= poseTracks.size()) {
    ++diagnostics.rejectedClock;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED;
    return false;
  }

  if (ResolveCanonicalDecision(shooterIndex, target.decision,
                               canonicalContextAvailable)) {
    target.timeUs = target.decision.applied_time_us;
    target.mapEpoch = target.decision.snapshot_id.epoch;
    target.canonical = true;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_NONE;
    return true;
  }
  // A callback-scoped canonical context is authoritative.  Policy rejection
  // may use an uncompensated trace, but never the packet-ack rewind estimate.
  if (canonicalContextAvailable) {
    fallbackReason =
        (MaxRewindMs() <= 0 ||
         target.decision.struct_size == sizeof(target.decision))
            ? WORR_REWIND_OBSERVATION_FALLBACK_POLICY_REJECTED
            : WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED;
    MaybeReportDiagnostics();
    return false;
  }

  const uint32_t currentFrame = gi.ServerFrame();
  const uint32_t acknowledgedFrame = fromPlayer->client->cmd.serverFrame;
  if (!acknowledgedFrame || acknowledgedFrame >= currentFrame) {
    ++diagnostics.rejectedClock;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED;
    MaybeReportDiagnostics();
    return false;
  }

  uint64_t snapshotTimeUs = 0;
  int estimatedIntervalMs = 0;
  if (!FindLegacySnapshotTime(poseTracks[shooterIndex], acknowledgedFrame,
                              fromPlayer->client->cmd.serverFrameDelta,
                              snapshotTimeUs, estimatedIntervalMs)) {
    ++diagnostics.missingHistory;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS;
    MaybeReportDiagnostics();
    return false;
  }

  uint64_t currentTimeUs = 0;
  if (!TimeToMicroseconds(level.time, currentTimeUs) ||
      snapshotTimeUs > currentTimeUs) {
    ++diagnostics.rejectedClock;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED;
    MaybeReportDiagnostics();
    return false;
  }
  const uint64_t interpolationBiasUs =
      static_cast<uint64_t>(InterpolationBiasMs(estimatedIntervalMs)) *
      UINT64_C(1000);
  uint64_t rawAgeUs = currentTimeUs - snapshotTimeUs;
  rawAgeUs = rawAgeUs > std::numeric_limits<uint64_t>::max() -
                            interpolationBiasUs
                 ? std::numeric_limits<uint64_t>::max()
                 : rawAgeUs + interpolationBiasUs;
  const uint64_t maximumUs =
      static_cast<uint64_t>(MaxRewindMs()) * UINT64_C(1000);
  if (!maximumUs) {
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_POLICY_REJECTED;
    return false;
  }
  const uint64_t rewindUs = std::min(rawAgeUs, maximumUs);
  if (rewindUs != rawAgeUs)
    ++diagnostics.capped;

  target.timeUs = currentTimeUs - rewindUs;
  target.mapEpoch = historyMapEpoch;
  target.canonical = false;
  fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_NONE;
  ++diagnostics.sessions;
  return true;
}

[[nodiscard]] bool BuildPose(const gentity_t &entity, uint64_t timeUs,
                             worr_rewind_pose_v1 &pose) {
  worr_event_entity_ref_v1 identity{};
  if (!EntityRef(&entity, identity))
    return false;

  pose = {};
  pose.struct_size = sizeof(pose);
  pose.schema_version = WORR_REWIND_ABI_VERSION;
  pose.model_revision = WORR_REWIND_POSE_MODEL_REVISION;
  pose.entity = identity;
  pose.mover = AbsentEntityRef();
  pose.map_epoch = historyMapEpoch;
  pose.server_tick = captureTick;
  pose.server_time_us = timeUs;
  pose.solid = static_cast<uint32_t>(entity.solid);
  pose.clip_flags = static_cast<uint32_t>(entity.clipMask);
  pose.lifecycle = entity.deadFlag ? WORR_REWIND_LIFECYCLE_DEAD
                                   : WORR_REWIND_LIFECYCLE_ALIVE;

  const bool linked = entity.linked && entity.solid != SOLID_NOT;
  if (linked) {
    pose.flags |= WORR_REWIND_POSE_LINKED;
    pose.collision_shape = WORR_REWIND_COLLISION_BOUNDS;
  }
  if (linked && entity.takeDamage && !entity.deadFlag)
    pose.flags |= WORR_REWIND_POSE_DAMAGEABLE;
  if (entity.s.event == EV_PLAYER_TELEPORT ||
      entity.s.event == EV_OTHER_TELEPORT) {
    pose.flags |= WORR_REWIND_POSE_DISCONTINUITY_TELEPORT;
  }

  CopyVector(pose.origin, entity.s.origin);
  CopyVector(pose.angles, entity.s.angles);
  CopyVector(pose.velocity, entity.velocity);
  CopyVector(pose.mins, entity.mins);
  CopyVector(pose.maxs, entity.maxs);
  return Worr_RewindPoseValidateV1(&pose);
}

[[nodiscard]] bool EligibleLivePlayer(const gentity_t *entity) {
  return entity && entity->inUse && entity->client && entity->linked &&
         entity->solid != SOLID_NOT && entity->takeDamage && !entity->deadFlag;
}

[[nodiscard]] bool CaptureEligibleRoster(
    std::array<worr_event_entity_ref_v1, MAX_CLIENTS_KEX> &roster,
    uint32_t &rosterCount) {
  roster = {};
  rosterCount = 0;
  const std::size_t clientCount = std::min<std::size_t>(
      static_cast<std::size_t>(game.maxClients), poseTracks.size());
  for (std::size_t index = 0; index < clientCount; ++index) {
    gentity_t *current = &g_entities[index + 1u];
    if (!EligibleLivePlayer(current))
      continue;
    worr_event_entity_ref_v1 identity{};
    if (!EntityRef(current, identity) || rosterCount == roster.size())
      return false;
    roster[rosterCount++] = identity;
  }
  return true;
}

[[nodiscard]] bool SameEligibleRoster(
    const FrozenSceneCache &cache,
    const std::array<worr_event_entity_ref_v1, MAX_CLIENTS_KEX> &roster,
    uint32_t rosterCount) {
  if (cache.eligibleRosterCount != rosterCount)
    return false;
  for (uint32_t i = 0; i < rosterCount; ++i) {
    if (cache.eligibleRoster[i].index != roster[i].index ||
        cache.eligibleRoster[i].generation != roster[i].generation) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] const worr_rewind_scene_v1 *CanonicalScene(
    std::size_t shooterIndex,
    const worr_rewind_policy_decision_v1 &decision) {
  FrozenSceneCache &cache = sceneCaches[shooterIndex];
  std::array<worr_event_entity_ref_v1, MAX_CLIENTS_KEX> eligibleRoster{};
  uint32_t eligibleRosterCount = 0;
  if (!CaptureEligibleRoster(eligibleRoster, eligibleRosterCount)) {
    ++diagnostics.missingHistory;
    ++diagnostics.scenesRejected;
    return nullptr;
  }
  if (cache.valid && SameCommand(cache.commandId, decision.command_id) &&
      SameCanonicalBytes(cache.decision, decision) &&
      SameEligibleRoster(cache, eligibleRoster, eligibleRosterCount)) {
    ++diagnostics.scenesReused;
    return &cache.scene;
  }

  cache = {};
  cache.commandId = decision.command_id;
  cache.decision = decision;
  cache.eligibleRoster = eligibleRoster;
  cache.eligibleRosterCount = eligibleRosterCount;
  if (!Worr_RewindSceneInitV1(&cache.scene, cache.storage.data(),
                              static_cast<uint32_t>(cache.storage.size()),
                              &decision)) {
    ++diagnostics.scenesRejected;
    return nullptr;
  }

  for (uint32_t rosterIndex = 0; rosterIndex < eligibleRosterCount;
       ++rosterIndex) {
    const worr_event_entity_ref_v1 identity = eligibleRoster[rosterIndex];
    const std::size_t index = static_cast<std::size_t>(identity.index - 1u);

    worr_rewind_pose_query_v1 query{};
    query.struct_size = sizeof(query);
    query.schema_version = WORR_REWIND_ABI_VERSION;
    query.entity = identity;
    query.map_epoch = decision.snapshot_id.epoch;
    query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
    query.target_time_us = decision.applied_time_us;
    worr_rewind_pose_result_v1 result{};
    if (!Worr_RewindHistoryQueryV1(&poseTracks[index].history, &query,
                                   &result)) {
      ++diagnostics.scenesRejected;
      return nullptr;
    }
    if (!result.found) {
      ++diagnostics.missingHistory;
      ++diagnostics.scenesRejected;
      return nullptr;
    }
    if ((result.pose.flags & (WORR_REWIND_POSE_LINKED |
                              WORR_REWIND_POSE_DAMAGEABLE)) !=
        (WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE)) {
      continue;
    }
    if (!Worr_RewindSceneAddResultV1(&cache.scene, &result)) {
      ++diagnostics.scenesRejected;
      return nullptr;
    }
  }
  if (!Worr_RewindSceneSealV1(&cache.scene)) {
    ++diagnostics.scenesRejected;
    return nullptr;
  }
  cache.valid = true;
  ++diagnostics.scenesBuilt;
  return &cache.scene;
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

[[nodiscard]] bool PointerIgnored(const gentity_t *entity,
                                  const gentity_t *passEntity,
                                  gentity_t *const *ignoredEntities,
                                  std::size_t ignoredEntityCount) {
  if (entity == passEntity)
    return true;
  for (std::size_t i = 0; i < ignoredEntityCount; ++i) {
    if (entity == ignoredEntities[i])
      return true;
  }
  return false;
}

[[nodiscard]] bool ShouldSkipCurrentEntity(
    const gentity_t *entity, const gentity_t *passEntity,
    gentity_t *const *ignoredEntities, std::size_t ignoredEntityCount,
    contents_t contentMask) {
  if (!entity || !entity->inUse || !entity->linked ||
      entity->solid == SOLID_NOT ||
      PointerIgnored(entity, passEntity, ignoredEntities,
                     ignoredEntityCount)) {
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

[[nodiscard]] TraceBounds CalculateTraceBounds(const Vector3 &start,
                                                const Vector3 *mins,
                                                const Vector3 *maxs,
                                                const Vector3 &end) {
  const Vector3 traceMins = mins ? *mins : vec3_origin;
  const Vector3 traceMaxs = maxs ? *maxs : vec3_origin;
  TraceBounds bounds{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (end[axis] > start[axis]) {
      bounds.mins[axis] = start[axis] + traceMins[axis] - 1.0f;
      bounds.maxs[axis] = end[axis] + traceMaxs[axis] + 1.0f;
    } else {
      bounds.mins[axis] = end[axis] + traceMins[axis] - 1.0f;
      bounds.maxs[axis] = start[axis] + traceMaxs[axis] + 1.0f;
    }
  }
  return bounds;
}

[[nodiscard]] bool PoseTouchesBounds(const worr_rewind_pose_v1 &pose,
                                     const TraceBounds &bounds) {
  const Vector3 origin = ToVector(pose.origin);
  const Vector3 absoluteMins = origin + ToVector(pose.mins);
  const Vector3 absoluteMaxs = origin + ToVector(pose.maxs);
  return absoluteMins.x <= bounds.maxs.x &&
         absoluteMins.y <= bounds.maxs.y &&
         absoluteMins.z <= bounds.maxs.z &&
         absoluteMaxs.x >= bounds.mins.x &&
         absoluteMaxs.y >= bounds.mins.y &&
         absoluteMaxs.z >= bounds.mins.z;
}

trace_t TraceCurrentScene(const Vector3 &start, const Vector3 *mins,
                          const Vector3 *maxs, const Vector3 &end,
                          gentity_t *passEntity, contents_t contentMask,
                          gentity_t *const *ignoredEntities,
                          std::size_t ignoredEntityCount,
                          bool replaceLivingPlayers,
                          const TraceBounds &bounds) {
  trace_t result = ClipEntity(world, start, mins, maxs, end, contentMask);
  if (result.fraction == 0.0f)
    return result;

  const std::size_t candidateCount = gi.BoxEntities(
      bounds.mins, bounds.maxs, traceCandidates.data(), traceCandidates.size(),
      AREA_SOLID, nullptr, nullptr);
  for (std::size_t i = 0; i < candidateCount && !result.allSolid; ++i) {
    gentity_t *entity = traceCandidates[i];
    if (ShouldSkipCurrentEntity(entity, passEntity, ignoredEntities,
                                ignoredEntityCount, contentMask)) {
      continue;
    }
    if (replaceLivingPlayers &&
        (entity->client || (entity->svFlags & SVF_PLAYER)) &&
        entity->takeDamage && !entity->deadFlag) {
      continue;
    }
    const trace_t clipped =
        ClipEntity(entity, start, mins, maxs, end, contentMask);
    MergeTrace(result, clipped, entity);
  }
  return result;
}

[[nodiscard]] bool InitIgnoreSet(
    worr_rewind_ignore_set_v1 &ignoreSet,
    std::array<worr_event_entity_ref_v1, kIgnoreCapacity> &storage,
    gentity_t *passEntity, gentity_t *const *ignoredEntities,
    std::size_t ignoredEntityCount) {
  if (!Worr_RewindIgnoreSetInitV1(
          &ignoreSet, storage.data(), static_cast<uint32_t>(storage.size()))) {
    return false;
  }
  auto add = [&ignoreSet](gentity_t *entity) {
    worr_event_entity_ref_v1 ref{};
    return !EntityRef(entity, ref) || Worr_RewindIgnoreSetAddV1(&ignoreSet, ref);
  };
  if (!add(passEntity))
    return false;
  for (std::size_t i = 0; i < ignoredEntityCount; ++i) {
    if (!add(ignoredEntities[i]))
      return false;
  }
  diagnostics.ignoredIdentities += ignoreSet.count;
  return true;
}

void ConfigureHistoricalProxy(const worr_rewind_pose_v1 &pose,
                              const gentity_t &current) {
  historicalProxy.inUse = true;
  historicalProxy.client = nullptr;
  historicalProxy.owner = nullptr;
  historicalProxy.linked = false;
  historicalProxy.s.number = 0;
  historicalProxy.s.modelIndex = 0;
  historicalProxy.s.origin = ToVector(pose.origin);
  historicalProxy.s.angles = ToVector(pose.angles);
  historicalProxy.mins = ToVector(pose.mins);
  historicalProxy.maxs = ToVector(pose.maxs);
  historicalProxy.solid = static_cast<solid_t>(pose.solid);
  historicalProxy.clipMask = static_cast<contents_t>(pose.clip_flags);
  historicalProxy.svFlags = current.svFlags;
}

void ClipHistoricalPose(trace_t &result, const worr_rewind_pose_v1 &pose,
                        uint32_t queryReason, const TraceBounds &bounds,
                        const Vector3 &start, const Vector3 *mins,
                        const Vector3 *maxs, const Vector3 &end,
                        gentity_t *passEntity, contents_t contentMask,
                        gentity_t *const *ignoredEntities,
                        std::size_t ignoredEntityCount) {
  if (result.allSolid ||
      (pose.flags & (WORR_REWIND_POSE_LINKED |
                     WORR_REWIND_POSE_DAMAGEABLE)) !=
          (WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE) ||
      pose.collision_shape != WORR_REWIND_COLLISION_BOUNDS ||
      !PoseTouchesBounds(pose, bounds)) {
    return;
  }

  gentity_t *current = EntityFromRef(pose.entity);
  if (!current || !current->client || !current->takeDamage ||
      current->deadFlag ||
      ShouldSkipCurrentEntity(current, passEntity, ignoredEntities,
                              ignoredEntityCount, contentMask)) {
    return;
  }

  ConfigureHistoricalProxy(pose, *current);
  const trace_t clipped = ClipEntity(&historicalProxy, start, mins, maxs, end,
                                     contentMask);
  MergeTrace(result, clipped, current);
  ++diagnostics.targets;
  if (queryReason == WORR_REWIND_QUERY_INTERPOLATED)
    ++diagnostics.interpolated;
  if (queryReason == WORR_REWIND_QUERY_DISCONTINUITY_FLOOR)
    ++diagnostics.discontinuities;
}

trace_t NativeTrace(const Vector3 &start, const Vector3 *mins,
                    const Vector3 *maxs, const Vector3 &end,
                    gentity_t *passEntity, contents_t contentMask,
                    gentity_t *const *ignoredEntities,
                    std::size_t ignoredEntityCount) {
  if (!ignoredEntityCount) {
    if (mins && maxs)
      return gi.trace(start, *mins, *maxs, end, passEntity, contentMask);
    return gi.traceLine(start, end, passEntity, contentMask);
  }
  const TraceBounds bounds = CalculateTraceBounds(start, mins, maxs, end);
  return TraceCurrentScene(start, mins, maxs, end, passEntity, contentMask,
                           ignoredEntities, ignoredEntityCount, false, bounds);
}

[[nodiscard]] bool ObservationEnabled() {
  return sg_lag_compensation_debug &&
         sg_lag_compensation_debug->integer >= 2;
}

void HashObservationBytes(uint64_t &hash, const void *data, std::size_t size) {
  constexpr uint64_t kFnvPrime = UINT64_C(1099511628211);
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= kFnvPrime;
  }
}

template <typename T>
void HashObservationValue(uint64_t &hash, const T &value) {
  HashObservationBytes(hash, &value, sizeof(value));
}

// A query guard deliberately covers the authoritative player collision roster
// rather than the historical proxy or caches.  The before/after fingerprint is
// diagnostic evidence that a lag-compensated trace did not mutate live state.
[[nodiscard]] uint64_t AuthoritativeCollisionHash() {
  uint64_t hash = UINT64_C(14695981039346656037);
  const std::size_t count = std::min<std::size_t>(
      static_cast<std::size_t>(game.maxClients), poseTracks.size());
  for (std::size_t index = 0; index < count; ++index) {
    const gentity_t &entity = g_entities[index + 1u];
    HashObservationValue(hash, entity.inUse);
    HashObservationValue(hash, entity.linked);
    HashObservationValue(hash, entity.s.number);
    HashObservationValue(hash, clientLifeGenerations[index]);
    HashObservationValue(hash, entity.solid);
    HashObservationValue(hash, entity.clipMask);
    HashObservationValue(hash, entity.svFlags);
    HashObservationValue(hash, entity.takeDamage);
    HashObservationValue(hash, entity.deadFlag);
    HashObservationValue(hash, entity.health);
    HashObservationValue(hash, entity.s.origin);
    HashObservationValue(hash, entity.mins);
    HashObservationValue(hash, entity.maxs);
  }
  return hash ? hash : UINT64_C(1);
}

void CopyDecisionToObservation(
    const worr_rewind_policy_decision_v1 &decision,
    worr_rewind_observation_v1 &observation) {
  if (decision.struct_size != sizeof(decision) ||
      decision.schema_version != WORR_REWIND_ABI_VERSION) {
    return;
  }
  observation.policy_reason = decision.reason;
  observation.command_id = decision.command_id;
  observation.snapshot_id = decision.snapshot_id;
  observation.source_snapshot_id = decision.source_snapshot_id;
  observation.requested_time_us = decision.requested_time_us;
  observation.mapped_time_us = decision.mapped_time_us;
  observation.applied_time_us = decision.applied_time_us;
  observation.mapping_error_bound_us = decision.mapping_error_bound_us;
}

trace_t TraceHistoricalScene(gentity_t *fromPlayer, const Vector3 &start,
                             const Vector3 *mins, const Vector3 *maxs,
                             const Vector3 &end, gentity_t *passEntity,
                             contents_t contentMask,
                             gentity_t *const *ignoredEntities,
                             std::size_t ignoredEntityCount,
                             uint32_t weaponPolicy) {
  const bool observe = ObservationEnabled();
  worr_rewind_observation_v1 observation{};
  std::chrono::steady_clock::time_point observationStart{};
  if (observe) {
    observationStart = std::chrono::steady_clock::now();
    (void)Worr_RewindObservationInitV1(&observation, weaponPolicy);
    if (g_lagCompensation && g_lagCompensation->integer)
      observation.flags |= WORR_REWIND_OBSERVATION_MASTER_ENABLED;
    observation.authoritative_hash_before = AuthoritativeCollisionHash();
  }

  auto finishObservation = [&](trace_t result) {
    if (!observe)
      return result;

    if (observation.path == WORR_REWIND_OBSERVATION_PATH_NONE)
      observation.path = WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD;
    observation.trace_fraction = std::clamp(result.fraction, 0.0f, 1.0f);
    if (result.ent)
      (void)EntityRef(result.ent, observation.hit_entity);

    if (observation.outcome == WORR_REWIND_OBSERVATION_OUTCOME_NONE) {
      if ((observation.path == WORR_REWIND_OBSERVATION_PATH_CANONICAL ||
           observation.path == WORR_REWIND_OBSERVATION_PATH_LEGACY) &&
          (observation.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) == 0) {
        observation.outcome =
            result.ent && result.ent->client && result.fraction < 1.0f
                ? WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT
                : WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_MISS;
      } else if (result.fraction == 0.0f) {
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_BLOCKED;
        if (observation.fallback_reason ==
            WORR_REWIND_OBSERVATION_FALLBACK_NONE) {
          observation.fallback_reason =
              WORR_REWIND_OBSERVATION_FALLBACK_CURRENT_WORLD_BLOCKED;
        }
      } else {
        observation.outcome = result.fraction < 1.0f
                                  ? WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_HIT
                                  : WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_MISS;
      }
    }

    observation.authoritative_hash_after = AuthoritativeCollisionHash();
    observation.flags |= WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED;
    if (observation.authoritative_hash_before ==
        observation.authoritative_hash_after) {
      observation.flags |= WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
    }
    const auto elapsed = std::chrono::steady_clock::now() - observationStart;
    observation.duration_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());

    uint64_t sequence = 0;
    if (Worr_RewindObservationJournalAppendV1(&observationJournal,
                                              &observation, &sequence) &&
        sg_lag_compensation_debug->integer >= 3) {
      gi.Com_PrintFmt(
          "lagcomp trace seq={} weapon={} path={} outcome={} fallback={} "
          "policy={} query={} candidates={} requested_us={} applied_us={} "
          "fraction={:.6f} scene={} authority_unchanged={} duration_ns={}\n",
          static_cast<unsigned long long>(sequence), observation.weapon_policy,
          observation.path, observation.outcome, observation.fallback_reason,
          observation.policy_reason, observation.query_reason,
          observation.candidate_count,
          static_cast<unsigned long long>(observation.requested_time_us),
          static_cast<unsigned long long>(observation.applied_time_us),
          observation.trace_fraction,
          static_cast<unsigned long long>(observation.scene_hash),
          observation.authoritative_hash_before ==
                  observation.authoritative_hash_after
              ? 1u
              : 0u,
          static_cast<unsigned long long>(observation.duration_ns));
    }
    return result;
  };

  RewindTarget target{};
  bool canonicalContextAvailable = false;
  uint32_t fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_NONE;
  const bool targetsPlayers = (contentMask & CONTENTS_PLAYER) != 0;
  if (!targetsPlayers || !ResolveTarget(fromPlayer, target,
                                        canonicalContextAvailable,
                                        fallbackReason)) {
    if (observe) {
      observation.path = canonicalContextAvailable
                             ? WORR_REWIND_OBSERVATION_PATH_CANONICAL
                             : WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD;
      observation.fallback_reason =
          targetsPlayers
              ? fallbackReason
              : WORR_REWIND_OBSERVATION_FALLBACK_NO_PLAYER_CONTENT;
      observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
      if (canonicalContextAvailable) {
        observation.flags |= WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT;
        CopyDecisionToObservation(target.decision, observation);
        if (fallbackReason ==
            WORR_REWIND_OBSERVATION_FALLBACK_POLICY_REJECTED) {
          observation.outcome =
              WORR_REWIND_OBSERVATION_OUTCOME_POLICY_REJECTED;
        }
      }
    }
    return finishObservation(NativeTrace(start, mins, maxs, end, passEntity,
                                         contentMask, ignoredEntities,
                                         ignoredEntityCount));
  }

  if (observe) {
    observation.path = target.canonical
                           ? WORR_REWIND_OBSERVATION_PATH_CANONICAL
                           : WORR_REWIND_OBSERVATION_PATH_LEGACY;
    observation.flags |= WORR_REWIND_OBSERVATION_POLICY_ACCEPTED;
    observation.fallback_reason = WORR_REWIND_OBSERVATION_FALLBACK_NONE;
    if (target.canonical) {
      observation.flags |= WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT;
      CopyDecisionToObservation(target.decision, observation);
    } else {
      observation.requested_time_us = target.timeUs;
      observation.mapped_time_us = target.timeUs;
      observation.applied_time_us = target.timeUs;
    }
  }

  const TraceBounds bounds = CalculateTraceBounds(start, mins, maxs, end);
  trace_t result = TraceCurrentScene(
      start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
      ignoredEntityCount, true, bounds);
  if (result.fraction == 0.0f) {
    if (observe) {
      observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
      observation.fallback_reason =
          WORR_REWIND_OBSERVATION_FALLBACK_CURRENT_WORLD_BLOCKED;
      observation.outcome =
          WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_BLOCKED;
    }
    MaybeReportDiagnostics();
    return finishObservation(result);
  }

  const std::size_t shooterIndex = ClientIndex(fromPlayer);
  if (target.canonical) {
    const worr_rewind_scene_v1 *scene =
        CanonicalScene(shooterIndex, target.decision);
    if (!scene) {
      // Policy accepted, but a sealed immutable scene could not be built.
      // Use an uncompensated collision query; do not use the legacy estimate.
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_SCENE_REJECTED;
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
      }
      return finishObservation(NativeTrace(
          start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
          ignoredEntityCount));
    }
    if (observe) {
      observation.flags |= WORR_REWIND_OBSERVATION_HISTORICAL_QUERY |
                           WORR_REWIND_OBSERVATION_HISTORICAL_SCENE;
      observation.candidate_count = scene->count;
      observation.scene_hash = scene->scene_hash;
      for (uint32_t index = 0; index < scene->count; ++index) {
        observation.query_reason = std::max<uint32_t>(
            observation.query_reason, scene->slots[index].query_reason);
      }
    }

    worr_rewind_ignore_set_v1 ignoreSet{};
    std::array<worr_event_entity_ref_v1, kIgnoreCapacity> ignoreStorage{};
    if (!InitIgnoreSet(ignoreSet, ignoreStorage, passEntity, ignoredEntities,
                       ignoredEntityCount)) {
      ++diagnostics.scenesRejected;
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_IGNORE_REJECTED;
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
      }
      return finishObservation(NativeTrace(
          start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
          ignoredEntityCount));
    }
    worr_rewind_trace_view_v1 view{};
    if (!Worr_RewindTraceViewV1(scene, &ignoreSet, &view)) {
      ++diagnostics.scenesRejected;
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_TRACE_VIEW_REJECTED;
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
      }
      return finishObservation(NativeTrace(
          start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
          ignoredEntityCount));
    }

    for (uint32_t i = 0; i < view.candidate_count && !result.allSolid; ++i) {
      const worr_rewind_scene_candidate_v1 &candidate = view.candidates[i];
      bool ignored = false;
      if (!Worr_RewindIgnoreSetContainsV1(&ignoreSet, candidate.pose.entity,
                                          &ignored) ||
          ignored) {
        continue;
      }
      ClipHistoricalPose(result, candidate.pose, candidate.query_reason,
                         bounds, start, mins, maxs, end, passEntity,
                         contentMask, ignoredEntities, ignoredEntityCount);
    }
  } else {
    const std::size_t clientCount = std::min<std::size_t>(
        static_cast<std::size_t>(game.maxClients),
        poseTracks.size());
    for (std::size_t index = 0; index < clientCount && !result.allSolid;
         ++index) {
      gentity_t *current = &g_entities[index + 1u];
      worr_event_entity_ref_v1 identity{};
      if (!EligibleLivePlayer(current))
        continue;
      if (!EntityRef(current, identity)) {
        ++diagnostics.missingHistory;
        if (observe) {
          observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
          observation.fallback_reason =
              WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS;
          observation.outcome =
              WORR_REWIND_OBSERVATION_OUTCOME_HISTORY_MISS;
        }
        return finishObservation(NativeTrace(
            start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
            ignoredEntityCount));
      }

      worr_rewind_pose_query_v1 query{};
      query.struct_size = sizeof(query);
      query.schema_version = WORR_REWIND_ABI_VERSION;
      query.entity = identity;
      query.map_epoch = target.mapEpoch;
      query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
      query.target_time_us = target.timeUs;
      worr_rewind_pose_result_v1 historical{};
      if (!Worr_RewindHistoryQueryV1(&poseTracks[index].history, &query,
                                     &historical) ||
          !historical.found) {
        ++diagnostics.missingHistory;
        if (observe) {
          observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK |
                               WORR_REWIND_OBSERVATION_HISTORICAL_QUERY;
          observation.fallback_reason =
              WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS;
          observation.outcome =
              WORR_REWIND_OBSERVATION_OUTCOME_HISTORY_MISS;
          observation.query_reason = historical.reason;
        }
        return finishObservation(NativeTrace(
            start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
            ignoredEntityCount));
      }
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_HISTORICAL_QUERY;
        observation.query_reason = historical.reason;
        ++observation.candidate_count;
      }
      ClipHistoricalPose(result, historical.pose, historical.reason, bounds,
                         start, mins, maxs, end, passEntity, contentMask,
                         ignoredEntities, ignoredEntityCount);
    }
  }

  MaybeReportDiagnostics();
  return finishObservation(result);
}

} // namespace

void LagCompensation_Init() {
  sg_lag_compensation_max_ms = gi.cvar(
      "sg_lag_compensation_max_ms", "200", CVAR_NOFLAGS);
  sg_lag_compensation_interp_ms = gi.cvar(
      "sg_lag_compensation_interp_ms", "-1", CVAR_NOFLAGS);
  sg_lag_compensation_debug =
      gi.cvar("sg_lag_compensation_debug", "0", CVAR_NOFLAGS);
  sg_lag_compensation_legacy_error_ms = gi.cvar(
      "sg_lag_compensation_legacy_error_ms", "50", CVAR_NOFLAGS);

  commandContextImport = nullptr;
  if (gi.GetExtension) {
    const auto *candidate =
        static_cast<const worr_command_context_import_v1 *>(
            gi.GetExtension(WORR_COMMAND_CONTEXT_IMPORT_V1));
    if (candidate && candidate->struct_size == sizeof(*candidate) &&
        candidate->api_version == WORR_COMMAND_CONTEXT_API_VERSION &&
        candidate->GetCurrent && candidate->GetScopeState) {
      commandContextImport = candidate;
    }
  }

  for (worr_rewind_policy_state_v1 &state : policyStates)
    (void)Worr_RewindPolicyStateInitV1(&state);
  (void)Worr_RewindObservationJournalInitV1(
      &observationJournal, observationStorage.data(),
      static_cast<uint32_t>(observationStorage.size()));
  provisionalMapEpoch = 0;
  diagnostics = {};
  initialized = true;
  LagCompensation_ResetMap();
}

void LagCompensation_Shutdown() {
  commandContextImport = nullptr;
  policyStates = {};
  decisionCaches = {};
  sceneCaches = {};
  poseTracks = {};
  observationJournal = {};
  observationStorage = {};
  initialized = false;
}

void LagCompensation_ResetMap() {
  if (!initialized)
    return;
  ++provisionalMapEpoch;
  if (provisionalMapEpoch == 0)
    provisionalMapEpoch = 1;
  authoritativeMapEpoch = 0;
  awaitingAuthoritativeMapEpoch = true;
  clientLifeGenerations = {};
  clientLifeGenerationExhausted = {};
  ResetHistories(provisionalMapEpoch);
  for (worr_rewind_policy_state_v1 &state : policyStates)
    (void)Worr_RewindPolicyStateInitV1(&state);
  (void)Worr_RewindObservationJournalInitV1(
      &observationJournal, observationStorage.data(),
      static_cast<uint32_t>(observationStorage.size()));
}

void LagCompensation_ResetClient(gentity_t *entity) {
  if (!initialized)
    return;
  const std::size_t index = ClientIndex(entity);
  if (index >= poseTracks.size())
    return;
  (void)InitTrack(poseTracks[index], static_cast<uint32_t>(index + 1u));
  (void)Worr_RewindPolicyStateInitV1(&policyStates[index]);
  decisionCaches[index] = {};
  sceneCaches[index] = {};
}

void LagCompensation_BeginClientLife(gentity_t *entity) {
  if (!initialized)
    return;
  const std::size_t index = ClientIndex(entity);
  if (index >= poseTracks.size())
    return;

  // Clear the old life first.  Exhaustion is sticky for the rest of the map:
  // generation zero makes pose capture and live-reference resolution fail
  // closed rather than aliasing an earlier life after uint32 wrap.
  (void)InitTrack(poseTracks[index], static_cast<uint32_t>(index + 1u));
  decisionCaches[index] = {};
  sceneCaches[index] = {};
  if (clientLifeGenerationExhausted[index] ||
      clientLifeGenerations[index] == UINT32_MAX) {
    clientLifeGenerations[index] = 0;
    clientLifeGenerationExhausted[index] = true;
    return;
  }

  // Commit the generation after clearing storage so every subsequent pose,
  // ignore identity, and live-reference check observes the same new life.
  ++clientLifeGenerations[index];
}

void LagCompensation_RecordFrame(gentity_t *entity) {
  const std::size_t index = ClientIndex(entity);
  if (!initialized || index >= poseTracks.size() || !entity ||
      !entity->client || !entity->inUse) {
    return;
  }

  uint64_t timeUs = 0;
  if (!TimeToMicroseconds(level.time, timeUs)) {
    ++diagnostics.appendRejected;
    return;
  }
  if (lastCaptureTimeUs == std::numeric_limits<uint64_t>::max() ||
      timeUs > lastCaptureTimeUs) {
    if (captureTick == std::numeric_limits<uint32_t>::max()) {
      ++diagnostics.appendRejected;
      return;
    }
    ++captureTick;
    lastCaptureTimeUs = timeUs;
  } else if (timeUs < lastCaptureTimeUs) {
    // The explicit map hook should precede a clock reset.  If it did not,
    // discard all old poses instead of accepting cross-map interpolation.
    ++provisionalMapEpoch;
    if (provisionalMapEpoch == 0)
      provisionalMapEpoch = 1;
    authoritativeMapEpoch = 0;
    awaitingAuthoritativeMapEpoch = true;
    ResetHistories(provisionalMapEpoch);
    captureTick = 1;
    lastCaptureTimeUs = timeUs;
  }

  PoseTrack &track = poseTracks[index];
  if (!track.initialized)
    return;
  if (track.history.count) {
    const uint32_t newestIndex = static_cast<uint32_t>(
        (static_cast<uint64_t>(track.history.head) +
         track.history.count - 1u) %
        track.history.capacity);
    const worr_rewind_pose_v1 &newest = track.history.slots[newestIndex];
    if (newest.server_time_us == timeUs && newest.server_tick == captureTick) {
      RecordLegacyFrame(track, gi.ServerFrame(), timeUs);
      return;
    }
  }

  worr_rewind_pose_v1 pose{};
  if (!BuildPose(*entity, timeUs, pose)) {
    ++diagnostics.appendRejected;
    return;
  }
  uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
  if (!Worr_RewindHistoryAppendV1(&track.history, &pose, &reason) ||
      reason != WORR_REWIND_APPEND_ACCEPTED) {
    ++diagnostics.appendRejected;
    return;
  }
  RecordLegacyFrame(track, gi.ServerFrame(), timeUs);
}

trace_t LagCompensation_TraceLine(gentity_t *fromPlayer,
                                  const Vector3 &start,
                                  const Vector3 &end,
                                  gentity_t *passEntity,
                                  contents_t contentMask,
                                  gentity_t *const *ignoredEntities,
                                  size_t ignoredEntityCount,
                                  uint32_t weaponPolicy) {
  return TraceHistoricalScene(fromPlayer, start, nullptr, nullptr, end,
                              passEntity, contentMask, ignoredEntities,
                              ignoredEntityCount, weaponPolicy);
}

trace_t LagCompensation_Trace(gentity_t *fromPlayer,
                              const Vector3 &start,
                              const Vector3 &mins,
                              const Vector3 &maxs,
                              const Vector3 &end,
                              gentity_t *passEntity,
                              contents_t contentMask,
                              uint32_t weaponPolicy) {
  return TraceHistoricalScene(fromPlayer, start, &mins, &maxs, end,
                              passEntity, contentMask, nullptr, 0,
                              weaponPolicy);
}

bool LagCompensation_CopyObservations(
    worr_rewind_observation_v1 *records, uint32_t recordsCapacity,
    uint32_t *recordCount) {
  if (!recordCount || (!records && recordsCapacity != 0) ||
      observationJournal.struct_size != sizeof(observationJournal)) {
    return false;
  }
  if (!records) {
    *recordCount = observationJournal.count;
    return true;
  }
  return Worr_RewindObservationJournalCopyV1(
      &observationJournal, records, recordsCapacity, recordCount);
}

bool LagCompensation_GetObservationTelemetry(
    worr_rewind_observation_telemetry_v1 *telemetry) {
  if (!telemetry ||
      !Worr_RewindObservationJournalValidateV1(&observationJournal)) {
    return false;
  }
  *telemetry = observationJournal.telemetry;
  return true;
}
