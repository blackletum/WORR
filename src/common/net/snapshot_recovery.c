/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/snapshot_recovery.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define SNAPSHOT_RECOVERY_REASON_MASK                                 \
    ((uint32_t)(WORR_SNAPSHOT_RECOVERY_REASON_INVALID_BASE |          \
                WORR_SNAPSHOT_RECOVERY_REASON_INVALID_FRAME |         \
                WORR_SNAPSHOT_RECOVERY_REASON_ENTITY_HISTORY |        \
                WORR_SNAPSHOT_RECOVERY_REASON_SEQUENCE_GAP |          \
                WORR_SNAPSHOT_RECOVERY_REASON_TRANSPORT_TRUNCATED |   \
                WORR_SNAPSHOT_RECOVERY_REASON_FRAGMENT_STALL |        \
                WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PROJECTOR |   \
                WORR_SNAPSHOT_RECOVERY_REASON_CANONICAL_PARITY |      \
                WORR_SNAPSHOT_RECOVERY_REASON_MANUAL))

#define SNAPSHOT_RECOVERY_MAX_THRESHOLD UINT32_C(1024)
#define SNAPSHOT_RECOVERY_MAX_BURST UINT32_C(64)
#define SNAPSHOT_RECOVERY_MAX_COOLDOWN UINT32_C(4096)

static void saturating_increment_u32(uint32_t *value)
{
    if (*value != UINT32_MAX)
        ++*value;
}

