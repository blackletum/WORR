/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/snapshot_recovery.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                \
    do {                                                                 \
        if (!(expression)) {                                             \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                    __LINE__, #expression);                              \
            return 1;                                                    \
        }                                                                \
    } while (0)

static worr_snapshot_recovery_observation_v1 observation(
    uint32_t type, uint32_t reason_mask, uint32_t snapshot_number,
    int32_t base_snapshot_number)
{
    worr_snapshot_recovery_observation_v1 value;

    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.schema_version = WORR_SNAPSHOT_RECOVERY_VERSION;
    value.type = type;
    value.reason_mask = reason_mask;
    value.snapshot_number = snapshot_number;
    value.base_snapshot_number = base_snapshot_number;
    return value;
}

static int test_legacy_recovery_and_bounded_retry(void)
{
    worr_snapshot_recovery_config_v1 config;
    worr_snapshot_recovery_state_v1 state;
    worr_snapshot_recovery_observation_v1 value;
    worr_snapshot_recovery_decision_v1 decision;

    Worr_SnapshotRecoveryDefaultConfigV1(&config);
    CHECK(config.legacy_failure_threshold == 1);
    CHECK(config.canonical_failure_threshold == 3);
    CHECK(config.max_requests_per_burst == 3);
    CHECK(config.cooldown_transmits == 2);
    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);

    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_LEGACY_FAILURE,
                        WORR_SNAPSHOT_RECOVERY_REASON_INVALID_BASE,
                        100, 95);
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(state.active == 1);
    CHECK(state.request_generation == 1);
    CHECK(state.reason_mask ==
          WORR_SNAPSHOT_RECOVERY_REASON_INVALID_BASE);

    for (uint32_t attempt = 1; attempt <= 3; ++attempt) {
        CHECK(Worr_SnapshotRecoveryDecideV1(&state, &config, &decision) ==
              WORR_SNAPSHOT_RECOVERY_OK);
        CHECK((decision.flags &
               WORR_SNAPSHOT_RECOVERY_DECISION_REQUEST_FULL) != 0);
        CHECK(decision.attempt_in_burst == attempt);
        CHECK(decision.request_generation == 1);
    }

    CHECK(Worr_SnapshotRecoveryDecideV1(&state, &config, &decision) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK((decision.flags & WORR_SNAPSHOT_RECOVERY_DECISION_COOLDOWN) != 0);
    CHECK((decision.flags &
           WORR_SNAPSHOT_RECOVERY_DECISION_REQUEST_FULL) == 0);
    CHECK(decision.cooldown_remaining == 1);
    CHECK(Worr_SnapshotRecoveryDecideV1(&state, &config, &decision) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK((decision.flags & WORR_SNAPSHOT_RECOVERY_DECISION_COOLDOWN) != 0);
    CHECK(decision.cooldown_remaining == 0);

    CHECK(Worr_SnapshotRecoveryDecideV1(&state, &config, &decision) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK((decision.flags &
           WORR_SNAPSHOT_RECOVERY_DECISION_REQUEST_FULL) != 0);
    CHECK(decision.attempt_in_burst == 1);
    CHECK(state.request_decisions == 4);
    CHECK(state.cooldown_suppressions == 2);

    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_DELTA,
                        0, 101, 99);
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(state.active == 1);
    CHECK(state.legacy_failure_streak == 0);

    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_KEYFRAME,
                        0, 102, -1);
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(state.active == 0);
    CHECK(state.reason_mask == 0);
    CHECK(state.recoveries == 1);
    CHECK(Worr_SnapshotRecoveryDecideV1(&state, &config, &decision) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(decision.flags == 0);
    return 0;
}

static int test_canonical_threshold_and_reason_accumulation(void)
{
    worr_snapshot_recovery_config_v1 config;
    worr_snapshot_recovery_state_v1 state;
    worr_snapshot_recovery_observation_v1 value;

    Worr_SnapshotRecoveryDefaultConfigV1(&config);
    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);

    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_FAILURE,
                        WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PROJECTOR,
                        10, 9);
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    value.snapshot_number = 11;
    value.base_snapshot_number = 10;
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(state.active == 0);

    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_SUCCESS,
                        0, 12, 11);
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(state.canonical_failure_streak == 0);

    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_FAILURE,
                        WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PROJECTOR,
                        13, 12);
    for (uint32_t index = 0; index < 3; ++index) {
        value.snapshot_number = 13 + index;
        value.base_snapshot_number = 12 + (int32_t)index;
        CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
              WORR_SNAPSHOT_RECOVERY_OK);
    }
    CHECK(state.active == 1);
    CHECK(state.request_generation == 1);

    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_LEGACY_FAILURE,
                        WORR_SNAPSHOT_RECOVERY_REASON_ENTITY_HISTORY |
                            WORR_SNAPSHOT_RECOVERY_REASON_SEQUENCE_GAP,
                        16, 10);
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK((state.reason_mask &
           WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PROJECTOR) != 0);
    CHECK((state.reason_mask &
           WORR_SNAPSHOT_RECOVERY_REASON_ENTITY_HISTORY) != 0);
    CHECK((state.reason_mask &
           WORR_SNAPSHOT_RECOVERY_REASON_SEQUENCE_GAP) != 0);
    CHECK(state.request_arms == 1);
    return 0;
}

