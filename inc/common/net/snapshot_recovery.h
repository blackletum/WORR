/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_SNAPSHOT_RECOVERY_VERSION UINT32_C(1)

/*
 * The recovery policy deliberately contains no pointers, wall-clock values,
 * engine types, or transport storage.  One caller-owned state record is a
 * deterministic state machine that can be replayed in tests and copied into
 * later FR-10-T14 telemetry without lifetime coupling.
 */

enum worr_snapshot_recovery_result_v1_e {
    WORR_SNAPSHOT_RECOVERY_OK = 0,
    WORR_SNAPSHOT_RECOVERY_INVALID_ARGUMENT = 1,
    WORR_SNAPSHOT_RECOVERY_INVALID_CONFIG = 2,
    WORR_SNAPSHOT_RECOVERY_INVALID_STATE = 3,
    WORR_SNAPSHOT_RECOVERY_INVALID_OBSERVATION = 4,
    WORR_SNAPSHOT_RECOVERY_GENERATION_EXHAUSTED = 5,
};

enum worr_snapshot_recovery_observation_type_v1_e {
    WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_DELTA = 1,
    WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_KEYFRAME = 2,
    WORR_SNAPSHOT_RECOVERY_OBSERVE_LEGACY_FAILURE = 3,
    WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_SUCCESS = 4,
    WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_FAILURE = 5,
    WORR_SNAPSHOT_RECOVERY_OBSERVE_FORCE_REQUEST = 6,
};

enum worr_snapshot_recovery_reason_v1_e {
    WORR_SNAPSHOT_RECOVERY_REASON_INVALID_BASE = UINT32_C(1) << 0,
    WORR_SNAPSHOT_RECOVERY_REASON_INVALID_FRAME = UINT32_C(1) << 1,
    WORR_SNAPSHOT_RECOVERY_REASON_ENTITY_HISTORY = UINT32_C(1) << 2,
    WORR_SNAPSHOT_RECOVERY_REASON_SEQUENCE_GAP = UINT32_C(1) << 3,
    WORR_SNAPSHOT_RECOVERY_REASON_TRANSPORT_TRUNCATED = UINT32_C(1) << 4,
    WORR_SNAPSHOT_RECOVERY_REASON_FRAGMENT_STALL = UINT32_C(1) << 5,
    WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PROJECTOR = UINT32_C(1) << 6,
    WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PARITY = UINT32_C(1) << 7,
    WORR_SNAPSHOT_RECOVERY_REASON_MANUAL = UINT32_C(1) << 8,
};

enum worr_snapshot_recovery_decision_flags_v1_e {
    WORR_SNAPSHOT_RECOVERY_DECISION_ACTIVE = UINT32_C(1) << 0,
    WORR_SNAPSHOT_RECOVERY_DECISION_REQUEST_FULL = UINT32_C(1) << 1,
    WORR_SNAPSHOT_RECOVERY_DECISION_COOLDOWN = UINT32_C(1) << 2,
    WORR_SNAPSHOT_RECOVERY_DECISION_EXHAUSTED = UINT32_C(1) << 3,
};

typedef struct worr_snapshot_recovery_config_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t legacy_failure_threshold;
    uint32_t canonical_failure_threshold;
    uint32_t max_requests_per_burst;
    uint32_t cooldown_transmits;
    uint32_t reserved[2];
} worr_snapshot_recovery_config_v1;

typedef struct worr_snapshot_recovery_observation_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t type;
    uint32_t reason_mask;
    uint32_t snapshot_number;
    int32_t base_snapshot_number;
    uint32_t reserved[2];
} worr_snapshot_recovery_observation_v1;

typedef struct worr_snapshot_recovery_decision_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t reason_mask;
    uint32_t request_generation;
    uint32_t attempt_in_burst;
    uint32_t cooldown_remaining;
    uint32_t reserved0;
} worr_snapshot_recovery_decision_v1;

typedef struct worr_snapshot_recovery_state_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t active;
    uint32_t exhausted;
    uint32_t request_generation;
    uint32_t reason_mask;
    uint32_t legacy_failure_streak;
    uint32_t canonical_failure_streak;
    uint32_t attempts_in_burst;
    uint32_t cooldown_remaining;
    uint32_t last_snapshot_number;
    int32_t last_base_snapshot_number;
    uint64_t observations;
    uint64_t accepted_delta_frames;
    uint64_t accepted_keyframes;
    uint64_t legacy_failures;
    uint64_t canonical_failures;
    uint64_t request_arms;
    uint64_t request_decisions;
    uint64_t cooldown_suppressions;
    uint64_t recoveries;
} worr_snapshot_recovery_state_v1;

void Worr_SnapshotRecoveryDefaultConfigV1(
    worr_snapshot_recovery_config_v1 *config_out);

uint32_t Worr_SnapshotRecoveryResetV1(
    worr_snapshot_recovery_state_v1 *state);

uint32_t Worr_SnapshotRecoveryObserveV1(
    worr_snapshot_recovery_state_v1 *state,
    const worr_snapshot_recovery_config_v1 *config,
    const worr_snapshot_recovery_observation_v1 *observation);

/*
 * Called once per actual transport opportunity.  REQUEST_FULL means the
 * adapter should ask its current protocol for an uncompressed/keyframe
 * snapshot.  The decision mutates only burst/cooldown accounting; accepting a
 * keyframe is the sole normal transition that clears an armed request.
 */
uint32_t Worr_SnapshotRecoveryDecideV1(
    worr_snapshot_recovery_state_v1 *state,
    const worr_snapshot_recovery_config_v1 *config,
    worr_snapshot_recovery_decision_v1 *decision_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(worr_snapshot_recovery_config_v1) == 32,
              "snapshot recovery config layout changed");
static_assert(sizeof(worr_snapshot_recovery_observation_v1) == 32,
              "snapshot recovery observation layout changed");
static_assert(sizeof(worr_snapshot_recovery_decision_v1) == 32,
              "snapshot recovery decision layout changed");
static_assert(sizeof(worr_snapshot_recovery_state_v1) == 120,
              "snapshot recovery state layout changed");
#else
_Static_assert(sizeof(worr_snapshot_recovery_config_v1) == 32,
               "snapshot recovery config layout changed");
_Static_assert(sizeof(worr_snapshot_recovery_observation_v1) == 32,
               "snapshot recovery observation layout changed");
_Static_assert(sizeof(worr_snapshot_recovery_decision_v1) == 32,
               "snapshot recovery decision layout changed");
_Static_assert(sizeof(worr_snapshot_recovery_state_v1) == 120,
               "snapshot recovery state layout changed");
#endif
