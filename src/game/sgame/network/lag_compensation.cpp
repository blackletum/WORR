// Copyright (c) 2026 The WORR Project
// Licensed under the GNU General Public License 2.0.

#include "lag_compensation.hpp"
#include "rewind_collision_import.hpp"
#include "shared/command_context.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

// Fixed storage keeps authoritative capture and weapon traces allocation-free.
// At the one-millisecond extreme this still covers twice the 250 ms hard cap.
constexpr uint32_t kHistoryCapacity = 512;
// Mover histories need only cover the bounded rewind window plus a generous
// gap allowance. Keeping their storage separate from player histories makes
// the maximum map cost explicit and prevents a map with many doors from
// allocating per-entity history dynamically.
constexpr uint32_t kMoverHistoryCapacity = 64;
constexpr uint32_t kMoverTrackCapacity = 64;
constexpr uint32_t kSceneCandidateCapacity =
    MAX_CLIENTS_KEX + kMoverTrackCapacity;
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

struct MoverTrack {
  std::array<worr_rewind_pose_v1, kMoverHistoryCapacity> storage{};
  worr_rewind_history_v1 history{};
  worr_event_entity_ref_v1 identity{};
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
  uint64_t moversCaptured = 0;
  uint64_t moverCaptureRejected = 0;
  uint64_t moverTrackExhausted = 0;
  uint64_t moverSceneCandidates = 0;
  uint64_t historicalBrushes = 0;
  uint64_t historicalBrushRejected = 0;
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
  std::array<worr_event_entity_ref_v1, kSceneCandidateCapacity> eligibleRoster{};
  std::array<worr_rewind_scene_candidate_v1, kSceneCandidateCapacity> storage{};
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

struct HistoricalBrushEntry {
  worr_event_entity_ref_v1 identity{};
  gentity_t *current = nullptr;
  worr_sgame_rewind_collision::Asset asset{};
};

// This context is built from a sealed scene before the current-world baseline
// runs. It allows that baseline to omit exactly the live movers represented by
// immutable historical brush poses, without unlinking or altering an edict.
struct HistoricalBrushContext {
  worr_sgame_rewind_collision::Map map{};
  std::array<HistoricalBrushEntry, kMoverTrackCapacity> entries{};
  uint32_t count = 0;
};

std::array<PoseTrack, MAX_CLIENTS_KEX> poseTracks{};
std::array<MoverTrack, kMoverTrackCapacity> moverTracks{};
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
const worr_sgame_rewind_collision::Import *rewindCollisionImport = nullptr;
cvar_t *sg_lag_compensation_max_ms = nullptr;
cvar_t *sg_lag_compensation_interp_ms = nullptr;
cvar_t *sg_lag_compensation_debug = nullptr;
cvar_t *sg_lag_compensation_legacy_error_ms = nullptr;
cvar_t *sg_worr_rewind_mover_selftest_status = nullptr;
cvar_t *sg_worr_rewind_rail_damage_selftest_status = nullptr;
cvar_t *sg_worr_rewind_canonical_rail_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_machinegun_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_chaingun_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_super_shotgun_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_disruptor_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_held_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_sustained_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_release_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_held_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_sustained_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_release_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_discharge_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_shotgun_damage_status = nullptr;

// This fixture is intentionally smaller than the general weapon diagnostics:
// its only job is to make a real command->ClientThink->Item::weaponThink path
// observable for one declared hitscan policy. It cannot create a command
// context, choose a rewind instant, or supply a hit result. Those remain
// server and weapon-path authority.
enum class CanonicalRailProbeStage : uint8_t {
  Idle,
  WaitingForPlayers,
  CapturingHistory,
  WaitingForCanonicalAttack,
  AwaitingDamage,
  Passed,
  Failed,
};

struct CanonicalRailProbeState {
  CanonicalRailProbeStage stage = CanonicalRailProbeStage::Idle;
  gentity_t *shooter = nullptr;
  gentity_t *target = nullptr;
  gentity_t *water = nullptr;
  worr_event_entity_ref_v1 shooter_identity{};
  worr_event_entity_ref_v1 target_identity{};
  worr_event_entity_ref_v1 water_identity{};
  worr_command_id_v1 command_id{};
  Vector3 shooter_origin{};
  Vector3 historical_target_origin{64.0f, 192.0f, 64.0f};
  Vector3 current_target_origin{};
  Vector3 current_target_offset{0.0f, 96.0f, 0.0f};
  uint64_t applied_age_us = 0;
  uint32_t target_history_captures = 0;
  uint32_t target_capture_prepares = 0;
  uint32_t target_capture_callbacks = 0;
  uint32_t capture_append_rejections = 0;
  uint32_t eligible_candidates = 0;
  uint32_t playing_candidates = 0;
  uint32_t observation_path = 0;
  uint32_t observation_outcome = 0;
  uint32_t observation_fallback = 0;
  uint32_t observation_flags = 0;
  uint32_t observation_query = 0;
  uint32_t observation_snapshot_epoch = 0;
  uint32_t history_epoch = 0;
  uint32_t target_history_count = 0;
  uint64_t observation_applied_time_us = 0;
  uint64_t latest_capture_time_us = 0;
  uint64_t trace_current_time_us = 0;
  uint64_t context_snapshot_time_us = 0;
  uint64_t context_mapped_time_us = 0;
  uint32_t weapon_policy = WORR_REWIND_WEAPON_UNSPECIFIED;
  item_id_t weapon_item = IT_NULL;
  uint32_t expected_damage = 0;
  int initial_ammo = 8;
  int weapon_idle_frame = 0;
  float target_distance = 112.0f;
  uint64_t damage_settle_delay_us = 0;
  uint64_t damage_settle_deadline_us = 0;
  uint64_t water_trace_observation_sequence = 0;
  bool defer_damage_evaluation_until_deadline = false;
  bool release_required = false;
  bool release_received = false;
  bool release_damage_stable = false;
  bool water_retrace_required = false;
  bool water_retrace_observed = false;
  bool sustained_hold_required = false;
  bool sustained_hold_interrupted = false;
  bool thunderbolt_discharge_required = false;
  bool thunderbolt_discharge_observed = false;
  bool thunderbolt_discharge_ammo_drained = false;
  item_id_t thunderbolt_discharge_ammo_item = IT_NULL;
  int shooter_health_before = 0;
  uint32_t release_damage_before = 0;
  uint64_t release_grace_deadline_us = 0;
  uint32_t weapon_damage = 0;
  int target_health_before = 0;
  uint32_t failure = 0;
  bool players_ready = false;
  bool history_ready = false;
  bool canonical_scope = false;
  bool attack_received = false;
  bool weapon_callback = false;
  bool canonical_historical_hit = false;
  bool current_geometry_unchanged = false;
};

constexpr uint32_t kCanonicalRailProbeRequiredHistoryCaptures = 6;
constexpr int kCanonicalRailProbeTargetHealth = 1000;
constexpr int kCanonicalRailProbeExpectedDamage = 80;
constexpr int kCanonicalMachinegunProbeExpectedDamage = 8;
constexpr int kCanonicalChaingunProbeExpectedDamage = 18;
constexpr int kCanonicalSuperShotgunProbeExpectedDamage = 120;
constexpr int kCanonicalDisruptorProbeExpectedDamage = 45;
constexpr int kCanonicalPlasmaBeamProbeExpectedDamage = 8;
constexpr int kCanonicalPlasmaBeamHeldProbeExpectedDamage = 24;
constexpr int kCanonicalPlasmaBeamSustainedProbeExpectedDamage = 256;
constexpr int kCanonicalPlasmaBeamReleaseProbeExpectedDamage = 24;
constexpr int kCanonicalPlasmaBeamWaterRetraceProbeExpectedDamage = 4;
constexpr int kCanonicalThunderboltProbeExpectedDamage = 8;
constexpr int kCanonicalThunderboltHeldProbeExpectedDamage = 24;
constexpr int kCanonicalThunderboltSustainedProbeExpectedDamage = 256;
constexpr int kCanonicalThunderboltReleaseProbeExpectedDamage = 24;
constexpr int kCanonicalThunderboltWaterRetraceProbeExpectedDamage = 4;
constexpr int kCanonicalThunderboltDischargeProbeExpectedDamage = 70;
constexpr int kCanonicalThunderboltDischargeProbeCells = 8;
// Plasma Beam requires two Cells to start each tick but consumes one. Supply
// 33 Cells so both beam policies can complete exactly 32 normal 8-damage
// ticks; the gate passes on the 32nd tick before Thunderbolt can consume the
// spare Cell.
constexpr int kCanonicalBeamSustainedProbeCells = 33;
constexpr uint64_t kCanonicalBeamSustainedProbeTimeoutUs = UINT64_C(5000000);
// The controlled view angle must remain valid for the complete five-second
// sustained window. Keep one second of tolerance for command and server-frame
// cadence without manufacturing a client angle or a weapon action.
constexpr GameTime kCanonicalBeamSustainedProbeAngleLockDuration = 6_sec;
constexpr int kCanonicalShotgunProbeExpectedDamage = 48;
constexpr uint64_t kCanonicalBeamReleaseGraceUs = UINT64_C(250000);

CanonicalRailProbeState canonicalRailProbe{};

void CanonicalRailProbePrepareFrameCapture(gentity_t *entity);
void CanonicalRailProbeCaptureFrame(gentity_t *entity);
void CanonicalRailProbeObserveTrace(
    gentity_t *shooter, const worr_rewind_observation_v1 &observation);
bool CanonicalRailProbeArm();
bool CanonicalMachinegunProbeArm();
bool CanonicalChaingunProbeArm();
bool CanonicalSuperShotgunProbeArm();
bool CanonicalDisruptorProbeArm();
bool CanonicalPlasmaBeamProbeArm();
bool CanonicalPlasmaBeamHeldProbeArm();
bool CanonicalPlasmaBeamSustainedProbeArm();
bool CanonicalPlasmaBeamReleaseProbeArm();
bool CanonicalPlasmaBeamWaterRetraceProbeArm();
bool CanonicalThunderboltProbeArm();
bool CanonicalThunderboltHeldProbeArm();
bool CanonicalThunderboltSustainedProbeArm();
bool CanonicalThunderboltReleaseProbeArm();
bool CanonicalThunderboltWaterRetraceProbeArm();
bool CanonicalThunderboltDischargeProbeArm();
bool CanonicalShotgunProbeArm();

uint32_t provisionalMapEpoch = 0;
uint32_t historyMapEpoch = 1;
uint32_t authoritativeMapEpoch = 0;
uint32_t captureTick = 0;
uint64_t lastCaptureTimeUs = std::numeric_limits<uint64_t>::max();
bool awaitingAuthoritativeMapEpoch = true;
bool initialized = false;

// These caches contain the full per-client scene/history working sets.  Their
// contents are meaningful only while valid; clearing the whole arrays with an
// aggregate assignment makes MSVC materialize a multi-megabyte temporary on
// the caller's stack.  Invalidation preserves the same observable contract
// without making map transitions or shutdown depend on stack size.
void InvalidateDecisionCaches() {
  for (CommandDecisionCache &cache : decisionCaches)
    cache.valid = false;
}

void InvalidateSceneCaches() {
  for (FrozenSceneCache &cache : sceneCaches)
    cache.valid = false;
}

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

// Server snapshots, canonical command proofs, and retained collision poses
// must share one timeline. GameTime intentionally pauses before a client is
// admitted, while the server's snapshot frame/timeline remains monotonic;
// using GameTime here made canonical decisions point into a different epoch
// of time after an idle dedicated server had accepted its first player.
[[nodiscard]] bool CurrentAuthoritativeTimeUs(uint64_t &timeUs) {
  if (!gi.ServerSimulationTimeUs)
    return false;
  timeUs = gi.ServerSimulationTimeUs();
  return true;
}

// End-frame pose capture observes the state produced by the game frame that
// is currently executing. The engine advances its published simulation clock
// immediately after that frame, so derive that completed-state timestamp
// without waiting for the next frame (where a command could already trace).
[[nodiscard]] bool CompletedAuthoritativeFrameTimeUs(uint64_t &timeUs) {
  uint64_t currentTimeUs = 0;
  if (!CurrentAuthoritativeTimeUs(currentTimeUs) || gi.frameTimeMs == 0)
    return false;
  const uint64_t frameTimeUs =
      static_cast<uint64_t>(gi.frameTimeMs) * UINT64_C(1000);
  if (currentTimeUs > std::numeric_limits<uint64_t>::max() - frameTimeUs)
    return false;
  timeUs = currentTimeUs + frameTimeUs;
  return true;
}

[[nodiscard]] worr_event_entity_ref_v1 AbsentEntityRef() {
  // Rewind poses define their optional mover reference as the all-zero
  // sentinel. The event-stream NO_ENTITY sentinel is deliberately not valid
  // in this ABI and would make every non-rider and mover pose reject.
  return {};
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
  if (ref.index == 0 || ref.index == WORR_EVENT_NO_ENTITY ||
      ref.index >= MAX_ENTITIES ||
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

[[nodiscard]] bool InitMoverTrack(MoverTrack &track, uint32_t entityIndex) {
  worr_rewind_history_config_v1 config{};
  Worr_RewindHistoryConfigDefaultsV1(&config);
  track.initialized = Worr_RewindHistoryInitV1(
      &track.history, track.storage.data(), kMoverHistoryCapacity, entityIndex,
      &config);
  return track.initialized;
}

void ResetMoverTracks() {
  for (MoverTrack &track : moverTracks) {
    track.initialized = false;
    track.identity = AbsentEntityRef();
    std::memset(&track.history, 0, sizeof(track.history));
  }
}

void ResetHistories(uint32_t mapEpoch) {
  historyMapEpoch = mapEpoch ? mapEpoch : 1u;
  captureTick = 0;
  lastCaptureTimeUs = std::numeric_limits<uint64_t>::max();
  for (std::size_t i = 0; i < poseTracks.size(); ++i)
    (void)InitTrack(poseTracks[i], static_cast<uint32_t>(i + 1u));
  ResetMoverTracks();
  InvalidateDecisionCaches();
  InvalidateSceneCaches();
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

[[nodiscard]] bool RelabelMoverTrack(MoverTrack &track, uint32_t mapEpoch) {
  if (!track.initialized || !Worr_RewindHistoryValidateV1(&track.history))
    return false;

  const uint32_t count = track.history.count;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t index = static_cast<uint32_t>(
        (static_cast<uint64_t>(track.history.head) + i) %
        track.history.capacity);
    relabelScratch[i] = track.history.slots[index];
  }

  const uint32_t entityIndex = track.history.entity_index;
  const worr_event_entity_ref_v1 identity = track.identity;
  if (!InitMoverTrack(track, entityIndex))
    return false;
  track.identity = identity;
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
    for (MoverTrack &track : moverTracks) {
      if (track.initialized && !RelabelMoverTrack(track, mapEpoch)) {
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
  InvalidateSceneCaches();
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
      "scenes_rejected={} movers_captured={} mover_capture_rejected={} "
      "mover_track_exhausted={} mover_scene_candidates={} "
      "historical_brushes={} historical_brush_rejected={} "
      "ignored_identities={}\n",
      diagnostics.requests, diagnostics.sessions, diagnostics.targets,
      diagnostics.interpolated, diagnostics.discontinuities,
      diagnostics.capped, diagnostics.rejectedClock,
      diagnostics.missingHistory, diagnostics.appendRejected,
      diagnostics.canonicalAccepted, diagnostics.canonicalRejected,
      diagnostics.canonicalReused, diagnostics.scenesBuilt,
      diagnostics.scenesReused, diagnostics.scenesRejected,
      diagnostics.moversCaptured, diagnostics.moverCaptureRejected,
      diagnostics.moverTrackExhausted, diagnostics.moverSceneCandidates,
      diagnostics.historicalBrushes, diagnostics.historicalBrushRejected,
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
  if (!CurrentAuthoritativeTimeUs(currentTimeUs) ||
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

[[nodiscard]] bool EligibleLiveMover(const gentity_t *entity) {
  return entity && entity->inUse && !entity->client && entity->linked &&
         entity->solid == SOLID_BSP &&
         (entity->moveType == MoveType::Push || entity->moveType == MoveType::Stop) &&
         entity->s.modelIndex > MODELINDEX_WORLD &&
         entity->s.modelIndex != MODELINDEX_PLAYER;
}

[[nodiscard]] bool CurrentCollisionMap(
    worr_sgame_rewind_collision::Map &map) {
  map = {};
  if (!rewindCollisionImport ||
      rewindCollisionImport->struct_size != sizeof(*rewindCollisionImport) ||
      rewindCollisionImport->api_version !=
          worr_sgame_rewind_collision::kApiVersion ||
      !rewindCollisionImport->GetMapIdentity ||
      !rewindCollisionImport->ResolveInlineBrush ||
      !rewindCollisionImport->TraceTransformed ||
      !rewindCollisionImport->GetMapIdentity(&map)) {
    return false;
  }
  return map.struct_size == sizeof(map) &&
         map.schema_version == worr_sgame_rewind_collision::kSchemaVersion &&
         map.map_epoch != 0 && map.inline_model_count != 0 &&
         map.reserved0 == 0;
}

[[nodiscard]] bool ResolveMoverAsset(
    const gentity_t &entity, const worr_sgame_rewind_collision::Map &map,
    worr_sgame_rewind_collision::Asset &asset) {
  asset = {};
  if (!EligibleLiveMover(&entity) || !rewindCollisionImport ||
      !rewindCollisionImport->ResolveInlineBrush ||
      !rewindCollisionImport->ResolveInlineBrush(
          map.map_epoch, static_cast<uint32_t>(entity.s.modelIndex), &asset)) {
    return false;
  }
  return asset.struct_size == sizeof(asset) &&
         asset.schema_version == worr_sgame_rewind_collision::kSchemaVersion &&
         asset.handle.map_epoch == map.map_epoch && asset.handle.asset_id != 0 &&
         asset.handle.asset_hash != 0 &&
         asset.kind == worr_sgame_rewind_collision::kAssetInlineBrush &&
         asset.source_model_index == static_cast<uint32_t>(entity.s.modelIndex) &&
         asset.map_checksum == map.map_checksum && asset.reserved0 == 0 &&
         std::isfinite(asset.local_mins[0]) && std::isfinite(asset.local_mins[1]) &&
         std::isfinite(asset.local_mins[2]) && std::isfinite(asset.local_maxs[0]) &&
         std::isfinite(asset.local_maxs[1]) && std::isfinite(asset.local_maxs[2]) &&
         asset.local_mins[0] <= asset.local_maxs[0] &&
         asset.local_mins[1] <= asset.local_maxs[1] &&
         asset.local_mins[2] <= asset.local_maxs[2];
}

[[nodiscard]] MoverTrack *FindMoverTrack(worr_event_entity_ref_v1 identity) {
  for (MoverTrack &track : moverTracks) {
    if (track.initialized && track.identity.index == identity.index &&
        track.identity.generation == identity.generation) {
      return &track;
    }
  }
  return nullptr;
}

[[nodiscard]] MoverTrack *AcquireMoverTrack(
    worr_event_entity_ref_v1 identity) {
  if (MoverTrack *existing = FindMoverTrack(identity))
    return existing;

  MoverTrack *reusable = nullptr;
  for (MoverTrack &track : moverTracks) {
    if (!track.initialized) {
      reusable = &track;
      break;
    }
    gentity_t *current = EntityFromRef(track.identity);
    if (!current || !EligibleLiveMover(current)) {
      reusable = &track;
      break;
    }
  }
  if (!reusable || !InitMoverTrack(*reusable, identity.index))
    return nullptr;
  reusable->identity = identity;
  return reusable;
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
  if (EligibleLiveMover(entity.groundEntity)) {
    worr_event_entity_ref_v1 mover{};
    if (!EntityRef(entity.groundEntity, mover))
      return false;
    pose.flags |= WORR_REWIND_POSE_HAS_MOVER;
    pose.mover = mover;
    CopyVector(pose.mover_relative_origin,
               entity.s.origin - entity.groundEntity->s.origin);
    CopyVector(pose.mover_relative_angles,
               entity.s.angles - entity.groundEntity->s.angles);
  }
  return Worr_RewindPoseValidateV1(&pose);
}

[[nodiscard]] bool BuildMoverPose(
    const gentity_t &entity, uint64_t timeUs,
    const worr_sgame_rewind_collision::Map &map,
    worr_rewind_pose_v1 &pose) {
  worr_event_entity_ref_v1 identity{};
  worr_sgame_rewind_collision::Asset asset{};
  if (!EntityRef(&entity, identity) || map.map_epoch != historyMapEpoch ||
      !ResolveMoverAsset(entity, map, asset)) {
    return false;
  }

  pose = {};
  pose.struct_size = sizeof(pose);
  pose.schema_version = WORR_REWIND_ABI_VERSION;
  pose.model_revision = WORR_REWIND_POSE_MODEL_REVISION;
  pose.flags = WORR_REWIND_POSE_LINKED;
  pose.entity = identity;
  pose.mover = AbsentEntityRef();
  pose.map_epoch = map.map_epoch;
  pose.server_tick = captureTick;
  pose.lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  pose.solid = static_cast<uint32_t>(entity.solid);
  pose.clip_flags = static_cast<uint32_t>(entity.clipMask);
  pose.collision_shape = WORR_REWIND_COLLISION_BRUSH_MODEL;
  pose.collision_asset_id = asset.handle.asset_id;
  pose.server_time_us = timeUs;
  CopyVector(pose.origin, entity.s.origin);
  CopyVector(pose.angles, entity.s.angles);
  CopyVector(pose.velocity, entity.velocity);
  std::copy(std::begin(asset.local_mins), std::end(asset.local_mins),
            std::begin(pose.mins));
  std::copy(std::begin(asset.local_maxs), std::end(asset.local_maxs),
            std::begin(pose.maxs));
  return Worr_RewindPoseValidateV1(&pose);
}

[[nodiscard]] bool HistoricalBrushAssetMatches(
    const worr_rewind_pose_v1 &pose,
    const worr_sgame_rewind_collision::Asset &asset) {
  return pose.map_epoch == asset.handle.map_epoch &&
         pose.collision_asset_id == asset.handle.asset_id &&
         pose.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL &&
         std::memcmp(pose.mins, asset.local_mins, sizeof(pose.mins)) == 0 &&
         std::memcmp(pose.maxs, asset.local_maxs, sizeof(pose.maxs)) == 0;
}

[[nodiscard]] bool BuildHistoricalBrushContext(
    const worr_rewind_scene_v1 *scene, HistoricalBrushContext &context) {
  context = {};
  if (!scene || !Worr_RewindSceneValidateV1(scene))
    return false;

  bool hasBrush = false;
  for (uint32_t index = 0; index < scene->count; ++index) {
    if (scene->slots[index].pose.collision_shape ==
        WORR_REWIND_COLLISION_BRUSH_MODEL) {
      hasBrush = true;
      break;
    }
  }
  if (!hasBrush)
    return true;

  if (!CurrentCollisionMap(context.map) ||
      context.map.map_epoch != scene->map_epoch) {
    return false;
  }

  for (uint32_t index = 0; index < scene->count; ++index) {
    const worr_rewind_pose_v1 &pose = scene->slots[index].pose;
    if (pose.collision_shape != WORR_REWIND_COLLISION_BRUSH_MODEL)
      continue;
    if (context.count == context.entries.size())
      return false;

    gentity_t *current = EntityFromRef(pose.entity);
    worr_sgame_rewind_collision::Asset asset{};
    if (!current || !EligibleLiveMover(current) ||
        !ResolveMoverAsset(*current, context.map, asset) ||
        !HistoricalBrushAssetMatches(pose, asset)) {
      return false;
    }

    HistoricalBrushEntry &entry = context.entries[context.count++];
    entry.identity = pose.entity;
    entry.current = current;
    entry.asset = asset;
  }
  return true;
}

[[nodiscard]] const HistoricalBrushEntry *FindHistoricalBrushEntry(
    const HistoricalBrushContext &context,
    worr_event_entity_ref_v1 identity) {
  for (uint32_t index = 0; index < context.count; ++index) {
    const HistoricalBrushEntry &entry = context.entries[index];
    if (entry.identity.index == identity.index &&
        entry.identity.generation == identity.generation) {
      return &entry;
    }
  }
  return nullptr;
}

[[nodiscard]] bool HistoricalBrushExcludesCurrent(
    const HistoricalBrushContext *context, const gentity_t *entity) {
  if (!context || !entity)
    return false;
  for (uint32_t index = 0; index < context->count; ++index) {
    if (context->entries[index].current == entity)
      return true;
  }
  return false;
}

[[nodiscard]] bool PrepareCaptureTime(uint64_t timeUs) {
  if (lastCaptureTimeUs == std::numeric_limits<uint64_t>::max() ||
      timeUs > lastCaptureTimeUs) {
    if (captureTick == std::numeric_limits<uint32_t>::max()) {
      ++diagnostics.appendRejected;
      return false;
    }
    ++captureTick;
    lastCaptureTimeUs = timeUs;
    return true;
  }
  if (timeUs < lastCaptureTimeUs) {
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
  return true;
}

[[nodiscard]] bool EligibleLivePlayer(const gentity_t *entity) {
  return entity && entity->inUse && entity->client && entity->linked &&
         entity->solid != SOLID_NOT && entity->takeDamage && !entity->deadFlag;
}

[[nodiscard]] bool CaptureSceneRoster(
    std::array<worr_event_entity_ref_v1, kSceneCandidateCapacity> &roster,
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

  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *current = &g_entities[index];
    if (!EligibleLiveMover(current))
      continue;
    worr_event_entity_ref_v1 identity{};
    if (!EntityRef(current, identity) || !FindMoverTrack(identity) ||
        rosterCount == roster.size()) {
      return false;
    }
    roster[rosterCount++] = identity;
    ++diagnostics.moverSceneCandidates;
  }
  return true;
}

[[nodiscard]] bool SameEligibleRoster(
    const FrozenSceneCache &cache,
    const std::array<worr_event_entity_ref_v1, kSceneCandidateCapacity>
        &roster,
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

enum CanonicalSceneRejection : uint32_t {
  CANONICAL_SCENE_REJECTION_NONE = 0,
  CANONICAL_SCENE_REJECTION_ROSTER = 1,
  CANONICAL_SCENE_REJECTION_INIT = 2,
  CANONICAL_SCENE_REJECTION_CLIENT_INDEX = 3,
  CANONICAL_SCENE_REJECTION_MOVER_TRACK = 4,
  CANONICAL_SCENE_REJECTION_HISTORY_QUERY = 5,
  CANONICAL_SCENE_REJECTION_HISTORY_MISS = 6,
  CANONICAL_SCENE_REJECTION_ADD_RESULT = 7,
  CANONICAL_SCENE_REJECTION_SEAL = 8,
};

[[nodiscard]] const worr_rewind_scene_v1 *CanonicalScene(
    std::size_t shooterIndex, const worr_rewind_policy_decision_v1 &decision,
    uint32_t *rejection = nullptr) {
  if (rejection)
    *rejection = CANONICAL_SCENE_REJECTION_NONE;
  const auto reject = [rejection](uint32_t reason) {
    if (rejection)
      *rejection = reason;
    return static_cast<const worr_rewind_scene_v1 *>(nullptr);
  };
  FrozenSceneCache &cache = sceneCaches[shooterIndex];
  std::array<worr_event_entity_ref_v1, kSceneCandidateCapacity>
      eligibleRoster{};
  uint32_t eligibleRosterCount = 0;
  if (!CaptureSceneRoster(eligibleRoster, eligibleRosterCount)) {
    ++diagnostics.missingHistory;
    ++diagnostics.scenesRejected;
    return reject(CANONICAL_SCENE_REJECTION_ROSTER);
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
    return reject(CANONICAL_SCENE_REJECTION_INIT);
  }

  for (uint32_t rosterIndex = 0; rosterIndex < eligibleRosterCount;
       ++rosterIndex) {
    const worr_event_entity_ref_v1 identity = eligibleRoster[rosterIndex];
    worr_rewind_history_v1 *history = nullptr;
    if (identity.index >= 1u &&
        identity.index <= static_cast<uint32_t>(game.maxClients)) {
      const std::size_t index =
          static_cast<std::size_t>(identity.index - 1u);
      if (index >= poseTracks.size()) {
        ++diagnostics.scenesRejected;
        return reject(CANONICAL_SCENE_REJECTION_CLIENT_INDEX);
      }
      history = &poseTracks[index].history;
    } else {
      MoverTrack *track = FindMoverTrack(identity);
      if (!track) {
        ++diagnostics.missingHistory;
        ++diagnostics.scenesRejected;
        return reject(CANONICAL_SCENE_REJECTION_MOVER_TRACK);
      }
      history = &track->history;
    }

    worr_rewind_pose_query_v1 query{};
    query.struct_size = sizeof(query);
    query.schema_version = WORR_REWIND_ABI_VERSION;
    query.entity = identity;
    query.map_epoch = decision.snapshot_id.epoch;
    query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
    query.target_time_us = decision.applied_time_us;
    worr_rewind_pose_result_v1 result{};
    if (!Worr_RewindHistoryQueryV1(history, &query, &result)) {
      ++diagnostics.scenesRejected;
      return reject(CANONICAL_SCENE_REJECTION_HISTORY_QUERY);
    }
    if (!result.found) {
      ++diagnostics.missingHistory;
      ++diagnostics.scenesRejected;
      return reject(CANONICAL_SCENE_REJECTION_HISTORY_MISS);
    }
    const uint32_t requiredFlags =
        result.pose.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL
            ? WORR_REWIND_POSE_LINKED
            : (WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE);
    if ((result.pose.flags & requiredFlags) != requiredFlags) {
      continue;
    }
    if (!Worr_RewindSceneAddResultV1(&cache.scene, &result)) {
      ++diagnostics.scenesRejected;
      return reject(CANONICAL_SCENE_REJECTION_ADD_RESULT);
    }
  }
  if (!Worr_RewindSceneSealV1(&cache.scene)) {
    ++diagnostics.scenesRejected;
    return reject(CANONICAL_SCENE_REJECTION_SEAL);
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
                          const HistoricalBrushContext *historicalBrushes,
                          const TraceBounds &bounds) {
  trace_t result = ClipEntity(world, start, mins, maxs, end, contentMask);
  if (result.fraction == 0.0f)
    return result;

  const std::size_t candidateCount = gi.BoxEntities(
      bounds.mins, bounds.maxs, traceCandidates.data(), traceCandidates.size(),
      AREA_SOLID, nullptr, nullptr);
  for (std::size_t i = 0; i < candidateCount && !result.allSolid; ++i) {
    gentity_t *entity = traceCandidates[i];
    if (HistoricalBrushExcludesCurrent(historicalBrushes, entity))
      continue;
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

// The engine receives only immutable asset identity and the transform copied
// into the sealed scene. The current mover is used solely to attach the
// resulting trace to a generation-validated entity; it is never moved,
// unlinked, or otherwise used as collision input.
[[nodiscard]] bool ClipHistoricalBrushPose(
    trace_t &result, const worr_rewind_pose_v1 &pose,
    const HistoricalBrushContext &context, const Vector3 &start,
    const Vector3 *mins, const Vector3 *maxs, const Vector3 &end,
    contents_t contentMask) {
  if (result.allSolid ||
      (pose.flags & WORR_REWIND_POSE_LINKED) == 0 ||
      pose.collision_shape != WORR_REWIND_COLLISION_BRUSH_MODEL) {
    return false;
  }

  const HistoricalBrushEntry *entry =
      FindHistoricalBrushEntry(context, pose.entity);
  if (!entry || !entry->current || EntityFromRef(pose.entity) != entry->current ||
      !HistoricalBrushAssetMatches(pose, entry->asset) ||
      !rewindCollisionImport || !rewindCollisionImport->TraceTransformed) {
    return false;
  }

  worr_sgame_rewind_collision::TraceRequest request{};
  request.struct_size = sizeof(request);
  request.schema_version = worr_sgame_rewind_collision::kSchemaVersion;
  request.asset = entry->asset.handle;
  request.contents_mask = static_cast<uint32_t>(contentMask);
  CopyVector(request.start, start);
  CopyVector(request.end, end);
  if (mins)
    CopyVector(request.mins, *mins);
  if (maxs)
    CopyVector(request.maxs, *maxs);
  CopyVector(request.origin, ToVector(pose.origin));
  CopyVector(request.angles, ToVector(pose.angles));

  trace_t clipped{};
  if (!rewindCollisionImport->TraceTransformed(&request, &clipped))
    return false;
  clipped.ent = entry->current;
  MergeTrace(result, clipped, entry->current);
  ++diagnostics.historicalBrushes;
  return true;
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
                           ignoredEntities, ignoredEntityCount, false, nullptr,
                           bounds);
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

// A query guard covers every live collision participant that this module may
// replace with a sealed historical pose. The before/after fingerprint is
// diagnostic evidence that a lag-compensated trace did not mutate an edict.
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
    HashObservationValue(hash, entity.s.angles);
    HashObservationValue(hash, entity.velocity);
    HashObservationValue(hash, entity.mins);
    HashObservationValue(hash, entity.maxs);
    worr_event_entity_ref_v1 groundMover{};
    if (EligibleLiveMover(entity.groundEntity))
      (void)EntityRef(entity.groundEntity, groundMover);
    HashObservationValue(hash, groundMover);
  }

  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  for (std::size_t index = firstMover; index < total; ++index) {
    const gentity_t &entity = g_entities[index];
    if (!EligibleLiveMover(&entity))
      continue;
    HashObservationValue(hash, entity.inUse);
    HashObservationValue(hash, entity.linked);
    HashObservationValue(hash, entity.s.number);
    HashObservationValue(hash, entity.spawn_count);
    HashObservationValue(hash, entity.solid);
    HashObservationValue(hash, entity.clipMask);
    HashObservationValue(hash, entity.svFlags);
    HashObservationValue(hash, entity.moveType);
    HashObservationValue(hash, entity.s.modelIndex);
    HashObservationValue(hash, entity.s.origin);
    HashObservationValue(hash, entity.s.angles);
    HashObservationValue(hash, entity.velocity);
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
  // A canonical scene can legitimately terminate on a historical brush mover.
  // Keep that distinct from an unblocked historical query in telemetry without
  // labeling unrelated live baseline entities as historical hits.
  bool historicalBrushHit = false;
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
            result.ent && result.fraction < 1.0f &&
                    (result.ent->client || historicalBrushHit)
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
    const bool observationAppended = Worr_RewindObservationJournalAppendV1(
        &observationJournal, &observation, &sequence);
    if (observationAppended)
      CanonicalRailProbeObserveTrace(fromPlayer, observation);
    if (observationAppended && sg_lag_compensation_debug->integer >= 3) {
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
  const std::size_t shooterIndex = ClientIndex(fromPlayer);
  const worr_rewind_scene_v1 *scene = nullptr;
  HistoricalBrushContext historicalBrushes{};
  if (target.canonical) {
    uint32_t sceneRejection = CANONICAL_SCENE_REJECTION_NONE;
    scene = CanonicalScene(shooterIndex, target.decision, &sceneRejection);
    if (!scene) {
      // Policy accepted, but a sealed immutable scene could not be built.
      // Use an uncompensated collision query; do not use the legacy estimate.
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_SCENE_REJECTED;
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
        observation.query_reason = sceneRejection;
      }
      return finishObservation(NativeTrace(
          start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
          ignoredEntityCount));
    }
    if (!BuildHistoricalBrushContext(scene, historicalBrushes)) {
      ++diagnostics.scenesRejected;
      ++diagnostics.historicalBrushRejected;
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
  }

  trace_t result = TraceCurrentScene(
      start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
      ignoredEntityCount, true,
      target.canonical ? &historicalBrushes : nullptr, bounds);
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

  if (target.canonical) {
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
      if (candidate.pose.collision_shape ==
          WORR_REWIND_COLLISION_BRUSH_MODEL) {
        if (!ClipHistoricalBrushPose(result, candidate.pose,
                                     historicalBrushes, start, mins, maxs,
                                     end, contentMask)) {
          ++diagnostics.scenesRejected;
          ++diagnostics.historicalBrushRejected;
          if (observe) {
            observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
            observation.fallback_reason =
                WORR_REWIND_OBSERVATION_FALLBACK_SCENE_REJECTED;
            observation.outcome =
                WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
          }
          return finishObservation(NativeTrace(
              start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
              ignoredEntityCount));
        }
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

  if (target.canonical && result.ent && result.fraction < 1.0f &&
      HistoricalBrushExcludesCurrent(&historicalBrushes, result.ent)) {
    historicalBrushHit = true;
  }

  MaybeReportDiagnostics();
  return finishObservation(result);
}

[[nodiscard]] bool CanonicalRailProbeTerminal() {
  return canonicalRailProbe.stage == CanonicalRailProbeStage::Passed ||
         canonicalRailProbe.stage == CanonicalRailProbeStage::Failed;
}

[[nodiscard]] bool CanonicalRailProbeActive() {
  return canonicalRailProbe.stage != CanonicalRailProbeStage::Idle &&
         !CanonicalRailProbeTerminal();
}

void CanonicalRailProbePublish() {
  if (!sg_worr_rewind_canonical_rail_damage_status ||
      !sg_worr_rewind_canonical_machinegun_damage_status ||
      !sg_worr_rewind_canonical_chaingun_damage_status ||
      !sg_worr_rewind_canonical_super_shotgun_damage_status ||
      !sg_worr_rewind_canonical_disruptor_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_held_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_sustained_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_release_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_held_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_sustained_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_release_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_discharge_damage_status ||
      !sg_worr_rewind_canonical_shotgun_damage_status)
    return;
  const char *status = "idle";
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::Passed)
    status = "pass";
  else if (canonicalRailProbe.stage == CanonicalRailProbeStage::Failed)
    status = "fail";
  else if (CanonicalRailProbeActive())
    status = "pending";
  const uint32_t armed = canonicalRailProbe.stage != CanonicalRailProbeStage::Idle;
  const uint32_t damageApplied =
      canonicalRailProbe.weapon_damage == canonicalRailProbe.expected_damage;
  char value[256]{};
  std::snprintf(
      value, sizeof(value),
      "%s:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%llu:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%llu:%llu:%llu:%llu:%llu:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u", status, armed,
      canonicalRailProbe.players_ready ? 1u : 0u,
      canonicalRailProbe.history_ready ? 1u : 0u,
      canonicalRailProbe.canonical_scope ? 1u : 0u,
      canonicalRailProbe.attack_received ? 1u : 0u,
      canonicalRailProbe.weapon_callback ? 1u : 0u,
      canonicalRailProbe.canonical_historical_hit ? 1u : 0u, damageApplied,
      canonicalRailProbe.current_geometry_unchanged ? 1u : 0u,
      canonicalRailProbe.target_history_captures,
      static_cast<unsigned long long>(canonicalRailProbe.applied_age_us),
      canonicalRailProbe.failure, canonicalRailProbe.eligible_candidates,
      canonicalRailProbe.playing_candidates, canonicalRailProbe.observation_path,
      canonicalRailProbe.observation_outcome,
      canonicalRailProbe.observation_fallback,
      canonicalRailProbe.observation_flags,
      canonicalRailProbe.observation_query,
      canonicalRailProbe.observation_snapshot_epoch,
      canonicalRailProbe.history_epoch,
      canonicalRailProbe.target_history_count,
      static_cast<unsigned long long>(canonicalRailProbe.observation_applied_time_us),
      static_cast<unsigned long long>(canonicalRailProbe.latest_capture_time_us),
      static_cast<unsigned long long>(canonicalRailProbe.trace_current_time_us),
      static_cast<unsigned long long>(canonicalRailProbe.context_snapshot_time_us),
      static_cast<unsigned long long>(canonicalRailProbe.context_mapped_time_us),
      canonicalRailProbe.target_capture_prepares,
      canonicalRailProbe.capture_append_rejections,
      canonicalRailProbe.target_capture_callbacks,
      canonicalRailProbe.weapon_policy,
      canonicalRailProbe.expected_damage,
      canonicalRailProbe.weapon_damage,
      canonicalRailProbe.water_retrace_required ? 1u : 0u,
      canonicalRailProbe.water_retrace_observed ? 1u : 0u,
      canonicalRailProbe.thunderbolt_discharge_required ? 1u : 0u,
      canonicalRailProbe.thunderbolt_discharge_ammo_drained ? 1u : 0u,
      canonicalRailProbe.thunderbolt_discharge_observed ? 1u : 0u,
      canonicalRailProbe.sustained_hold_required ? 1u : 0u,
      canonicalRailProbe.sustained_hold_interrupted ? 1u : 0u);
  gi.cvarForceSet("sg_worr_rewind_canonical_rail_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_machinegun_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_chaingun_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_super_shotgun_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_disruptor_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_held_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_sustained_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_release_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_held_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_sustained_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_release_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_discharge_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_shotgun_damage_status", value);
}

void CanonicalRailProbeFail(uint32_t failure) {
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::Passed ||
      canonicalRailProbe.stage == CanonicalRailProbeStage::Failed) {
    return;
  }
  canonicalRailProbe.failure = failure;
  canonicalRailProbe.stage = CanonicalRailProbeStage::Failed;
  CanonicalRailProbePublish();
}

[[nodiscard]] bool CanonicalRailProbeSameEntity(
    const gentity_t *entity, worr_event_entity_ref_v1 expected) {
  worr_event_entity_ref_v1 actual{};
  return entity && expected.index != 0 && EntityRef(entity, actual) &&
         actual.index == expected.index && actual.generation == expected.generation;
}

[[nodiscard]] bool CanonicalRailProbeSameReference(
    worr_event_entity_ref_v1 actual, worr_event_entity_ref_v1 expected) {
  return expected.index != 0 && actual.index == expected.index &&
         actual.generation == expected.generation;
}

void CanonicalRailProbePlacePlayer(gentity_t *entity, const Vector3 &origin) {
  if (!entity || !entity->client)
    return;
  entity->s.origin = origin;
  entity->s.modelIndex = MODELINDEX_PLAYER;
  entity->solid = SOLID_BBOX;
  entity->takeDamage = true;
  entity->deadFlag = false;
  entity->health = kCanonicalRailProbeTargetHealth;
  entity->maxHealth = kCanonicalRailProbeTargetHealth;
  // The headless clients may be admitted to the match one frame before their
  // normal spawn bookkeeping is observed. The fixture owns this temporary
  // player state and needs ClientEndServerFrame to perform the ordinary pose
  // capture; it still waits for a real received attack command before firing.
  entity->client->pers.spawned = true;
  // A just-admitted player can retain a spawn health-bonus decay timer. The
  // fixture owns its synthetic 1000-health target state, so retain neither the
  // bonus nor its later per-frame health decrement; otherwise a multi-round
  // weapon can appear to deal one extra point after a correct callback.
  entity->client->pers.healthBonus = 0;
  entity->velocity = {};
  entity->groundEntity = nullptr;
  entity->gravity = 0.0f;
  entity->client->ps.pmove.origin = origin;
  entity->client->ps.pmove.velocity = {};
  entity->client->old_pmove.origin = origin;
  entity->client->old_pmove.velocity = {};
  gi.linkEntity(entity);
}

// Keep a fixture target at its selected live pose after real weapon damage.
// Unlike CanonicalRailProbePlacePlayer this deliberately preserves health and
// every combat outcome: it only clears movement introduced by normal damage
// knockback, so the next ordinary repeating-beam tick queries the same target
// rather than a target displaced out of the bounded acceptance ray.
void CanonicalRailProbePinTarget(gentity_t *entity, const Vector3 &origin) {
  if (!entity || !entity->client)
    return;
  entity->s.origin = origin;
  entity->velocity = {};
  entity->client->ps.pmove.origin = origin;
  entity->client->ps.pmove.velocity = {};
  entity->client->old_pmove.origin = origin;
  entity->client->old_pmove.velocity = {};
  gi.linkEntity(entity);
}

[[nodiscard]] bool CanonicalRailProbeSelectPlayers() {
  if (canonicalRailProbe.shooter || canonicalRailProbe.target) {
    if (!CanonicalRailProbeSameEntity(canonicalRailProbe.shooter,
                                      canonicalRailProbe.shooter_identity) ||
        !CanonicalRailProbeSameEntity(canonicalRailProbe.target,
                                      canonicalRailProbe.target_identity)) {
      CanonicalRailProbeFail(4);
      return false;
    }
    if (canonicalRailProbe.water_retrace_required &&
        !CanonicalRailProbeSameEntity(canonicalRailProbe.water,
                                      canonicalRailProbe.water_identity)) {
      CanonicalRailProbeFail(16);
      return false;
    }
    return true;
  }

  gentity_t *shooter = nullptr;
  gentity_t *target = nullptr;
  canonicalRailProbe.eligible_candidates = 0;
  canonicalRailProbe.playing_candidates = 0;
  const std::size_t clientCount = std::min<std::size_t>(
      static_cast<std::size_t>(game.maxClients), poseTracks.size());
  for (std::size_t index = 0; index < clientCount; ++index) {
    gentity_t *candidate = &g_entities[index + 1u];
    if (!candidate || !candidate->client || !candidate->inUse ||
        !ClientIsPlaying(candidate->client)) {
      continue;
    }
    ++canonicalRailProbe.playing_candidates;
    if (EligibleLivePlayer(candidate))
      ++canonicalRailProbe.eligible_candidates;
    // The acceptance gate supplies two independent real UDP clients. The
    // first supplies the canonical attack command; the second retains the
    // target history. Do not use a game bot here: it does not exercise the
    // connection and command lifecycle required by this proof.
    if (!shooter) {
      shooter = candidate;
    } else if (!target) {
      target = candidate;
    }
  }
  if (!shooter || !target || shooter == target) {
    CanonicalRailProbePublish();
    return false;
  }

  worr_event_entity_ref_v1 shooterIdentity{};
  worr_event_entity_ref_v1 targetIdentity{};
  if (!EntityRef(shooter, shooterIdentity) || !EntityRef(target, targetIdentity)) {
    CanonicalRailProbeFail(5);
    return false;
  }

  canonicalRailProbe.shooter = shooter;
  canonicalRailProbe.target = target;
  canonicalRailProbe.shooter_identity = shooterIdentity;
  canonicalRailProbe.target_identity = targetIdentity;
  if (canonicalRailProbe.water_retrace_required) {
    // The packaged collision fixture owns a real func_water brush centered at
    // the world origin. Select it by entity identity so later observations
    // prove actual water contact followed by a normal production retrace.
    const std::size_t total = std::min<std::size_t>(
        static_cast<std::size_t>(MAX_ENTITIES),
        static_cast<std::size_t>(globals.numEntities));
    for (std::size_t index = static_cast<std::size_t>(game.maxClients) + 1u;
         index < total; ++index) {
      gentity_t *candidate = &g_entities[index];
      if (!candidate->inUse || candidate->solid != SOLID_BSP ||
          !candidate->className ||
          Q_strcasecmp(candidate->className, "func_water") != 0) {
        continue;
      }
      worr_event_entity_ref_v1 waterIdentity{};
      if (EntityRef(candidate, waterIdentity)) {
        canonicalRailProbe.water = candidate;
        canonicalRailProbe.water_identity = waterIdentity;
        break;
      }
    }
    if (!canonicalRailProbe.water) {
      CanonicalRailProbeFail(16);
      return false;
    }
  }
  // Keep the target inside P_ProjectSource's close-target branch for the
  // bullet family as well as Railgun. The ordinary weapon callback still
  // computes and owns the final hitscan trace; this merely prevents the
  // fixture's off-axis muzzle offset from turning the initial projection probe
  // into a different line than the following bullet trace.
  canonicalRailProbe.shooter_origin =
      canonicalRailProbe.historical_target_origin -
      Vector3{canonicalRailProbe.target_distance, 0.0f, 0.0f};
  canonicalRailProbe.current_target_origin =
      canonicalRailProbe.historical_target_origin +
      canonicalRailProbe.current_target_offset;
  canonicalRailProbe.players_ready = true;
  canonicalRailProbe.stage = CanonicalRailProbeStage::CapturingHistory;
  CanonicalRailProbePublish();
  return true;
}

void CanonicalRailProbePrepareFrameCapture(gentity_t *entity) {
  if (!CanonicalRailProbeActive())
    return;
  if (!CanonicalRailProbeSelectPlayers())
    return;
  if ((canonicalRailProbe.stage == CanonicalRailProbeStage::CapturingHistory ||
       canonicalRailProbe.stage ==
           CanonicalRailProbeStage::WaitingForCanonicalAttack) &&
      entity == canonicalRailProbe.target) {
    ++canonicalRailProbe.target_capture_prepares;
    canonicalRailProbe.capture_append_rejections =
        static_cast<uint32_t>(std::min<uint64_t>(
            diagnostics.appendRejected, std::numeric_limits<uint32_t>::max()));
    // Every pre-fire pose is an ordinary end-frame capture at the same
    // historical location.  The actual rail trace later sees the target only
    // at current_target_origin in the live world.
    CanonicalRailProbePlacePlayer(
        canonicalRailProbe.target,
        canonicalRailProbe.historical_target_origin);
    canonicalRailProbe.target->health = kCanonicalRailProbeTargetHealth;
    canonicalRailProbe.target->client->PowerupTimer(
        PowerupTimer::SpawnProtection) = 0_ms;
    CanonicalRailProbePublish();
  }
}

void CanonicalRailProbeCaptureFrame(gentity_t *entity) {
  if (!CanonicalRailProbeActive())
    return;
  if (entity == canonicalRailProbe.target)
    ++canonicalRailProbe.target_capture_callbacks;
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::CapturingHistory &&
      entity == canonicalRailProbe.target) {
    ++canonicalRailProbe.target_history_captures;
    if (canonicalRailProbe.target_history_captures >=
        kCanonicalRailProbeRequiredHistoryCaptures) {
      canonicalRailProbe.history_ready = true;
      canonicalRailProbe.stage =
          CanonicalRailProbeStage::WaitingForCanonicalAttack;
    }
    CanonicalRailProbePublish();
    return;
  }

  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.weapon_callback) {
    return;
  }
  if (!CanonicalRailProbeSameEntity(canonicalRailProbe.target,
                                    canonicalRailProbe.target_identity)) {
    CanonicalRailProbeFail(11);
    return;
  }
  if (canonicalRailProbe.thunderbolt_discharge_required &&
      !CanonicalRailProbeSameEntity(canonicalRailProbe.shooter,
                                    canonicalRailProbe.shooter_identity)) {
    CanonicalRailProbeFail(11);
    return;
  }
  if (canonicalRailProbe.sustained_hold_required) {
    CanonicalRailProbePinTarget(canonicalRailProbe.target,
                                canonicalRailProbe.current_target_origin);
  }
  const int healthBefore = canonicalRailProbe.thunderbolt_discharge_required
                               ? canonicalRailProbe.shooter_health_before
                               : canonicalRailProbe.target_health_before;
  const int healthAfter = canonicalRailProbe.thunderbolt_discharge_required
                              ? canonicalRailProbe.shooter->health
                              : canonicalRailProbe.target->health;
  canonicalRailProbe.weapon_damage =
      healthBefore > healthAfter
          ? static_cast<uint32_t>(healthBefore - healthAfter)
          : 0u;
  const bool damageApplied =
      canonicalRailProbe.weapon_damage == canonicalRailProbe.expected_damage;
  uint64_t currentTimeUs = 0;
  const bool needsAuthoritativeTime =
      canonicalRailProbe.damage_settle_delay_us ||
      (canonicalRailProbe.release_required &&
       canonicalRailProbe.release_received);
  if (needsAuthoritativeTime && !CurrentAuthoritativeTimeUs(currentTimeUs)) {
    CanonicalRailProbeFail(13);
    return;
  }
  bool damageSettled = true;
  if (canonicalRailProbe.damage_settle_delay_us) {
    const bool deadlineReached =
        currentTimeUs >= canonicalRailProbe.damage_settle_deadline_us;
    // Delayed projectiles need their normal daemon/lifecycle window to settle
    // before measuring damage. Held beams instead publish pending progress
    // until the requested normal cadence reaches exact cumulative damage, then
    // finish immediately; the same deadline remains a fail-closed timeout.
    damageSettled = canonicalRailProbe.defer_damage_evaluation_until_deadline
                        ? deadlineReached
                        : (damageApplied || deadlineReached);
  }
  if (!damageSettled) {
    CanonicalRailProbePublish();
    return;
  }
  if (canonicalRailProbe.release_required) {
    // The real held cadence must reach its exact damage before a release may
    // complete the fixture. A missing release remains pending only until the
    // same bounded cadence deadline, then fails closed.
    if (!damageApplied) {
      CanonicalRailProbeFail(12);
      return;
    }
    if (!canonicalRailProbe.release_received) {
      if (currentTimeUs >= canonicalRailProbe.damage_settle_deadline_us) {
        CanonicalRailProbeFail(14);
        return;
      }
      CanonicalRailProbePublish();
      return;
    }
    if (currentTimeUs < canonicalRailProbe.release_grace_deadline_us) {
      CanonicalRailProbePublish();
      return;
    }
    canonicalRailProbe.release_damage_stable =
        canonicalRailProbe.weapon_damage == canonicalRailProbe.release_damage_before;
    if (!canonicalRailProbe.release_damage_stable) {
      CanonicalRailProbeFail(15);
      return;
    }
  }
  // The collision journal does not promise an entity reference for every
  // inline water-brush hit. The production weapon result is stronger and
  // unambiguous here: fire_beams/fire_thunderbolt halve damage only after the
  // real water trace, and the displaced live target can receive that exact
  // damage only through the following water-excluding historical retrace.
  if (canonicalRailProbe.water_retrace_required) {
    canonicalRailProbe.water_retrace_observed =
        damageApplied && canonicalRailProbe.canonical_historical_hit &&
        canonicalRailProbe.current_geometry_unchanged;
  }
  if (canonicalRailProbe.thunderbolt_discharge_required) {
    canonicalRailProbe.thunderbolt_discharge_ammo_drained =
        canonicalRailProbe.thunderbolt_discharge_ammo_item != IT_NULL &&
        canonicalRailProbe.shooter && canonicalRailProbe.shooter->client &&
        canonicalRailProbe.shooter->client->pers.inventory[
            canonicalRailProbe.thunderbolt_discharge_ammo_item] == 0;
  }
  const bool normalHitscanProof =
      canonicalRailProbe.canonical_scope &&
      canonicalRailProbe.attack_received && canonicalRailProbe.weapon_callback &&
      canonicalRailProbe.canonical_historical_hit && damageApplied &&
      canonicalRailProbe.current_geometry_unchanged &&
      (!canonicalRailProbe.water_retrace_required ||
       canonicalRailProbe.water_retrace_observed) &&
      (!canonicalRailProbe.release_required ||
       (canonicalRailProbe.release_received &&
        canonicalRailProbe.release_damage_stable));
  // Production discharge intentionally returns before the beam's historical
  // trace. It is a current-authority radius/self-damage effect, so require the
  // explicit production-branch observer rather than fabricating a hitscan
  // observation that the weapon did not make.
  const bool thunderboltDischargeProof =
      canonicalRailProbe.players_ready && canonicalRailProbe.history_ready &&
      canonicalRailProbe.canonical_scope && canonicalRailProbe.attack_received &&
      canonicalRailProbe.thunderbolt_discharge_observed && damageApplied &&
      canonicalRailProbe.thunderbolt_discharge_ammo_drained;
  if ((canonicalRailProbe.thunderbolt_discharge_required
           ? thunderboltDischargeProof
           : normalHitscanProof)) {
    canonicalRailProbe.stage = CanonicalRailProbeStage::Passed;
    CanonicalRailProbePublish();
  } else {
    CanonicalRailProbeFail(
        canonicalRailProbe.water_retrace_required &&
                !canonicalRailProbe.water_retrace_observed
            ? 17
            : (canonicalRailProbe.thunderbolt_discharge_required &&
                       (!canonicalRailProbe.thunderbolt_discharge_ammo_drained ||
                        !canonicalRailProbe.thunderbolt_discharge_observed)
                   ? (canonicalRailProbe.thunderbolt_discharge_ammo_drained
                          ? 19
                          : 18)
                   : 12));
  }
}

void CanonicalRailProbeObserveTrace(
    gentity_t *shooter, const worr_rewind_observation_v1 &observation) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      shooter != canonicalRailProbe.shooter ||
      observation.weapon_policy != canonicalRailProbe.weapon_policy) {
    return;
  }
  canonicalRailProbe.weapon_callback = true;
  const uint32_t requiredFlags =
      WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT |
      WORR_REWIND_OBSERVATION_POLICY_ACCEPTED |
      WORR_REWIND_OBSERVATION_HISTORICAL_QUERY |
      WORR_REWIND_OBSERVATION_HISTORICAL_SCENE |
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED |
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
  const bool historicalTargetHit =
      observation.path == WORR_REWIND_OBSERVATION_PATH_CANONICAL &&
      observation.outcome == WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT &&
      observation.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      (observation.flags & requiredFlags) == requiredFlags &&
      (observation.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) == 0 &&
      SameCommand(observation.command_id, canonicalRailProbe.command_id) &&
      observation.hit_entity.index == canonicalRailProbe.target_identity.index &&
      observation.hit_entity.generation == canonicalRailProbe.target_identity.generation &&
      observation.trace_fraction > 0.0f && observation.trace_fraction < 1.0f;
  const bool historicalWaterHit =
      canonicalRailProbe.water_retrace_required &&
      observation.path == WORR_REWIND_OBSERVATION_PATH_CANONICAL &&
      observation.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      (observation.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) == 0 &&
      SameCommand(observation.command_id, canonicalRailProbe.command_id) &&
      CanonicalRailProbeSameReference(observation.hit_entity,
                                      canonicalRailProbe.water_identity) &&
      observation.trace_fraction > 0.0f && observation.trace_fraction < 1.0f;
  if (historicalWaterHit)
    canonicalRailProbe.water_trace_observation_sequence =
        observation.observation_sequence;
  const bool currentGeometryUnchanged =
      canonicalRailProbe.target && canonicalRailProbe.target->linked &&
      canonicalRailProbe.target->s.origin ==
          canonicalRailProbe.current_target_origin;

  // A hitscan weapon can emit auxiliary collision queries before or after the
  // target-bearing damage trace. Retain only the observation that names the
  // target instead of mistaking an unrelated clear segment for a miss.
  if (historicalTargetHit) {
    canonicalRailProbe.observation_path = observation.path;
    canonicalRailProbe.observation_outcome = observation.outcome;
    canonicalRailProbe.observation_fallback = observation.fallback_reason;
    canonicalRailProbe.observation_flags = observation.flags;
    canonicalRailProbe.observation_query = observation.query_reason;
    canonicalRailProbe.observation_snapshot_epoch = observation.snapshot_id.epoch;
    canonicalRailProbe.history_epoch = historyMapEpoch;
    canonicalRailProbe.observation_applied_time_us = observation.applied_time_us;
    canonicalRailProbe.latest_capture_time_us = lastCaptureTimeUs;
    const std::size_t targetIndex = ClientIndex(canonicalRailProbe.target);
    if (targetIndex < poseTracks.size())
      canonicalRailProbe.target_history_count = poseTracks[targetIndex].history.count;
    uint64_t currentTimeUs = 0;
    if (CurrentAuthoritativeTimeUs(currentTimeUs)) {
      canonicalRailProbe.trace_current_time_us = currentTimeUs;
      if (observation.applied_time_us <= currentTimeUs)
        canonicalRailProbe.applied_age_us =
            currentTimeUs - observation.applied_time_us;
    }
    canonicalRailProbe.canonical_historical_hit = true;
    canonicalRailProbe.current_geometry_unchanged = currentGeometryUnchanged;
    canonicalRailProbe.water_retrace_observed =
        !canonicalRailProbe.water_retrace_required ||
        (canonicalRailProbe.water_trace_observation_sequence != 0 &&
         canonicalRailProbe.water_trace_observation_sequence <
             observation.observation_sequence);
  }
  CanonicalRailProbePublish();
}

void CanonicalRailProbePrepareCommand(gentity_t *entity, usercmd_t *command) {
  if (!CanonicalRailProbeActive() || !entity || !entity->client || !command ||
      !CanonicalRailProbeSelectPlayers() || entity != canonicalRailProbe.shooter ||
      (canonicalRailProbe.stage !=
           CanonicalRailProbeStage::WaitingForCanonicalAttack &&
       canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage)) {
    return;
  }
  if (!commandContextImport || !commandContextImport->GetScopeState ||
      !commandContextImport->GetCurrent) {
    CanonicalRailProbeFail(2);
    return;
  }
  const uint32_t scopeState = commandContextImport->GetScopeState();
  if (scopeState == WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY)
    return;
  if (scopeState != WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID) {
    CanonicalRailProbeFail(6);
    return;
  }
  worr_authoritative_command_context_v1 context{};
  if (!commandContextImport->GetCurrent(&context) ||
      context.struct_size != sizeof(context) ||
      context.schema_version != WORR_COMMAND_CONTEXT_API_VERSION ||
      context.client_index != ClientIndex(entity) ||
      !Worr_CommandIdValidV1(context.command.command_id, false)) {
    CanonicalRailProbeFail(7);
    return;
  }
  canonicalRailProbe.canonical_scope = true;
  canonicalRailProbe.context_snapshot_time_us =
      context.current_snapshot.server_time_us;
  canonicalRailProbe.context_mapped_time_us =
      context.mapping_proof.mapped_server_time_us;
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::AwaitingDamage) {
    if (canonicalRailProbe.sustained_hold_required &&
        (command->buttons & BUTTON_ATTACK) == 0) {
      canonicalRailProbe.sustained_hold_interrupted = true;
      CanonicalRailProbeFail(20);
      return;
    }
    // The release is meaningful only after normal held firing has reached the
    // exact requested cumulative damage. It remains a real no-attack command
    // under the same production command-context admission path.
    if (canonicalRailProbe.release_required && !canonicalRailProbe.release_received &&
        (command->buttons & BUTTON_ATTACK) == 0 &&
        canonicalRailProbe.weapon_damage == canonicalRailProbe.expected_damage) {
      uint64_t currentTimeUs = 0;
      if (!CurrentAuthoritativeTimeUs(currentTimeUs)) {
        CanonicalRailProbeFail(13);
        return;
      }
      canonicalRailProbe.release_received = true;
      canonicalRailProbe.release_damage_before = canonicalRailProbe.weapon_damage;
      canonicalRailProbe.release_grace_deadline_us =
          currentTimeUs + kCanonicalBeamReleaseGraceUs;
    }
    CanonicalRailProbePublish();
    return;
  }
  if ((command->buttons & BUTTON_ATTACK) == 0) {
    CanonicalRailProbePublish();
    return;
  }

  Item *weapon = GetItemByIndex(canonicalRailProbe.weapon_item);
  if (!weapon || !weapon->weaponThink || weapon->ammo == IT_NULL ||
      weapon->id == IT_NULL ||
      canonicalRailProbe.weapon_policy == WORR_REWIND_WEAPON_UNSPECIFIED ||
      canonicalRailProbe.expected_damage == 0) {
    CanonicalRailProbeFail(8);
    return;
  }

  // This alters only the isolated fixture's server-owned player state.  The
  // received command retains its original attack bit and the normal weapon
  // dispatcher below invokes Item::weaponThink; this function never calls a
  // weapon or trace routine directly.
  CanonicalRailProbePlacePlayer(entity, canonicalRailProbe.shooter_origin);
  CanonicalRailProbePlacePlayer(canonicalRailProbe.target,
                                canonicalRailProbe.current_target_origin);
  canonicalRailProbe.target->health = kCanonicalRailProbeTargetHealth;
  canonicalRailProbe.target->client->PowerupTimer(
      PowerupTimer::SpawnProtection) = 0_ms;
  entity->client->spawnAngleLockAngles = {};
  entity->client->spawnAngleLockUntil =
      level.time + (canonicalRailProbe.sustained_hold_required
                        ? kCanonicalBeamSustainedProbeAngleLockDuration
                        : 1_sec);
  entity->client->pers.weapon = weapon;
  entity->client->pers.inventory[weapon->id] = 1;
  entity->client->pers.inventory[weapon->ammo] = canonicalRailProbe.initial_ammo;
  if (canonicalRailProbe.thunderbolt_discharge_required) {
    entity->client->pers.ammoMax[static_cast<int>(AmmoID::Cells)] =
        static_cast<short>(std::max<int>(
            entity->client->pers.ammoMax[static_cast<int>(AmmoID::Cells)],
            kCanonicalThunderboltDischargeProbeCells));
  }
  entity->client->weapon.pending = nullptr;
  entity->client->weapon.fireFinished = level.time;
  entity->client->weapon.thinkTime = level.time;
  entity->client->weapon.fireBuffered = false;
  entity->client->weapon.thunk = false;
  entity->client->weaponState = WeaponState::Ready;
  entity->client->ps.gunFrame = canonicalRailProbe.weapon_idle_frame;
  // The fixture reacts only after observing a genuine attack bit. Reset an
  // older held-button edge so the ordinary ClientThink assignment immediately
  // below latches this same received command; no button is synthesized here.
  entity->client->buttons = BUTTON_NONE;
  entity->client->latchedButtons &= ~BUTTON_ATTACK;

  canonicalRailProbe.command_id = context.command.command_id;
  canonicalRailProbe.target_health_before = canonicalRailProbe.target->health;
  if (canonicalRailProbe.thunderbolt_discharge_required) {
    canonicalRailProbe.shooter_health_before = entity->health;
    canonicalRailProbe.thunderbolt_discharge_ammo_item = weapon->ammo;
    canonicalRailProbe.thunderbolt_discharge_observed = false;
    canonicalRailProbe.thunderbolt_discharge_ammo_drained = false;
  }
  if (canonicalRailProbe.damage_settle_delay_us) {
    uint64_t currentTimeUs = 0;
    if (!CurrentAuthoritativeTimeUs(currentTimeUs)) {
      CanonicalRailProbeFail(13);
      return;
    }
    canonicalRailProbe.damage_settle_deadline_us =
        currentTimeUs + canonicalRailProbe.damage_settle_delay_us;
  }
  canonicalRailProbe.attack_received = true;
  canonicalRailProbe.stage = CanonicalRailProbeStage::AwaitingDamage;
  CanonicalRailProbePublish();
}

bool CanonicalHitscanProbeArm(uint32_t weaponPolicy, item_id_t weaponItem,
                              uint32_t expectedDamage, int weaponIdleFrame,
                              float targetDistance, uint64_t damageSettleDelayUs,
                              bool deferDamageEvaluationUntilDeadline = false,
                              Vector3 currentTargetOffset = {0.0f, 96.0f,
                                                             0.0f},
                              bool releaseRequired = false,
                              Vector3 historicalTargetOrigin = {64.0f, 192.0f,
                                                                64.0f},
                              bool waterRetraceRequired = false,
                              bool thunderboltDischargeRequired = false,
                              int initialAmmo = 8,
                              bool sustainedHoldRequired = false) {
  if (!initialized || !deathmatch || !deathmatch->integer ||
      !g_lagCompensation || !g_lagCompensation->integer ||
      !ObservationEnabled() || !commandContextImport ||
      !commandContextImport->GetCurrent || !commandContextImport->GetScopeState) {
    canonicalRailProbe = {};
    canonicalRailProbe.stage = CanonicalRailProbeStage::Failed;
    canonicalRailProbe.failure = 1;
    CanonicalRailProbePublish();
    return false;
  }
  canonicalRailProbe = {};
  canonicalRailProbe.weapon_policy = weaponPolicy;
  canonicalRailProbe.weapon_item = weaponItem;
  canonicalRailProbe.expected_damage = expectedDamage;
  canonicalRailProbe.initial_ammo = initialAmmo;
  canonicalRailProbe.weapon_idle_frame = weaponIdleFrame;
  canonicalRailProbe.target_distance = targetDistance;
  canonicalRailProbe.damage_settle_delay_us = damageSettleDelayUs;
  canonicalRailProbe.defer_damage_evaluation_until_deadline =
      deferDamageEvaluationUntilDeadline;
  canonicalRailProbe.current_target_offset = currentTargetOffset;
  canonicalRailProbe.release_required = releaseRequired;
  canonicalRailProbe.historical_target_origin = historicalTargetOrigin;
  canonicalRailProbe.water_retrace_required = waterRetraceRequired;
  canonicalRailProbe.thunderbolt_discharge_required =
      thunderboltDischargeRequired;
  canonicalRailProbe.sustained_hold_required = sustainedHoldRequired;
  canonicalRailProbe.stage = CanonicalRailProbeStage::WaitingForPlayers;
  // The runner has already admitted the two real clients. Select and separate
  // them synchronously with the arming command so an ordinary spawn-frame
  // telefrag cannot replace a captured entity identity before the first
  // retained pose. This remains fixture setup only: no command authority,
  // trace, or weapon action is created here.
  if (CanonicalRailProbeSelectPlayers()) {
    CanonicalRailProbePlacePlayer(canonicalRailProbe.shooter,
                                  canonicalRailProbe.shooter_origin);
    CanonicalRailProbePlacePlayer(canonicalRailProbe.target,
                                  canonicalRailProbe.historical_target_origin);
  }
  CanonicalRailProbePublish();
  return canonicalRailProbe.stage != CanonicalRailProbeStage::Failed;
}

bool CanonicalRailProbeArm() {
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_RAILGUN,
                                  IT_WEAPON_RAILGUN,
                                  kCanonicalRailProbeExpectedDamage, 19, 112.0f,
                                  0);
}

bool CanonicalMachinegunProbeArm() {
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_MACHINEGUN,
                                  IT_WEAPON_MACHINEGUN,
                                  kCanonicalMachinegunProbeExpectedDamage, 6,
                                  112.0f, 0);
}

bool CanonicalChaingunProbeArm() {
  // Frame 14 advances through the normal firing callback to frame 15, where
  // the Chaingun's ordinary three-round burst is selected from the received
  // attack button. The status requires all three current-authority bullet
  // hits, rather than accepting a single-round callback observation.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_CHAINGUN,
                                  IT_WEAPON_CHAINGUN,
                                  kCanonicalChaingunProbeExpectedDamage, 14,
                                  112.0f, 0);
}

bool CanonicalSuperShotgunProbeArm() {
  // 64 units keeps both ordinary +/-5 degree barrels and their full pellet
  // patterns inside the historic player hull. This is still a normal close
  // range player shot; only the fixture's server-owned starting geometry is
  // selected here, not a trace or weapon result.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_SUPER_SHOTGUN,
                                  IT_WEAPON_SSHOTGUN,
                                  kCanonicalSuperShotgunProbeExpectedDamage,
                                  18, 64.0f, 0);
}

bool CanonicalDisruptorProbeArm() {
  // The convergence trace is canonical and immediate, but fire_disruptor
  // deliberately resolves physical projectile contact and its 500 ms pain
  // daemon later. Give normal flight and lifecycle processing 1500 ms before
  // measuring the final current-authority damage.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE,
                                  IT_WEAPON_DISRUPTOR,
                                  kCanonicalDisruptorProbeExpectedDamage, 10,
                                  112.0f, UINT64_C(1500000), true);
}

bool CanonicalPlasmaBeamProbeArm() {
  // A held real command starts the normal repeating Plasma Beam state. This
  // acceptance seam proves its first ordinary deathmatch beam tick only; it
  // intentionally does not claim a full held/release or water-retrace
  // lifecycle from a one-command fixture.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_PLASMA_BEAM,
                                  IT_WEAPON_PLASMABEAM,
                                  kCanonicalPlasmaBeamProbeExpectedDamage,
                                  13, 112.0f, 0);
}