static int test_invalid_inputs_are_transactional(void)
{
    worr_snapshot_recovery_config_v1 config;
    worr_snapshot_recovery_config_v1 invalid_config;
    worr_snapshot_recovery_state_v1 state;
    worr_snapshot_recovery_state_v1 before;
    worr_snapshot_recovery_observation_v1 value;

    Worr_SnapshotRecoveryDefaultConfigV1(&config);
    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    before = state;

    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_LEGACY_FAILURE,
                        WORR_SNAPSHOT_RECOVERY_REASON_INVALID_FRAME,
                        1, 0);
    value.reserved[0] = 1;
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_INVALID_OBSERVATION);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);

    invalid_config = config;
    invalid_config.max_requests_per_burst = 0;
    value.reserved[0] = 0;
    CHECK(Worr_SnapshotRecoveryObserveV1(
              &state, &invalid_config, &value) ==
          WORR_SNAPSHOT_RECOVERY_INVALID_CONFIG);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    return 0;
}

static int expect_invalid_state_transactional(
    const worr_snapshot_recovery_config_v1 *config,
    const worr_snapshot_recovery_state_v1 *invalid_state)
{
    worr_snapshot_recovery_state_v1 state = *invalid_state;
    const worr_snapshot_recovery_state_v1 before = state;
    worr_snapshot_recovery_observation_v1 value = observation(
        WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_KEYFRAME, 0, 10, -1);
    worr_snapshot_recovery_decision_v1 decision;

    CHECK(Worr_SnapshotRecoveryObserveV1(&state, config, &value) ==
          WORR_SNAPSHOT_RECOVERY_INVALID_STATE);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(Worr_SnapshotRecoveryDecideV1(&state, config, &decision) ==
          WORR_SNAPSHOT_RECOVERY_INVALID_STATE);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    return 0;
}

static int test_invalid_state_invariants_are_transactional(void)
{
    worr_snapshot_recovery_config_v1 config;
    worr_snapshot_recovery_state_v1 state;

    Worr_SnapshotRecoveryDefaultConfigV1(&config);

    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    state.active = 1;
    state.reason_mask = WORR_SNAPSHOT_RECOVERY_REASON_MANUAL;
    CHECK(expect_invalid_state_transactional(&config, &state) == 0);

    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    state.active = 1;
    state.request_generation = 1;
    CHECK(expect_invalid_state_transactional(&config, &state) == 0);

    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    state.reason_mask = WORR_SNAPSHOT_RECOVERY_REASON_MANUAL;
    CHECK(expect_invalid_state_transactional(&config, &state) == 0);

    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    state.attempts_in_burst = 1;
    CHECK(expect_invalid_state_transactional(&config, &state) == 0);

    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    state.cooldown_remaining = 1;
    CHECK(expect_invalid_state_transactional(&config, &state) == 0);

    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    state.exhausted = 1;
    state.request_generation = UINT32_MAX - 1;
    CHECK(expect_invalid_state_transactional(&config, &state) == 0);
    return 0;
}

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
    return *seed;
}