static void saturating_increment_u64(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static bool config_valid(const worr_snapshot_recovery_config_v1 *config)
{
    return config && config->struct_size == sizeof(*config) &&
           config->schema_version == WORR_SNAPSHOT_RECOVERY_VERSION &&
           config->legacy_failure_threshold >= 1 &&
           config->legacy_failure_threshold <=
               SNAPSHOT_RECOVERY_MAX_THRESHOLD &&
           config->canonical_failure_threshold >= 1 &&
           config->canonical_failure_threshold <=
               SNAPSHOT_RECOVERY_MAX_THRESHOLD &&
           config->max_requests_per_burst >= 1 &&
           config->max_requests_per_burst <= SNAPSHOT_RECOVERY_MAX_BURST &&
           config->cooldown_transmits <= SNAPSHOT_RECOVERY_MAX_COOLDOWN &&
           config->reserved[0] == 0 && config->reserved[1] == 0;
}

static bool state_valid(const worr_snapshot_recovery_state_v1 *state)
{
    return state && state->struct_size == sizeof(*state) &&
           state->schema_version == WORR_SNAPSHOT_RECOVERY_VERSION &&
           state->active <= 1 && state->exhausted <= 1 &&
           !(state->active && state->exhausted) &&
           (!state->active || (state->request_generation != 0 &&
                               state->reason_mask != 0)) &&
           (!state->exhausted ||
            state->request_generation == UINT32_MAX) &&
           (state->active ||
            (state->reason_mask == 0 && state->attempts_in_burst == 0 &&
             state->cooldown_remaining == 0)) &&
           (state->reason_mask & ~SNAPSHOT_RECOVERY_REASON_MASK) == 0;
}

static bool state_matches_config(
    const worr_snapshot_recovery_state_v1 *state,
    const worr_snapshot_recovery_config_v1 *config)
{
    return state->attempts_in_burst <= config->max_requests_per_burst &&
           state->cooldown_remaining <= config->cooldown_transmits;
}

static bool observation_valid(
    const worr_snapshot_recovery_observation_v1 *observation)
{
    if (!observation || observation->struct_size != sizeof(*observation) ||
        observation->schema_version != WORR_SNAPSHOT_RECOVERY_VERSION ||
        observation->reserved[0] != 0 || observation->reserved[1] != 0 ||
        (observation->reason_mask & ~SNAPSHOT_RECOVERY_REASON_MASK) != 0) {
        return false;
    }

    switch (observation->type) {
    case WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_DELTA:
        return observation->reason_mask == 0 &&
               observation->base_snapshot_number > 0 &&
               (uint32_t)observation->base_snapshot_number <
                   observation->snapshot_number;
    case WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_KEYFRAME:
        return observation->reason_mask == 0 &&
               observation->base_snapshot_number <= 0;
    case WORR_SNAPSHOT_RECOVERY_OBSERVE_LEGACY_FAILURE:
    case WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_FAILURE:
    case WORR_SNAPSHOT_RECOVERY_OBSERVE_FORCE_REQUEST:
        return observation->reason_mask != 0;
    case WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_SUCCESS:
        return observation->reason_mask == 0;
    default:
        return false;
    }
}

static uint32_t arm_request(worr_snapshot_recovery_state_v1 *state,
                            uint32_t reason_mask)
{
    state->reason_mask |= reason_mask;
    if (state->active)
        return WORR_SNAPSHOT_RECOVERY_OK;
    if (state->request_generation == UINT32_MAX) {
        state->reason_mask = 0;
        state->exhausted = 1;
        return WORR_SNAPSHOT_RECOVERY_GENERATION_EXHAUSTED;
    }

    ++state->request_generation;
    state->active = 1;
    state->attempts_in_burst = 0;
    state->cooldown_remaining = 0;
    saturating_increment_u64(&state->request_arms);
    return WORR_SNAPSHOT_RECOVERY_OK;
}

static void initialize_decision(
    worr_snapshot_recovery_decision_v1 *decision)
{
    memset(decision, 0, sizeof(*decision));
    decision->struct_size = sizeof(*decision);
    decision->schema_version = WORR_SNAPSHOT_RECOVERY_VERSION;
}

void Worr_SnapshotRecoveryDefaultConfigV1(
    worr_snapshot_recovery_config_v1 *config_out)
{
    if (!config_out)
        return;
    memset(config_out, 0, sizeof(*config_out));
    config_out->struct_size = sizeof(*config_out);
    config_out->schema_version = WORR_SNAPSHOT_RECOVERY_VERSION;
    config_out->legacy_failure_threshold = 1;
    config_out->canonical_failure_threshold = 3;
    config_out->max_requests_per_burst = 3;
    config_out->cooldown_transmits = 2;
}

uint32_t Worr_SnapshotRecoveryResetV1(
    worr_snapshot_recovery_state_v1 *state)
{
    if (!state)
        return WORR_SNAPSHOT_RECOVERY_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    state->struct_size = sizeof(*state);
    state->schema_version = WORR_SNAPSHOT_RECOVERY_VERSION;
    return WORR_SNAPSHOT_RECOVERY_OK;
}

uint32_t Worr_SnapshotRecoveryObserveV1(
    worr_snapshot_recovery_state_v1 *state,
    const worr_snapshot_recovery_config_v1 *config,
    const worr_snapshot_recovery_observation_v1 *observation)
{
    uint32_t result = WORR_SNAPSHOT_RECOVERY_OK;

    if (!state || !config || !observation)
        return WORR_SNAPSHOT_RECOVERY_INVALID_ARGUMENT;
    if (!config_valid(config))
        return WORR_SNAPSHOT_RECOVERY_INVALID_CONFIG;
    if (!state_valid(state) || !state_matches_config(state, config))
        return WORR_SNAPSHOT_RECOVERY_INVALID_STATE;
    if (!observation_valid(observation))
        return WORR_SNAPSHOT_RECOVERY_INVALID_OBSERVATION;

    saturating_increment_u64(&state->observations);
    state->last_snapshot_number = observation->snapshot_number;
    state->last_base_snapshot_number = observation->base_snapshot_number;

    switch (observation->type) {
    case WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_DELTA:
        saturating_increment_u64(&state->accepted_delta_frames);
        state->legacy_failure_streak = 0;
        break;

    case WORR_SNAPSHOT_RECOVERY_OBSERVE_ACCEPTED_KEYFRAME:
        saturating_increment_u64(&state->accepted_keyframes);
        state->legacy_failure_streak = 0;
        state->canonical_failure_streak = 0;
        if (state->active)
            saturating_increment_u64(&state->recoveries);
        state->active = 0;
        state->reason_mask = 0;
        state->attempts_in_burst = 0;
        state->cooldown_remaining = 0;
        break;

    case WORR_SNAPSHOT_RECOVERY_OBSERVE_LEGACY_FAILURE:
        saturating_increment_u64(&state->legacy_failures);
        saturating_increment_u32(&state->legacy_failure_streak);
        if (state->legacy_failure_streak >=
            config->legacy_failure_threshold) {
            result = arm_request(state, observation->reason_mask);
        }
        break;

    case WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_SUCCESS:
        state->canonical_failure_streak = 0;
        break;

    case WORR_SNAPSHOT_RECOVERY_OBSERVE_CANONICAL_FAILURE:
        saturating_increment_u64(&state->canonical_failures);
        saturating_increment_u32(&state->canonical_failure_streak);
        if (state->canonical_failure_streak >=
            config->canonical_failure_threshold) {
            result = arm_request(state, observation->reason_mask);
        }
        break;

    case WORR_SNAPSHOT_RECOVERY_OBSERVE_FORCE_REQUEST:
        result = arm_request(state, observation->reason_mask);
        break;

    default:
        /* observation_valid() already excluded this path. */
        return WORR_SNAPSHOT_RECOVERY_INVALID_OBSERVATION;
    }

    return result;
}

uint32_t Worr_SnapshotRecoveryDecideV1(
    worr_snapshot_recovery_state_v1 *state,
    const worr_snapshot_recovery_config_v1 *config,
    worr_snapshot_recovery_decision_v1 *decision_out)
{
    if (!state || !config || !decision_out)
        return WORR_SNAPSHOT_RECOVERY_INVALID_ARGUMENT;
    initialize_decision(decision_out);
    if (!config_valid(config))
        return WORR_SNAPSHOT_RECOVERY_INVALID_CONFIG;
    if (!state_valid(state) || !state_matches_config(state, config))
        return WORR_SNAPSHOT_RECOVERY_INVALID_STATE;

    if (state->exhausted) {
        decision_out->flags = WORR_SNAPSHOT_RECOVERY_DECISION_EXHAUSTED;
        return WORR_SNAPSHOT_RECOVERY_OK;
    }
    if (!state->active)
        return WORR_SNAPSHOT_RECOVERY_OK;

    decision_out->flags = WORR_SNAPSHOT_RECOVERY_DECISION_ACTIVE;
    decision_out->reason_mask = state->reason_mask;
    decision_out->request_generation = state->request_generation;

    if (state->cooldown_remaining != 0) {
        --state->cooldown_remaining;
        decision_out->flags |= WORR_SNAPSHOT_RECOVERY_DECISION_COOLDOWN;
        decision_out->cooldown_remaining = state->cooldown_remaining;
        saturating_increment_u64(&state->cooldown_suppressions);
        return WORR_SNAPSHOT_RECOVERY_OK;
    }

    if (state->attempts_in_burst >= config->max_requests_per_burst) {
        state->attempts_in_burst = 0;
        if (config->cooldown_transmits != 0) {
            state->cooldown_remaining = config->cooldown_transmits - 1;
            decision_out->flags |=
                WORR_SNAPSHOT_RECOVERY_DECISION_COOLDOWN;
            decision_out->cooldown_remaining = state->cooldown_remaining;
            saturating_increment_u64(&state->cooldown_suppressions);
            return WORR_SNAPSHOT_RECOVERY_OK;
        }
    }

    ++state->attempts_in_burst;
    saturating_increment_u64(&state->request_decisions);
    decision_out->flags |=
        WORR_SNAPSHOT_RECOVERY_DECISION_REQUEST_FULL;
    decision_out->attempt_in_burst = state->attempts_in_burst;
    return WORR_SNAPSHOT_RECOVERY_OK;
}