bool CanonicalPlasmaBeamHeldProbeArm() {
  // The held real command remains active while normal repeating weapon frames
  // accumulate three 8-damage ticks. Keep the live target distinct but on the
  // aim ray so later commands query their newer history rather than correctly
  // missing the one-shot fixture's deliberately off-ray current pose. The
  // bounded window is a timeout, not a mandatory delay: completion occurs as
  // soon as the normal cadence reaches exact 24 damage.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_PLASMA_BEAM,
                                  IT_WEAPON_PLASMABEAM,
                                  kCanonicalPlasmaBeamHeldProbeExpectedDamage,
                                  13, 112.0f, UINT64_C(1500000), false,
                                  {32.0f, 0.0f, 0.0f});
}

bool CanonicalPlasmaBeamSustainedProbeArm() {
  // Hold through 32 normal 8-damage ticks. The bounded five-second deadline
  // rejects a stalled repeater, while no release is synthesized after the
  // initial real attack edge. Plasma Beam receives a one-Cell eligibility
  // reserve because it requires two Cells to begin a tick.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PLASMA_BEAM, IT_WEAPON_PLASMABEAM,
      kCanonicalPlasmaBeamSustainedProbeExpectedDamage, 13, 112.0f,
      kCanonicalBeamSustainedProbeTimeoutUs, false, {32.0f, 0.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false,
      kCanonicalBeamSustainedProbeCells, true);
}