static int test_deterministic_replay(void)
{
    worr_snapshot_recovery_config_v1 config;
    worr_snapshot_recovery_state_v1 left;
    worr_snapshot_recovery_state_v1 right;
    uint32_t seed = UINT32_C(0x94d049bb);

    Worr_SnapshotRecoveryDefaultConfigV1(&config);
    CHECK(Worr_SnapshotRecoveryResetV1(&left) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(Worr_SnapshotRecoveryResetV1(&right) ==
          WORR_SNAPSHOT_RECOVERY_OK);

    for (uint32_t index = 1; index <= 20000; ++index) {
        const uint32_t random = next_random(&seed);
        const uint32_t choice = random % 6;
        worr_snapshot_recovery_observation_v1 value;

        switch (choice) {
        case 0:
            value = observation(
                WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_DELTA, 0,
                index + 1, (int32_t)index);
            break;
        case 1:
            value = observation(
                WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_KEYFRAME, 0,
                index, -1);
            break;
        case 2:
            value = observation(
                WORR_SNAPSHOT_RECOVERY_OBSERVE_LEGACY_FAILURE,
                WORR_SNAPSHOT_RECOVERY_REASON_INVALID_BASE, index,
                (int32_t)(index / 2));
            break;
        case 3:
            value = observation(
                WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_SUCCESS, 0,
                index, (int32_t)(index - 1));
            break;
        case 4:
            value = observation(
                WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_FAILURE,
                WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PARITY, index,
                (int32_t)(index - 1));
            break;
        default:
            value = observation(
                WORR_SNAPSHOT_RECOVERY_OBSERVE_FORCE_REQUEST,
                WORR_SNAPSHOT_RECOVERY_REASON_MANUAL, index,
                (int32_t)(index - 1));
            break;
        }

        CHECK(Worr_SnapshotRecoveryObserveV1(&left, &config, &value) ==
              Worr_SnapshotRecoveryObserveV1(&right, &config, &value));
        if ((random & 3u) == 0) {
            worr_snapshot_recovery_decision_v1 left_decision;
            worr_snapshot_recovery_decision_v1 right_decision;
            CHECK(Worr_SnapshotRecoveryDecideV1(
                      &left, &config, &left_decision) ==
                  Worr_SnapshotRecoveryDecideV1(
                      &right, &config, &right_decision));
            CHECK(memcmp(&left_decision, &right_decision,
                         sizeof(left_decision)) == 0);
        }
        CHECK(memcmp(&left, &right, sizeof(left)) == 0);
    }
    return 0;
}

static int test_saturation_and_generation_exhaustion(void)
{
    worr_snapshot_recovery_config_v1 config;
    worr_snapshot_recovery_state_v1 state;
    worr_snapshot_recovery_observation_v1 value;
    worr_snapshot_recovery_decision_v1 decision;

    Worr_SnapshotRecoveryDefaultConfigV1(&config);
    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    state.canonical_failures = UINT64_MAX;
    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_FAILURE,
                        WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PARITY,
                        1, 0);
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(state.canonical_failures == UINT64_MAX);

    CHECK(Worr_SnapshotRecoveryResetV1(&state) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    state.request_generation = UINT32_MAX;
    value = observation(WORR_SNAPSHOT_RECOVERY_OBSERVE_FORCE_REQUEST,
                        WORR_SNAPSHOT_RECOVERY_REASON_MANUAL, 2, 1);
    CHECK(Worr_SnapshotRecoveryObserveV1(&state, &config, &value) ==
          WORR_SNAPSHOT_RECOVERY_GENERATION_EXHAUSTED);
    CHECK(state.exhausted == 1);
    CHECK(state.active == 0);
    CHECK(state.reason_mask == 0);
    CHECK(Worr_SnapshotRecoveryDecideV1(&state, &config, &decision) ==
          WORR_SNAPSHOT_RECOVERY_OK);
    CHECK(decision.flags ==
          WORR_SNAPSHOT_RECOVERY_DECISION_EXHAUSTED);
    return 0;
}

int main(void)
{
    CHECK(test_legacy_recovery_and_bounded_retry() == 0);
    CHECK(test_canonical_threshold_and_reason_accumulation() == 0);
    CHECK(test_invalid_inputs_are_transactional() == 0);
    CHECK(test_invalid_state_invariants_are_transactional() == 0);
    CHECK(test_deterministic_replay() == 0);
    CHECK(test_saturation_and_generation_exhaustion() == 0);
    puts("snapshot recovery tests passed");
    return 0;
}