bool CanonicalPlasmaBeamReleaseProbeArm() {
  // The release variant first reaches the normal three-tick cadence, then
  // requires a real no-attack command and a 250 ms no-extra-damage grace.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PLASMA_BEAM, IT_WEAPON_PLASMABEAM,
      kCanonicalPlasmaBeamReleaseProbeExpectedDamage, 13, 112.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f}, true);
}

bool CanonicalPlasmaBeamWaterRetraceProbeArm() {
  // The horizontal beam crosses the packaged fixture's real func_water before reaching
  // the retained historical player. Production fire_beams must observe water,
  // re-trace without it, and apply the normal halved 4 damage.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PLASMA_BEAM, IT_WEAPON_PLASMABEAM,
      kCanonicalPlasmaBeamWaterRetraceProbeExpectedDamage, 13, 224.0f, 0,
      false, {0.0f, 96.0f, 0.0f}, false, {112.0f, 0.0f, 0.0f}, true);
}

bool CanonicalThunderboltProbeArm() {
  // This is the normal dry-world first tick. The Thunderbolt implementation
  // owns its main/side-ray footprint and target de-duplication; exact 8 damage
  // rejects a fixture result that incorrectly accumulates multiple rays.
  // Underwater discharge and sustained/release lifecycle coverage are separate.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_THUNDERBOLT,
                                  IT_WEAPON_THUNDERBOLT,
                                  kCanonicalThunderboltProbeExpectedDamage,
                                  3, 112.0f, 0);
}

bool CanonicalThunderboltHeldProbeArm() {
  // fire_thunderbolt owns the repeated main/side-ray footprint and target
  // de-duplication on every ordinary held tick. Keep the distinct live target
  // on the aim ray so later command histories remain hittable. Exact 24 damage
  // proves three normal 8-damage ticks, without inferring underwater discharge
  // behavior.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltHeldProbeExpectedDamage, 3, 112.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f});
}

bool CanonicalThunderboltSustainedProbeArm() {
  // Hold through 32 normal 8-damage ticks. The normal production footprint
  // must retain its per-tick target de-duplication for the complete bounded
  // hold; no release is synthesized after attack start.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltSustainedProbeExpectedDamage, 3, 112.0f,
      kCanonicalBeamSustainedProbeTimeoutUs, false, {32.0f, 0.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false,
      kCanonicalBeamSustainedProbeCells, true);
}

bool CanonicalThunderboltReleaseProbeArm() {
  // Retain the ordinary dry-world main/side-ray and de-duplication path until
  // the real client releases; any post-release damage fails the grace check.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltReleaseProbeExpectedDamage, 3, 112.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f}, true);
}

bool CanonicalThunderboltWaterRetraceProbeArm() {
  // Keep the shooter out of water while the production main beam crosses the
  // fixture volume. Exact 4 damage verifies normal water halving; a recorded
  // water observation must precede the target retrace and side-ray
  // de-duplication still prevents duplicate target damage.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltWaterRetraceProbeExpectedDamage, 3, 224.0f, 0,
      false, {0.0f, 96.0f, 0.0f}, false, {112.0f, 0.0f, 0.0f}, true);
}

bool CanonicalThunderboltDischargeProbeArm() {
  // This is an explicit current-authority radius policy, not a hitscan rewind
  // claim. The shooter begins underwater with eight cells; the real command
  // must drain them and apply the normal 35 * 8 / 2 discharge hit followed by
  // Damage's self-damage halving (70). The retained target remains outside the
  // radius after command setup, while the command context remains the normal
  // canonical admission path.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltDischargeProbeExpectedDamage, 3, 112.0f, 0, false,
      {0.0f, 400.0f, 0.0f}, false, {112.0f, 0.0f, 0.0f}, false, true);
}

bool CanonicalShotgunProbeArm() {
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_SHOTGUN,
                                  IT_WEAPON_SHOTGUN,
                                  kCanonicalShotgunProbeExpectedDamage, 19,
                                  112.0f, 0);
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
  sg_worr_rewind_mover_selftest_status = gi.cvar(
      "sg_worr_rewind_mover_selftest_status", "idle", CVAR_NOSET);
  sg_worr_rewind_rail_damage_selftest_status = gi.cvar(
      "sg_worr_rewind_rail_damage_selftest_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_rail_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_rail_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_machinegun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_machinegun_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_chaingun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_chaingun_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_super_shotgun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_super_shotgun_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_disruptor_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_disruptor_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_held_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_held_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_sustained_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_sustained_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_release_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_release_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status",
      "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_held_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_held_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_sustained_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_sustained_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_release_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_release_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status",
      "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_discharge_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_discharge_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_shotgun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_shotgun_damage_status", "idle", CVAR_NOSET);

  commandContextImport = nullptr;
  rewindCollisionImport = nullptr;
  if (gi.GetExtension) {
    const auto *candidate =
        static_cast<const worr_command_context_import_v1 *>(
            gi.GetExtension(WORR_COMMAND_CONTEXT_IMPORT_V1));
    if (candidate && candidate->struct_size == sizeof(*candidate) &&
        candidate->api_version == WORR_COMMAND_CONTEXT_API_VERSION &&
        candidate->GetCurrent && candidate->GetScopeState) {
      commandContextImport = candidate;
    }

    const auto *collisionCandidate =
        static_cast<const worr_sgame_rewind_collision::Import *>(
            gi.GetExtension(worr_sgame_rewind_collision::kImportName));
    if (collisionCandidate &&
        collisionCandidate->struct_size == sizeof(*collisionCandidate) &&
        collisionCandidate->api_version ==
            worr_sgame_rewind_collision::kApiVersion &&
        collisionCandidate->GetMapIdentity &&
        collisionCandidate->ResolveInlineBrush &&
        collisionCandidate->TraceTransformed) {
      rewindCollisionImport = collisionCandidate;
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
  rewindCollisionImport = nullptr;
  canonicalRailProbe = {};
  // Init()/ResetMap() recreate all bounded state before it can be used again.
  // Do not aggregate-assign those large static arrays here: MSVC lowers that
  // assignment through a stack temporary and can overflow the game thread.
  InvalidateDecisionCaches();
  InvalidateSceneCaches();
  initialized = false;
}

void LagCompensation_ResetMap() {
  if (!initialized)
    return;
  canonicalRailProbe = {};
  CanonicalRailProbePublish();
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
  decisionCaches[index].valid = false;
  sceneCaches[index].valid = false;
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
  decisionCaches[index].valid = false;
  sceneCaches[index].valid = false;
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

  // The headless canonical rail fixture stages its target before the ordinary
  // end-frame pose capture.  Normal operation is an exact no-op here.
  CanonicalRailProbePrepareFrameCapture(entity);

  uint64_t timeUs = 0;
  if (!CompletedAuthoritativeFrameTimeUs(timeUs)) {
    ++diagnostics.appendRejected;
    return;
  }
  if (!PrepareCaptureTime(timeUs))
    return;

  PoseTrack &track = poseTracks[index];
  // Map/client lifecycle callbacks normally initialize every track, but a
  // newly admitted client can reach its first end-frame during a reset handoff.
  // Recover the empty bounded store here instead of silently omitting that
  // authoritative pose (the history is still empty and never aliases a life).
  if (!track.initialized &&
      !InitTrack(track, static_cast<uint32_t>(index + 1u))) {
    ++diagnostics.appendRejected;
    return;
  }
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
    if (entity == canonicalRailProbe.target)
      canonicalRailProbe.capture_append_rejections =
          static_cast<uint32_t>(std::min<uint64_t>(
              diagnostics.appendRejected, std::numeric_limits<uint32_t>::max()));
    return;
  }
  RecordLegacyFrame(track, gi.ServerFrame(), timeUs);
  CanonicalRailProbeCaptureFrame(entity);
}

void LagCompensation_RecordMovers() {
  if (!initialized)
    return;

  uint64_t timeUs = 0;
  if (!CompletedAuthoritativeFrameTimeUs(timeUs)) {
    ++diagnostics.moverCaptureRejected;
    return;
  }
  if (!PrepareCaptureTime(timeUs)) {
    ++diagnostics.moverCaptureRejected;
    return;
  }

  worr_sgame_rewind_collision::Map map{};
  if (!CurrentCollisionMap(map) || !AdoptAuthoritativeMapEpoch(map.map_epoch)) {
    ++diagnostics.moverCaptureRejected;
    return;
  }

  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *entity = &g_entities[index];
    if (!EligibleLiveMover(entity))
      continue;

    worr_event_entity_ref_v1 identity{};
    if (!EntityRef(entity, identity)) {
      ++diagnostics.moverCaptureRejected;
      continue;
    }
    MoverTrack *track = AcquireMoverTrack(identity);
    if (!track) {
      ++diagnostics.moverTrackExhausted;
      continue;
    }
    if (track->history.count) {
      const uint32_t newestIndex = static_cast<uint32_t>(
          (static_cast<uint64_t>(track->history.head) + track->history.count -
           1u) %
          track->history.capacity);
      const worr_rewind_pose_v1 &newest = track->history.slots[newestIndex];
      if (newest.server_time_us == timeUs &&
          newest.server_tick == captureTick) {
        continue;
      }
    }

    worr_rewind_pose_v1 pose{};
    if (!BuildMoverPose(*entity, timeUs, map, pose)) {
      ++diagnostics.moverCaptureRejected;
      continue;
    }
    uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
    if (!Worr_RewindHistoryAppendV1(&track->history, &pose, &reason) ||
        reason != WORR_REWIND_APPEND_ACCEPTED) {
      ++diagnostics.moverCaptureRejected;
      continue;
    }
    ++diagnostics.moversCaptured;
  }
}

[[nodiscard]] bool RuntimeRiderFrameContinuity(
    const PoseTrack &riderTrack, const MoverTrack &moverTrack,
    worr_event_entity_ref_v1 moverIdentity, uint32_t &sampleCount,
    uint64_t &latestCaptureTimeUs) {
  sampleCount = 0;
  latestCaptureTimeUs = 0;
  if (!riderTrack.initialized || !moverTrack.initialized ||
      !Worr_RewindHistoryValidateV1(&riderTrack.history) ||
      !Worr_RewindHistoryValidateV1(&moverTrack.history)) {
    return false;
  }

  bool haveFirstSample = false;
  bool moverRotated = false;
  bool riderMoved = false;
  float firstMoverAngles[3]{};
  float firstRiderOrigin[3]{};
  for (uint32_t riderOffset = 0; riderOffset < riderTrack.history.count;
       ++riderOffset) {
    const uint32_t riderSlot = static_cast<uint32_t>(
        (static_cast<uint64_t>(riderTrack.history.head) + riderOffset) %
        riderTrack.history.capacity);
    const worr_rewind_pose_v1 &riderPose =
        riderTrack.history.slots[riderSlot];
    if ((riderPose.flags & WORR_REWIND_POSE_HAS_MOVER) == 0 ||
        riderPose.mover.index != moverIdentity.index ||
        riderPose.mover.generation != moverIdentity.generation) {
      continue;
    }

    const worr_rewind_pose_v1 *moverPose = nullptr;
    for (uint32_t moverOffset = 0; moverOffset < moverTrack.history.count;
         ++moverOffset) {
      const uint32_t moverSlot = static_cast<uint32_t>(
          (static_cast<uint64_t>(moverTrack.history.head) + moverOffset) %
          moverTrack.history.capacity);
      const worr_rewind_pose_v1 &candidate =
          moverTrack.history.slots[moverSlot];
      if (candidate.entity.index == moverIdentity.index &&
          candidate.entity.generation == moverIdentity.generation &&
          candidate.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL &&
          candidate.server_time_us == riderPose.server_time_us &&
          candidate.server_tick == riderPose.server_tick) {
        moverPose = &candidate;
        break;
      }
    }
    if (!moverPose)
      continue;

    float expectedRelativeOrigin[3]{};
    float expectedRelativeAngles[3]{};
    for (std::size_t component = 0; component < 3; ++component) {
      expectedRelativeOrigin[component] =
          riderPose.origin[component] - moverPose->origin[component];
      expectedRelativeAngles[component] =
          riderPose.angles[component] - moverPose->angles[component];
    }
    if (std::memcmp(riderPose.mover_relative_origin, expectedRelativeOrigin,
                    sizeof(expectedRelativeOrigin)) != 0 ||
        std::memcmp(riderPose.mover_relative_angles, expectedRelativeAngles,
                    sizeof(expectedRelativeAngles)) != 0) {
      return false;
    }

    if (!haveFirstSample) {
      std::memcpy(firstMoverAngles, moverPose->angles,
                  sizeof(firstMoverAngles));
      std::memcpy(firstRiderOrigin, riderPose.origin, sizeof(firstRiderOrigin));
      haveFirstSample = true;
    } else {
      moverRotated |= std::memcmp(firstMoverAngles, moverPose->angles,
                                  sizeof(firstMoverAngles)) != 0;
      riderMoved |= std::memcmp(firstRiderOrigin, riderPose.origin,
                                sizeof(firstRiderOrigin)) != 0;
    }
    ++sampleCount;
    latestCaptureTimeUs = riderPose.server_time_us;
  }
  return sampleCount >= 2 && moverRotated && riderMoved;
}

bool LagCompensation_ArmHistoricalBrushRuntimeProbe() {
  if (!initialized)
    return false;

  worr_sgame_rewind_collision::Map map{};
  if (!CurrentCollisionMap(map))
    return false;
  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  gentity_t *mover = nullptr;
  worr_sgame_rewind_collision::Asset asset{};
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *candidate = &g_entities[index];
    worr_event_entity_ref_v1 identity{};
    worr_sgame_rewind_collision::Asset candidateAsset{};
    if (!EligibleLiveMover(candidate) || !EntityRef(candidate, identity) ||
        !ResolveMoverAsset(*candidate, map, candidateAsset) ||
        candidateAsset.local_maxs[0] - candidateAsset.local_mins[0] < 16.0f ||
        candidateAsset.local_maxs[1] - candidateAsset.local_mins[1] <=
            candidateAsset.local_maxs[0] - candidateAsset.local_mins[0] +
                4.0f ||
        candidate->aVelocity.length() == 0.0f) {
      continue;
    }
    mover = candidate;
    asset = candidateAsset;
    break;
  }
  if (!mover)
    return false;

  gentity_t *rider = nullptr;
  for (std::size_t index = 1;
       index <= static_cast<std::size_t>(game.maxClients); ++index) {
    gentity_t *candidate = &g_entities[index];
    if (EligibleLivePlayer(candidate)) {
      rider = candidate;
      break;
    }
  }
  if (!rider)
    return false;

  Vector3 forward, right, up;
  AngleVectors(mover->s.angles, forward, right, up);
  const float localXHalf =
      (asset.local_maxs[0] - asset.local_mins[0]) * 0.5f;
  const float riderOffset = std::min(8.0f, localXHalf * 0.5f);
  rider->groundEntity = mover;
  rider->s.origin = mover->s.origin + forward * riderOffset;
  rider->s.origin.z += asset.local_maxs[2] - rider->mins.z;
  rider->s.angles = mover->s.angles;
  rider->s.angles[YAW] += 15.0f;
  rider->velocity = {};
  gi.linkEntity(rider);
  return true;
}

bool LagCompensation_RunHistoricalBrushRuntimeProbe(
    LagCompensationHistoricalBrushRuntimeProbe *probe) {
  if (!probe)
    return false;
  *probe = {};
  struct PublishStatus {
    LagCompensationHistoricalBrushRuntimeProbe *probe;
    bool passed = false;
    uint32_t failure = 0;

    ~PublishStatus() {
      if (!probe || !sg_worr_rewind_mover_selftest_status)
        return;
      char value[128]{};
      const auto scaleFraction = [](float fraction) {
        return static_cast<uint32_t>(std::clamp(fraction, 0.0f, 1.0f) *
                                     1000000.0f + 0.5f);
      };
      std::snprintf(
          value, sizeof(value),
          "%s:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u",
          passed ? "pass" : "fail", probe->setup_ready ? 1u : 0u,
          probe->scene_ready ? 1u : 0u, probe->rotation_applied ? 1u : 0u,
          probe->rider_setup ? 1u : 0u,
          probe->rider_frame_continuity ? 1u : 0u,
          probe->rider_frame_scene_sealed ? 1u : 0u,
          probe->rider_provenance_sealed ? 1u : 0u,
          probe->fixture_reference_blocked ? 1u : 0u,
          probe->rotation_control_unblocked ? 1u : 0u,
          probe->baseline_unblocked ? 1u : 0u,
          probe->historical_dispatched ? 1u : 0u,
          probe->historical_blocked ? 1u : 0u,
          probe->authority_unchanged ? 1u : 0u, probe->candidate_count,
          probe->rider_continuity_samples,
          scaleFraction(probe->baseline_fraction),
          scaleFraction(probe->historical_fraction), failure);
      gi.cvarForceSet("sg_worr_rewind_mover_selftest_status", value);
    }
  } status{probe};
  if (!initialized) {
    status.failure = 1;
    return false;
  }

  // The probe runs only through an explicit `sv` command on a dedicated
  // fixture map. It still uses the production game entities, provider import,
  // scene builder, baseline, and transformed trace path.
  worr_sgame_rewind_collision::Map map{};
  if (!rewindCollisionImport) {
    status.failure = 2;
    return false;
  }
  if (rewindCollisionImport->struct_size != sizeof(*rewindCollisionImport) ||
      rewindCollisionImport->api_version !=
          worr_sgame_rewind_collision::kApiVersion ||
      !rewindCollisionImport->GetMapIdentity ||
      !rewindCollisionImport->ResolveInlineBrush ||
      !rewindCollisionImport->TraceTransformed) {
    status.failure = 3;
    return false;
  }
  if (!rewindCollisionImport->GetMapIdentity(&map)) {
    status.failure = 4;
    return false;
  }
  if (map.struct_size != sizeof(map) ||
      map.schema_version != worr_sgame_rewind_collision::kSchemaVersion ||
      map.map_epoch == 0 || map.inline_model_count == 0 ||
      map.reserved0 != 0) {
    status.failure = 5;
    return false;
  }

  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  gentity_t *mover = nullptr;
  worr_event_entity_ref_v1 identity{};
  worr_sgame_rewind_collision::Asset asset{};
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *candidate = &g_entities[index];
    worr_event_entity_ref_v1 candidateIdentity{};
    worr_sgame_rewind_collision::Asset candidateAsset{};
    if (!EligibleLiveMover(candidate) || !EntityRef(candidate, candidateIdentity) ||
        !ResolveMoverAsset(*candidate, map, candidateAsset) ||
        candidateAsset.local_maxs[0] - candidateAsset.local_mins[0] < 16.0f ||
        candidateAsset.local_maxs[1] - candidateAsset.local_mins[1] <=
            candidateAsset.local_maxs[0] - candidateAsset.local_mins[0] +
                4.0f) {
      continue;
    }
    mover = candidate;
    identity = candidateIdentity;
    asset = candidateAsset;
    break;
  }
  if (!mover) {
    status.failure = 6;
    return false;
  }

  gentity_t *rider = nullptr;
  for (std::size_t index = 1;
       index <= static_cast<std::size_t>(game.maxClients) &&
       index < poseTracks.size() + 1u;
       ++index) {
    gentity_t *candidate = &g_entities[index];
    if (EligibleLivePlayer(candidate)) {
      rider = candidate;
      break;
    }
  }
  const std::size_t riderIndex = ClientIndex(rider);
  if (!rider || riderIndex >= poseTracks.size()) {
    status.failure = 14;
    return false;
  }
  worr_event_entity_ref_v1 riderIdentity{};
  if (!EntityRef(rider, riderIdentity)) {
    status.failure = 15;
    return false;
  }
  uint64_t normalFrameCaptureTimeUs = 0;
  MoverTrack *frameMoverTrack = FindMoverTrack(identity);
  if (!frameMoverTrack ||
      !RuntimeRiderFrameContinuity(poseTracks[riderIndex], *frameMoverTrack,
                                   identity, probe->rider_continuity_samples,
                                   normalFrameCaptureTimeUs)) {
    status.failure = 16;
    return false;
  }
  probe->rider_frame_continuity = true;

  // Freeze a scene at the newest pair captured by ordinary server frames. This
  // proves that the live pusher/player history is usable by the same immutable
  // scene builder that authoritative rewind traces consume, before the scoped
  // diagnostic setup below changes either entity.
  worr_rewind_policy_decision_v1 normalFrameDecision{};
  normalFrameDecision.struct_size = sizeof(normalFrameDecision);
  normalFrameDecision.schema_version = WORR_REWIND_ABI_VERSION;
  normalFrameDecision.flags = WORR_REWIND_DECISION_ACCEPTED;
  normalFrameDecision.reason = WORR_REWIND_POLICY_EXACT;
  normalFrameDecision.command_id = {1u, 2u};
  normalFrameDecision.snapshot_id = {map.map_epoch, 2u};
  normalFrameDecision.source_snapshot_id = normalFrameDecision.snapshot_id;
  normalFrameDecision.watermark_provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  normalFrameDecision.requested_time_us = normalFrameCaptureTimeUs;
  normalFrameDecision.mapped_time_us = normalFrameCaptureTimeUs;
  normalFrameDecision.applied_time_us = normalFrameCaptureTimeUs;
  if (!Worr_RewindPolicyDecisionValidateV1(&normalFrameDecision, true)) {
    status.failure = 17;
    return false;
  }
  const worr_rewind_scene_v1 *normalFrameScene =
      CanonicalScene(riderIndex, normalFrameDecision);
  const worr_rewind_scene_candidate_v1 *normalFrameRider = nullptr;
  const worr_rewind_scene_candidate_v1 *normalFrameMover = nullptr;
  if (normalFrameScene) {
    for (uint32_t index = 0; index < normalFrameScene->count; ++index) {
      const worr_rewind_scene_candidate_v1 &candidate =
          normalFrameScene->slots[index];
      if (candidate.pose.entity.index == riderIdentity.index &&
          candidate.pose.entity.generation == riderIdentity.generation &&
          candidate.pose.collision_shape == WORR_REWIND_COLLISION_BOUNDS) {
        normalFrameRider = &candidate;
      }
      if (candidate.pose.entity.index == identity.index &&
          candidate.pose.entity.generation == identity.generation &&
          candidate.pose.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL) {
        normalFrameMover = &candidate;
      }
    }
  }
  float normalRelativeOrigin[3]{};
  float normalRelativeAngles[3]{};
  if (normalFrameRider && normalFrameMover) {
    for (std::size_t component = 0; component < 3; ++component) {
      normalRelativeOrigin[component] =
          normalFrameRider->pose.origin[component] -
          normalFrameMover->pose.origin[component];
      normalRelativeAngles[component] =
          normalFrameRider->pose.angles[component] -
          normalFrameMover->pose.angles[component];
    }
  }
  probe->rider_frame_scene_sealed =
      normalFrameScene &&
      (normalFrameScene->flags & WORR_REWIND_SCENE_SEALED) != 0 &&
      normalFrameRider && normalFrameMover &&
      normalFrameRider->query_reason == WORR_REWIND_QUERY_EXACT &&
      normalFrameMover->query_reason == WORR_REWIND_QUERY_EXACT &&
      normalFrameRider->pose.server_time_us == normalFrameCaptureTimeUs &&
      normalFrameMover->pose.server_time_us == normalFrameCaptureTimeUs &&
      (normalFrameRider->pose.flags & WORR_REWIND_POSE_HAS_MOVER) != 0 &&
      normalFrameRider->pose.mover.index == identity.index &&
      normalFrameRider->pose.mover.generation == identity.generation &&
      std::memcmp(normalFrameRider->pose.mover_relative_origin,
                  normalRelativeOrigin, sizeof(normalRelativeOrigin)) == 0 &&
      std::memcmp(normalFrameRider->pose.mover_relative_angles,
                  normalRelativeAngles, sizeof(normalRelativeAngles)) == 0;
  if (!probe->rider_frame_scene_sealed) {
    status.failure = 17;
    return false;
  }

  uint64_t captureTimeUs = 0;
  if (!CurrentAuthoritativeTimeUs(captureTimeUs)) {
    status.failure = 7;
    return false;
  }

  // Keep all setup mutations scoped and restore the active map entity even if
  // a later assertion rejects the probe. The before/after authority guard is
  // deliberately taken after setup, so it measures the real trace path only.
  struct RestoreMover {
    gentity_t *entity;
    Vector3 origin;
    Vector3 angles;
    bool linked;

    ~RestoreMover() {
      entity->s.origin = origin;
      entity->s.angles = angles;
      if (linked)
        gi.linkEntity(entity);
      else
        gi.unlinkEntity(entity);
    }
  } restore{mover, mover->s.origin, mover->s.angles, mover->linked};

  struct RestoreRider {
    gentity_t *entity;
    Vector3 origin;
    Vector3 angles;
    gentity_t *ground;
    bool linked;

    ~RestoreRider() {
      entity->s.origin = origin;
      entity->s.angles = angles;
      entity->groundEntity = ground;
      if (linked)
        gi.linkEntity(entity);
      else
        gi.unlinkEntity(entity);
    }
  } riderRestore{rider, rider->s.origin, rider->s.angles, rider->groundEntity,
                  rider->linked};

  // The fixture brush has an intentionally asymmetric XY footprint.  Capture
  // it at a quarter turn so a ray at its rotated-only X extent proves that the
  // sealed pose's angles reach the native transformed-BSP provider; an
  // unrotated asset at the same origin cannot block that ray.
  mover->s.angles[YAW] += 90.0f;
  gi.linkEntity(mover);
  probe->rotation_applied = true;

  // The runner creates a real engine bot before invoking this console command.
  // Place that actual spawned player on the moved brush and retain a non-zero
  // angle offset, so the ordinary player capture must record both rider
  // identity and mover-relative origin/angles.
  rider->groundEntity = mover;
  rider->s.origin = mover->s.origin;
  rider->s.origin.z += asset.local_maxs[2] - rider->mins.z;
  rider->s.angles = mover->s.angles;
  rider->s.angles[YAW] += 15.0f;
  gi.linkEntity(rider);
  probe->rider_setup = true;
  const Vector3 expectedRiderRelativeOrigin = rider->s.origin - mover->s.origin;
  const Vector3 expectedRiderRelativeAngles = rider->s.angles - mover->s.angles;

  // Capture the fixture's authentic inline BSP at its historical transform.
  // The current collider then moves by 96 units: it would intersect the same
  // query if the baseline did not omit represented live movers.
  LagCompensation_RecordFrame(rider);
  LagCompensation_RecordMovers();
  MoverTrack *moverTrack = FindMoverTrack(identity);
  if (!moverTrack || moverTrack->history.count == 0) {
    status.failure = 9;
    return false;
  }

  // Startup commands can run in the same simulation timestamp as the map's
  // normal final-frame mover capture.  Keep that production call above, then
  // append one strictly later, fully validated test sample through the same
  // builder/history ABI so this probe cannot accidentally select the preceding
  // unrotated pose.  The temporary sample and every frozen scene cache are
  // restored before the command returns.
  if (captureTick == std::numeric_limits<uint32_t>::max() ||
      captureTimeUs > std::numeric_limits<uint64_t>::max() - 1000u) {
    status.failure = 13;
    return false;
  }
  struct RestoreProbeMoverHistories {
    std::array<worr_rewind_history_v1, kMoverTrackCapacity> histories{};
    std::array<bool, kMoverTrackCapacity> touched{};

    ~RestoreProbeMoverHistories() {
      for (std::size_t index = 0; index < moverTracks.size(); ++index) {
        if (touched[index])
          moverTracks[index].history = histories[index];
      }
      InvalidateSceneCaches();
    }
  } historyRestore{};
  struct RestoreProbePlayerHistory {
    PoseTrack *track;
    worr_rewind_history_v1 history;

    ~RestoreProbePlayerHistory() {
      track->history = history;
      InvalidateSceneCaches();
    }
  } riderHistoryRestore{&poseTracks[riderIndex], poseTracks[riderIndex].history};
  const uint64_t probeCaptureTimeUs = captureTimeUs + 1000u;
  if (riderHistoryRestore.track->history.count >=
      riderHistoryRestore.track->history.capacity) {
    status.failure = 15;
    return false;
  }
  worr_rewind_pose_v1 riderCapture{};
  uint32_t riderAppendReason = WORR_REWIND_APPEND_REJECT_INVALID;
  if (!BuildPose(*rider, probeCaptureTimeUs, riderCapture)) {
    status.failure = 15;
    return false;
  }
  riderCapture.server_tick = captureTick + 1u;
  if (!Worr_RewindHistoryAppendV1(&riderHistoryRestore.track->history,
                                   &riderCapture, &riderAppendReason) ||
      riderAppendReason != WORR_REWIND_APPEND_ACCEPTED) {
    status.failure = 15;
    return false;
  }
  bool selectedCaptureAppended = false;
  for (std::size_t index = 0; index < moverTracks.size(); ++index) {
    MoverTrack &track = moverTracks[index];
    gentity_t *current = EntityFromRef(track.identity);
    if (!track.initialized || !EligibleLiveMover(current))
      continue;
    if (track.history.count >= track.history.capacity) {
      status.failure = 13;
      return false;
    }
    historyRestore.histories[index] = track.history;
    historyRestore.touched[index] = true;
    worr_rewind_pose_v1 probeCapture{};
    uint32_t appendReason = WORR_REWIND_APPEND_REJECT_INVALID;
    if (!BuildMoverPose(*current, probeCaptureTimeUs, map, probeCapture)) {
      status.failure = 13;
      return false;
    }
    probeCapture.server_tick = captureTick + 1u;
    if (!Worr_RewindHistoryAppendV1(&track.history, &probeCapture,
                                     &appendReason) ||
        appendReason != WORR_REWIND_APPEND_ACCEPTED) {
      status.failure = 13;
      return false;
    }
    selectedCaptureAppended |= &track == moverTrack;
  }
  if (!selectedCaptureAppended) {
    status.failure = 13;
    return false;
  }
  worr_rewind_policy_decision_v1 decision{};
  decision.struct_size = sizeof(decision);
  decision.schema_version = WORR_REWIND_ABI_VERSION;
  decision.flags = WORR_REWIND_DECISION_ACCEPTED;
  decision.reason = WORR_REWIND_POLICY_EXACT;
  decision.command_id = {1u, 1u};
  decision.snapshot_id = {map.map_epoch, 1u};
  decision.source_snapshot_id = decision.snapshot_id;
  decision.watermark_provenance = WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  decision.requested_time_us = probeCaptureTimeUs;
  decision.mapped_time_us = probeCaptureTimeUs;
  decision.applied_time_us = probeCaptureTimeUs;
  if (!Worr_RewindPolicyDecisionValidateV1(&decision, true)) {
    status.failure = 8;
    return false;
  }

  const worr_rewind_scene_v1 *scene = CanonicalScene(0, decision);
  HistoricalBrushContext historicalBrushes{};
  if (!scene) {
    status.failure = 10;
    return false;
  }
  if (!BuildHistoricalBrushContext(scene, historicalBrushes)) {
    status.failure = 11;
    return false;
  }
  probe->setup_ready = true;
  probe->scene_ready = true;
  probe->candidate_count = scene->count;

  const worr_rewind_pose_v1 *historicalPose = nullptr;
  const worr_rewind_pose_v1 *historicalRiderPose = nullptr;
  for (uint32_t index = 0; index < scene->count; ++index) {
    const worr_rewind_pose_v1 &pose = scene->slots[index].pose;
    if (pose.entity.index == identity.index &&
        pose.entity.generation == identity.generation &&
        pose.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL) {
      historicalPose = &pose;
    }
    if (pose.entity.index == riderIdentity.index &&
        pose.entity.generation == riderIdentity.generation &&
        pose.collision_shape == WORR_REWIND_COLLISION_BOUNDS) {
      historicalRiderPose = &pose;
    }
  }
  if (!historicalPose || !HistoricalBrushAssetMatches(*historicalPose, asset)) {
    status.failure = 12;
    return false;
  }
  float expectedRiderRelativeOriginArray[3]{};
  float expectedRiderRelativeAnglesArray[3]{};
  CopyVector(expectedRiderRelativeOriginArray, expectedRiderRelativeOrigin);
  CopyVector(expectedRiderRelativeAnglesArray, expectedRiderRelativeAngles);
  probe->rider_provenance_sealed =
      historicalRiderPose &&
      (historicalRiderPose->flags & WORR_REWIND_POSE_HAS_MOVER) != 0 &&
      historicalRiderPose->mover.index == identity.index &&
      historicalRiderPose->mover.generation == identity.generation &&
      historicalRiderPose->server_time_us == probeCaptureTimeUs &&
      std::memcmp(historicalRiderPose->mover_relative_origin,
                  expectedRiderRelativeOriginArray,
                  sizeof(expectedRiderRelativeOriginArray)) == 0 &&
      std::memcmp(historicalRiderPose->mover_relative_angles,
                  expectedRiderRelativeAnglesArray,
                  sizeof(expectedRiderRelativeAnglesArray)) == 0;
  if (!probe->rider_provenance_sealed) {
    status.failure = 15;
    return false;
  }

  const Vector3 historicalOrigin = ToVector(historicalPose->origin);
  const float centerZ =
      (historicalPose->mins[2] + historicalPose->maxs[2]) * 0.5f;
  const float localXHalf =
      (historicalPose->maxs[0] - historicalPose->mins[0]) * 0.5f;
  const float localYHalf =
      (historicalPose->maxs[1] - historicalPose->mins[1]) * 0.5f;
  const float rotationOnlyX = localXHalf + (localYHalf - localXHalf) * 0.5f;
  Vector3 captureForward, captureRight, captureUp;
  AngleVectors(ToVector(historicalPose->angles), captureForward, captureRight,
               captureUp);
  Vector3 start = historicalOrigin + captureRight * rotationOnlyX;
  Vector3 end = start;
  start -= captureForward * 64.0f;
  end += captureForward * 64.0f;
  start.z += centerZ;
  end.z += centerZ;
  const TraceBounds bounds = CalculateTraceBounds(start, nullptr, nullptr, end);
  const trace_t fixtureReference =
      ClipEntity(mover, start, nullptr, nullptr, end, MASK_SHOT);
  probe->fixture_reference_blocked =
      fixtureReference.ent == mover && fixtureReference.fraction > 0.0f &&
      fixtureReference.fraction < 0.5f;

  // Use the identical immutable asset/context at the pose's quarter-turn
  // predecessor as a negative control. The fixture now rotates during the
  // ordinary frame-continuity phase, so restore.angles is no longer generally
  // an unrotated world orientation. This must dispatch but remain clear:
  // otherwise the positive historical result would not demonstrate
  // rotation-sensitive collision behavior.
  worr_rewind_pose_v1 unrotatedPose = *historicalPose;
  unrotatedPose.angles[YAW] -= 90.0f;
  trace_t rotationControl{};
  rotationControl.fraction = 1.0f;
  const bool rotationControlDispatched = ClipHistoricalBrushPose(
      rotationControl, unrotatedPose, historicalBrushes, start, nullptr,
      nullptr, end, MASK_SHOT);
  probe->rotation_control_unblocked =
      rotationControlDispatched && rotationControl.fraction == 1.0f;

  mover->s.origin.x += 96.0f;
  mover->s.angles = restore.angles;
  gi.linkEntity(mover);

  const uint64_t authorityBefore = AuthoritativeCollisionHash();
  trace_t result = TraceCurrentScene(start, nullptr, nullptr, end, nullptr,
                                     MASK_SHOT, nullptr, 0, true,
                                     &historicalBrushes, bounds);
  probe->baseline_fraction = result.fraction;
  probe->baseline_unblocked = result.fraction == 1.0f;
  const bool dispatched = ClipHistoricalBrushPose(
      result, *historicalPose, historicalBrushes, start, nullptr, nullptr, end,
      MASK_SHOT);
  probe->historical_dispatched = dispatched;
  probe->historical_fraction = result.fraction;
  probe->historical_blocked =
      dispatched && result.ent == mover && result.fraction > 0.0f &&
      result.fraction < 0.5f;
  probe->authority_unchanged =
      authorityBefore == AuthoritativeCollisionHash();

  status.passed = probe->setup_ready && probe->scene_ready &&
                  probe->rotation_applied && probe->rider_setup &&
                  probe->rider_frame_continuity &&
                  probe->rider_frame_scene_sealed &&
                  probe->rider_provenance_sealed &&
                  probe->fixture_reference_blocked &&
                  probe->rotation_control_unblocked &&
                  probe->baseline_unblocked && probe->historical_blocked &&
                  probe->authority_unchanged;
  return status.passed;
}

bool LagCompensation_ArmRailDamageRuntimeProbe() {
  if (!initialized)
    return false;

  std::array<gentity_t *, 2> players{};
  uint32_t playerCount = 0;
  for (std::size_t index = 1;
       index <= static_cast<std::size_t>(game.maxClients) &&
       index < poseTracks.size() + 1u;
       ++index) {
    gentity_t *candidate = &g_entities[index];
    if (!candidate->inUse || !candidate->client || candidate->deadFlag)
      continue;
    players[playerCount++] = candidate;
    if (playerCount == players.size())
      break;
  }
  if (playerCount != players.size())
    return false;

  // The collision-only BSP intentionally has no world floor. Keep the two
  // real engine bots separated and gravity-free only while the dedicated
  // acceptance runner captures their ordinary end-frame history; otherwise
  // one can fall out of the fixture before the live weapon probe begins.
  constexpr Vector3 kOrigins[] = {{-192.0f, 192.0f, 64.0f},
                                  {64.0f, 192.0f, 64.0f}};
  for (std::size_t index = 0; index < players.size(); ++index) {
    gentity_t *player = players[index];
    player->s.origin = kOrigins[index];
    player->velocity = {};
    player->groundEntity = nullptr;
    player->gravity = 0.0f;
    gi.linkEntity(player);
  }
  return true;
}

bool LagCompensation_RunRailDamageRuntimeProbe(
    LagCompensationRailDamageRuntimeProbe *probe) {
  if (!probe)
    return false;
  *probe = {};
  struct PublishStatus {
    LagCompensationRailDamageRuntimeProbe *probe;
    bool passed = false;
    uint32_t failure = 0;

    ~PublishStatus() {
      if (!probe || !sg_worr_rewind_rail_damage_selftest_status)
        return;
      const auto scaleFraction = [](float fraction) {
        return static_cast<uint32_t>(std::clamp(fraction, 0.0f, 1.0f) *
                                     1000000.0f + 0.5f);
      };
      char value[192]{};
      std::snprintf(
          value, sizeof(value),
          "%s:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u",
          passed ? "pass" : "fail", probe->setup_ready ? 1u : 0u,
          probe->history_ready ? 1u : 0u,
          probe->current_world_miss ? 1u : 0u,
          probe->rejected_current_fallback ? 1u : 0u,
          probe->rejected_no_damage ? 1u : 0u,
          probe->legacy_rewind_selected ? 1u : 0u,
          probe->rail_policy_observed ? 1u : 0u,
          probe->near_latency_hit ? 1u : 0u,
          probe->bounded_latency_hit ? 1u : 0u,
          probe->capped_latency_hit ? 1u : 0u,
          probe->damage_applied ? 1u : 0u,
          probe->geometry_unchanged ? 1u : 0u,
          probe->query_authority_unchanged ? 1u : 0u,
          probe->candidate_count, probe->damage_amount,
          scaleFraction(probe->current_fraction),
          scaleFraction(probe->near_latency_fraction),
          scaleFraction(probe->bounded_latency_fraction),
          scaleFraction(probe->capped_latency_fraction), failure);
      gi.cvarForceSet("sg_worr_rewind_rail_damage_selftest_status", value);
    }
  } status{probe};

  // This probe is deliberately explicit about all opt-in state. It is run
  // only by a dedicated fixture command and must never make a disabled server
  // look as though it was evaluating live lag compensation.
  if (!initialized || !deathmatch || !deathmatch->integer ||
      !g_lagCompensation || !g_lagCompensation->integer) {
    status.failure = 1;
    return false;
  }
  if (!ObservationEnabled()) {
    status.failure = 2;
    return false;
  }

  gentity_t *shooter = nullptr;
  gentity_t *target = nullptr;
  for (std::size_t index = 1;
       index <= static_cast<std::size_t>(game.maxClients) &&
       index < poseTracks.size() + 1u;
       ++index) {
    gentity_t *candidate = &g_entities[index];
    if (!EligibleLivePlayer(candidate) || !candidate->client)
      continue;
    if (!shooter)
      shooter = candidate;
    else {
      target = candidate;
      break;
    }
  }
  if (!shooter || !target || shooter == target) {
    status.failure = 3;
    return false;
  }

  const std::size_t shooterIndex = ClientIndex(shooter);
  const std::size_t targetIndex = ClientIndex(target);
  if (shooterIndex >= poseTracks.size() || targetIndex >= poseTracks.size() ||
      !poseTracks[shooterIndex].initialized ||
      !poseTracks[targetIndex].initialized) {
    status.failure = 4;
    return false;
  }
  worr_event_entity_ref_v1 targetIdentity{};
  if (!EntityRef(target, targetIdentity)) {
    status.failure = 5;
    return false;
  }

  uint64_t currentTimeUs = 0;
  if (!CurrentAuthoritativeTimeUs(currentTimeUs)) {
    status.failure = 6;
    return false;
  }
  const uint32_t currentFrame = gi.ServerFrame();
  const PoseTrack &shooterTrack = poseTracks[shooterIndex];
  struct HistoricalAcknowledgement {
    LegacyFrameSample frame{};
    worr_rewind_pose_result_v1 target{};
    uint64_t rawAgeUs = 0;
    bool found = false;
  };
  HistoricalAcknowledgement nearAcknowledgement{};
  HistoricalAcknowledgement boundedAcknowledgement{};
  HistoricalAcknowledgement cappedAcknowledgement{};
  const uint64_t maxRewindUs =
      static_cast<uint64_t>(MaxRewindMs()) * UINT64_C(1000);
  const uint64_t nearWindowUs = maxRewindUs / 2u;
  for (uint32_t age = 0; age < shooterTrack.legacyFrames.count; ++age) {
    const LegacyFrameSample &sample =
        NewestLegacyFrame(shooterTrack.legacyFrames, age);
    if (!sample.serverFrame || sample.serverFrame >= currentFrame ||
        sample.timeUs > currentTimeUs) {
      continue;
    }
    const uint64_t rawAgeUs = currentTimeUs - sample.timeUs;
    worr_rewind_pose_query_v1 query{};
    query.struct_size = sizeof(query);
    query.schema_version = WORR_REWIND_ABI_VERSION;
    query.entity = targetIdentity;
    query.map_epoch = historyMapEpoch;
    query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
    query.target_time_us = sample.timeUs;
    worr_rewind_pose_result_v1 candidate{};
    if (!Worr_RewindHistoryQueryV1(&poseTracks[targetIndex].history, &query,
                                   &candidate) ||
        !candidate.found ||
        candidate.pose.collision_shape != WORR_REWIND_COLLISION_BOUNDS ||
        (candidate.pose.flags & WORR_REWIND_POSE_LINKED) == 0 ||
        (candidate.pose.flags & WORR_REWIND_POSE_DAMAGEABLE) == 0) {
      continue;
    }
    HistoricalAcknowledgement *selected = nullptr;
    if (rawAgeUs <= nearWindowUs && !nearAcknowledgement.found) {
      selected = &nearAcknowledgement;
    } else if (rawAgeUs <= maxRewindUs &&
               !boundedAcknowledgement.found) {
      selected = &boundedAcknowledgement;
    } else if (rawAgeUs > maxRewindUs && !cappedAcknowledgement.found) {
      selected = &cappedAcknowledgement;
    }
    if (!selected)
      continue;
    selected->frame = sample;
    selected->target = candidate;
    selected->rawAgeUs = rawAgeUs;
    selected->found = true;
    if (nearAcknowledgement.found && boundedAcknowledgement.found &&
        cappedAcknowledgement.found) {
      break;
    }
  }
  if (!nearAcknowledgement.found || !boundedAcknowledgement.found ||
      !cappedAcknowledgement.found) {
    status.failure = 7;
    return false;
  }
  probe->history_ready = true;

  // The two fixture bots are real engine players. The bot source marker is
  // cleared only for these two synchronous calls so ResolveTarget exercises
  // its normal non-bot command acknowledgement branch. The marker, command,
  // health, velocity, linkage, and geometry are restored before this console
  // command returns; ordinary gameplay never enters this diagnostic mode.
  struct RestorePlayers {
    gentity_t *shooter;
    gentity_t *target;
    Vector3 shooterOrigin;
    Vector3 shooterAngles;
    Vector3 shooterVelocity;
    gentity_t *shooterGround;
    float shooterGravity;
    bool shooterLinked;
    svflags_t shooterFlags;
    usercmd_t shooterCommand;
    Vector3 targetOrigin;
    Vector3 targetAngles;
    Vector3 targetVelocity;
    gentity_t *targetGround;
    float targetGravity;
    bool targetLinked;
    solid_t targetSolid;
    contents_t targetClipMask;
    int targetHealth;

    ~RestorePlayers() {
      shooter->s.origin = shooterOrigin;
      shooter->s.angles = shooterAngles;
      shooter->velocity = shooterVelocity;
      shooter->groundEntity = shooterGround;
      shooter->gravity = shooterGravity;
      shooter->svFlags = shooterFlags;
      shooter->client->cmd = shooterCommand;
      target->s.origin = targetOrigin;
      target->s.angles = targetAngles;
      target->velocity = targetVelocity;
      target->groundEntity = targetGround;
      target->gravity = targetGravity;
      target->health = targetHealth;
      if (shooterLinked)
        gi.linkEntity(shooter);
      else
        gi.unlinkEntity(shooter);
      if (targetLinked)
        gi.linkEntity(target);
      else
        gi.unlinkEntity(target);
    }
  } restore{shooter,
            target,
            shooter->s.origin,
            shooter->s.angles,
            shooter->velocity,
            shooter->groundEntity,
            shooter->gravity,
            shooter->linked,
            shooter->svFlags,
            shooter->client->cmd,
            target->s.origin,
            target->s.angles,
            target->velocity,
            target->groundEntity,
            target->gravity,
            target->linked,
            target->solid,
            target->clipMask,
            target->health};

  constexpr int kProbeDamage = 10;
  if (target->health <= kProbeDamage * 3 || !target->takeDamage ||
      target->deadFlag) {
    status.failure = 8;
    return false;
  }
  // The arm phase keeps the fixture bots stationary while normal frames fill
  // history. All three authority classes must therefore resolve the same
  // target geometry; otherwise a latency result could be an accidental
  // aim-position change rather than the selected rewind boundary.
  if (std::memcmp(nearAcknowledgement.target.pose.origin,
                  boundedAcknowledgement.target.pose.origin,
                  sizeof(nearAcknowledgement.target.pose.origin)) != 0 ||
      std::memcmp(nearAcknowledgement.target.pose.origin,
                  cappedAcknowledgement.target.pose.origin,
                  sizeof(nearAcknowledgement.target.pose.origin)) != 0) {
    status.failure = 8;
    return false;
  }
  const Vector3 historicalOrigin =
      ToVector(nearAcknowledgement.target.pose.origin);
  const Vector3 start = historicalOrigin - Vector3{160.0f, 0.0f, 0.0f};
  const Vector3 end = historicalOrigin + Vector3{8192.0f, 0.0f, 0.0f};
  const Vector3 currentTargetOrigin =
      historicalOrigin + Vector3{0.0f, 96.0f, 0.0f};
  shooter->s.origin = start;
  shooter->velocity = {};
  shooter->groundEntity = nullptr;
  target->s.origin = currentTargetOrigin;
  target->velocity = {};
  target->groundEntity = nullptr;
  gi.linkEntity(shooter);
  gi.linkEntity(target);
  probe->setup_ready = true;

  const trace_t current = NativeTrace(start, nullptr, nullptr, end, shooter,
                                      MASK_PROJECTILE, nullptr, 0);
  probe->current_fraction = current.fraction;
  probe->current_world_miss = current.fraction == 1.0f;
  if (!probe->current_world_miss) {
    status.failure = 9;
    return false;
  }

  shooter->svFlags &= ~SVF_BOT;
  const int targetHealthBefore = target->health;

  std::array<worr_rewind_observation_v1, kObservationCapacity> observations{};
  auto findRailObservation =
      [&](uint64_t firstSequence, bool historical,
          worr_rewind_observation_v1 &result) {
        uint32_t observationCount = 0;
        if (!LagCompensation_CopyObservations(observations.data(),
                                              observations.size(),
                                              &observationCount)) {
          return false;
        }
        for (uint32_t index = 0; index < observationCount; ++index) {
          const worr_rewind_observation_v1 &candidate = observations[index];
          if (candidate.observation_sequence < firstSequence ||
              candidate.weapon_policy != WORR_REWIND_WEAPON_RAILGUN) {
            continue;
          }
          if (!historical &&
              candidate.path == WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD &&
              candidate.outcome ==
                  WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_MISS &&
              candidate.fallback_reason ==
                  WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED) {
            result = candidate;
            return true;
          }
          if (historical &&
              candidate.path == WORR_REWIND_OBSERVATION_PATH_LEGACY &&
              candidate.outcome ==
                  WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT &&
              candidate.hit_entity.index == targetIdentity.index &&
              candidate.hit_entity.generation == targetIdentity.generation) {
            result = candidate;
            return true;
          }
        }
        return false;
      };

  // A current-or-future acknowledgement is never a rewind target. The same
  // production rail path must therefore miss the displaced current target and
  // leave health unchanged before the valid acknowledgement classes are tried.
  const uint64_t rejectedSequenceBefore = observationJournal.next_sequence;
  shooter->client->cmd.serverFrame = currentFrame;
  shooter->client->cmd.serverFrameDelta = 0;
  fire_rail(shooter, start, Vector3{1.0f, 0.0f, 0.0f}, kProbeDamage, 0);
  const int targetHealthAfterRejected = target->health;
  worr_rewind_observation_v1 rejected{};
  if (!findRailObservation(rejectedSequenceBefore, false, rejected)) {
    status.failure = 10;
    return false;
  }

  // These acknowledgements come from the shooter slot's ordinary server-frame
  // capture and are selected only after matching eligible target history. No
  // caller-provided timestamp or target identity reaches the production trace.
  auto fireHistorical = [&](const HistoricalAcknowledgement &acknowledgement,
                            worr_rewind_observation_v1 &result) {
    const uint64_t firstSequence = observationJournal.next_sequence;
    shooter->client->cmd.serverFrame = acknowledgement.frame.serverFrame;
    shooter->client->cmd.serverFrameDelta = 0;
    fire_rail(shooter, start, Vector3{1.0f, 0.0f, 0.0f}, kProbeDamage, 0);
    return findRailObservation(firstSequence, true, result);
  };
  worr_rewind_observation_v1 near{};
  worr_rewind_observation_v1 bounded{};
  worr_rewind_observation_v1 capped{};
  if (!fireHistorical(nearAcknowledgement, near) ||
      !fireHistorical(boundedAcknowledgement, bounded) ||
      !fireHistorical(cappedAcknowledgement, capped)) {
    status.failure = 10;
    return false;
  }

  constexpr uint32_t kAuthorityGuard =
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED |
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
  probe->rejected_current_fallback =
      rejected.weapon_policy == WORR_REWIND_WEAPON_RAILGUN &&
      rejected.path == WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD &&
      rejected.outcome == WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_MISS &&
      rejected.fallback_reason ==
          WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED &&
      (rejected.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) != 0;
  probe->rejected_no_damage = targetHealthAfterRejected == targetHealthBefore;
  probe->legacy_rewind_selected =
      near.path == WORR_REWIND_OBSERVATION_PATH_LEGACY &&
      bounded.path == WORR_REWIND_OBSERVATION_PATH_LEGACY &&
      capped.path == WORR_REWIND_OBSERVATION_PATH_LEGACY &&
      near.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      bounded.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      capped.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      (near.flags & WORR_REWIND_OBSERVATION_POLICY_ACCEPTED) != 0 &&
      (bounded.flags & WORR_REWIND_OBSERVATION_POLICY_ACCEPTED) != 0 &&
      (capped.flags & WORR_REWIND_OBSERVATION_POLICY_ACCEPTED) != 0 &&
      (near.flags & WORR_REWIND_OBSERVATION_HISTORICAL_QUERY) != 0 &&
      (bounded.flags & WORR_REWIND_OBSERVATION_HISTORICAL_QUERY) != 0 &&
      (capped.flags & WORR_REWIND_OBSERVATION_HISTORICAL_QUERY) != 0;
  probe->rail_policy_observed =
      near.weapon_policy == WORR_REWIND_WEAPON_RAILGUN &&
      bounded.weapon_policy == WORR_REWIND_WEAPON_RAILGUN &&
      capped.weapon_policy == WORR_REWIND_WEAPON_RAILGUN;
  probe->near_latency_fraction = near.trace_fraction;
  probe->bounded_latency_fraction = bounded.trace_fraction;
  probe->capped_latency_fraction = capped.trace_fraction;
  probe->candidate_count =
      std::min({near.candidate_count, bounded.candidate_count,
                capped.candidate_count});
  auto historicalHit = [&](const worr_rewind_observation_v1 &observation) {
    return observation.outcome ==
               WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT &&
           observation.hit_entity.index == targetIdentity.index &&
           observation.hit_entity.generation == targetIdentity.generation &&
           observation.trace_fraction > 0.0f && observation.trace_fraction < 1.0f;
  };
  probe->near_latency_hit =
      historicalHit(near) && near.applied_time_us == nearAcknowledgement.frame.timeUs;
  probe->bounded_latency_hit =
      historicalHit(bounded) &&
      bounded.applied_time_us == boundedAcknowledgement.frame.timeUs;
  const uint64_t cappedTimeUs = currentTimeUs - maxRewindUs;
  probe->capped_latency_hit =
      historicalHit(capped) && cappedAcknowledgement.rawAgeUs > maxRewindUs &&
      capped.applied_time_us == cappedTimeUs &&
      capped.applied_time_us > cappedAcknowledgement.frame.timeUs;
  probe->damage_amount = targetHealthBefore > target->health
                             ? static_cast<uint32_t>(targetHealthBefore -
                                                     target->health)
                             : 0u;
  probe->damage_applied = probe->damage_amount == kProbeDamage * 3;
  probe->geometry_unchanged =
      target->s.origin == currentTargetOrigin && target->velocity == Vector3{} &&
      target->groundEntity == nullptr && target->linked &&
      target->solid == restore.targetSolid &&
      target->clipMask == restore.targetClipMask;
  probe->query_authority_unchanged =
      (rejected.flags & kAuthorityGuard) == kAuthorityGuard &&
      (near.flags & kAuthorityGuard) == kAuthorityGuard &&
      (bounded.flags & kAuthorityGuard) == kAuthorityGuard &&
      (capped.flags & kAuthorityGuard) == kAuthorityGuard;

  status.passed =
      probe->setup_ready && probe->history_ready && probe->current_world_miss &&
      probe->rejected_current_fallback && probe->rejected_no_damage &&
      probe->legacy_rewind_selected && probe->rail_policy_observed &&
      probe->near_latency_hit && probe->bounded_latency_hit &&
      probe->capped_latency_hit && probe->damage_applied &&
      probe->geometry_unchanged && probe->query_authority_unchanged;
  if (!status.passed)
    status.failure = 11;
  return status.passed;
}

bool LagCompensation_ArmCanonicalRailDamageRuntimeProbe() {
  return CanonicalRailProbeArm();
}

bool LagCompensation_ArmCanonicalMachinegunDamageRuntimeProbe() {
  return CanonicalMachinegunProbeArm();
}

bool LagCompensation_ArmCanonicalChaingunDamageRuntimeProbe() {
  return CanonicalChaingunProbeArm();
}

bool LagCompensation_ArmCanonicalSuperShotgunDamageRuntimeProbe() {
  return CanonicalSuperShotgunProbeArm();
}

bool LagCompensation_ArmCanonicalDisruptorDamageRuntimeProbe() {
  return CanonicalDisruptorProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamDamageRuntimeProbe() {
  return CanonicalPlasmaBeamProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamHeldDamageRuntimeProbe() {
  return CanonicalPlasmaBeamHeldProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamSustainedDamageRuntimeProbe() {
  return CanonicalPlasmaBeamSustainedProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamReleaseDamageRuntimeProbe() {
  return CanonicalPlasmaBeamReleaseProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamWaterRetraceDamageRuntimeProbe() {
  return CanonicalPlasmaBeamWaterRetraceProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltDamageRuntimeProbe() {
  return CanonicalThunderboltProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltHeldDamageRuntimeProbe() {
  return CanonicalThunderboltHeldProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltSustainedDamageRuntimeProbe() {
  return CanonicalThunderboltSustainedProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltReleaseDamageRuntimeProbe() {
  return CanonicalThunderboltReleaseProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltWaterRetraceDamageRuntimeProbe() {
  return CanonicalThunderboltWaterRetraceProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltDischargeDamageRuntimeProbe() {
  return CanonicalThunderboltDischargeProbeArm();
}

bool LagCompensation_ArmCanonicalShotgunDamageRuntimeProbe() {
  return CanonicalShotgunProbeArm();
}

void LagCompensation_PrepareCanonicalWeaponDamageCommand(gentity_t *entity,
                                                          usercmd_t *command) {
  CanonicalRailProbePrepareCommand(entity, command);
}

void LagCompensation_ObserveThunderboltUnderwaterDischarge(gentity_t *shooter) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.thunderbolt_discharge_required ||
      shooter != canonicalRailProbe.shooter ||
      !CanonicalRailProbeSameEntity(shooter,
                                    canonicalRailProbe.shooter_identity)) {
    return;
  }
  canonicalRailProbe.weapon_callback = true;
  canonicalRailProbe.thunderbolt_discharge_observed = true;
  CanonicalRailProbePublish();
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
