/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/adaptive_input.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define ADAPTIVE_INPUT_MAX_OBSERVED_RTT_MS UINT32_C(60000)
#define ADAPTIVE_INPUT_MAX_OBSERVED_DEPTH UINT32_C(1048576)
#define ADAPTIVE_INPUT_MAX_OBSERVED_RATE UINT32_C(1073741824)
#define ADAPTIVE_INPUT_MAX_CONFIGURED_PPS UINT32_C(1000)
#define ADAPTIVE_INPUT_MAX_REDUNDANCY UINT32_C(32)
#define ADAPTIVE_INPUT_MIN_PPS UINT32_C(10)

#define ADAPTIVE_INPUT_OBSERVATION_FLAGS                              \
    ((uint32_t)WORR_ADAPTIVE_INPUT_OBSERVATION_BATCH_REDUNDANCY)

#define ADAPTIVE_INPUT_PROTECTIVE_REASONS                             \
    ((uint32_t)(WORR_ADAPTIVE_INPUT_REASON_LOSS_ELEVATED |            \
                WORR_ADAPTIVE_INPUT_REASON_LOSS_SEVERE |              \
                WORR_ADAPTIVE_INPUT_REASON_RTT_HIGH |                 \
                WORR_ADAPTIVE_INPUT_REASON_JITTER_HIGH |              \
                WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE |            \
                WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE_CRITICAL |   \
                WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG |              \
                WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG_CRITICAL))

static uint32_t min_u32(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static uint32_t max_u32(uint32_t left, uint32_t right)
{
    return left > right ? left : right;
}

static uint32_t clamp_u64_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static uint64_t saturating_deadline(uint64_t now, uint32_t delay)
{
    if ((uint64_t)delay > UINT64_MAX - now)
        return UINT64_MAX;
    return now + (uint64_t)delay;
}

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static void saturating_add(uint64_t *value, uint64_t amount)
{
    if (amount > UINT64_MAX - *value)
        *value = UINT64_MAX;
    else
        *value += amount;
}

static uint32_t q8_round(uint32_t value)
{
    return (uint32_t)(((uint64_t)value + UINT64_C(128)) >> 8);
}

static uint32_t q8_ewma(uint32_t previous, uint32_t sample)
{
    const uint64_t sum = (uint64_t)previous * UINT64_C(3) +
                         (uint64_t)sample * UINT64_C(256);
    return (uint32_t)((sum + UINT64_C(2)) / UINT64_C(4));
}

static uint32_t loss_basis_points(uint64_t received, uint64_t dropped)
{
    uint64_t total;

    if (dropped > UINT64_MAX - received)
        total = UINT64_MAX;
    else
        total = received + dropped;
    if (total == 0)
        return 0;

    /* A normal evaluation window contains only a handful of packets.  The
     * reduction keeps deliberately hostile 64-bit counters safe without
     * relying on compiler-specific 128-bit arithmetic. */
    while (total > UINT32_MAX) {
        received = (received >> 1) + (received & UINT64_C(1));
        dropped = (dropped >> 1) + (dropped & UINT64_C(1));
        if (dropped > UINT64_MAX - received)
            total = UINT64_MAX;
        else
            total = received + dropped;
    }
    if (dropped >= total)
        return UINT32_C(10000);
    return (uint32_t)((dropped * UINT64_C(10000)) / total);
}

static uint32_t quantized_send_interval(uint32_t packets_per_second)
{
    uint32_t interval;
    uint32_t buckets;

    if (packets_per_second == 0)
        return 0;
    interval = UINT32_C(1000) / packets_per_second;
    if (interval == 0)
        interval = 1;
    buckets = UINT32_C(100) / interval;
    if (buckets != 0)
        interval = UINT32_C(100) / buckets;
    return max_u32(interval, 1);
}

static bool config_valid(const worr_adaptive_input_config_v1 *config)
{
    return config && config->struct_size == sizeof(*config) &&
           config->schema_version == WORR_ADAPTIVE_INPUT_VERSION &&
           config->evaluation_interval_ms >= 10 &&
           config->evaluation_interval_ms <= 5000 &&
           config->recovery_hold_ms <= 60000 &&
           config->minimum_loss_sample_packets >= 1 &&
           config->minimum_loss_sample_packets <= 10000 &&
           config->elevated_loss_basis_points >= 1 &&
           config->elevated_loss_basis_points <=
               config->severe_loss_basis_points &&
           config->severe_loss_basis_points <= 10000 &&
           config->high_rtt_ms >= 1 &&
           config->high_rtt_ms <= ADAPTIVE_INPUT_MAX_OBSERVED_RTT_MS &&
           config->high_jitter_ms >= 1 &&
           config->high_jitter_ms <= ADAPTIVE_INPUT_MAX_OBSERVED_RTT_MS &&
           config->queue_pressure_commands >= 1 &&
           config->queue_pressure_commands <
               config->queue_critical_commands &&
           config->ack_backlog_packets >= 1 &&
           config->ack_backlog_packets < config->ack_critical_packets &&
           config->critical_rate_bytes_per_second >= 1 &&
           config->critical_rate_bytes_per_second <=
               config->constrained_rate_bytes_per_second &&
           config->constrained_rate_bytes_per_second <=
               ADAPTIVE_INPUT_MAX_OBSERVED_RATE &&
           config->maximum_packets_per_second >= ADAPTIVE_INPUT_MIN_PPS &&
           config->maximum_packets_per_second <=
               ADAPTIVE_INPUT_MAX_CONFIGURED_PPS &&
           config->pressure_packets_per_second >= ADAPTIVE_INPUT_MIN_PPS &&
           config->pressure_packets_per_second <=
               config->critical_packets_per_second &&
           config->critical_packets_per_second <=
               config->maximum_packets_per_second &&
           config->reserved[0] == 0 && config->reserved[1] == 0;
}

static bool observation_valid(
    const worr_adaptive_input_observation_v1 *observation)
{
    return observation &&
           observation->struct_size == sizeof(*observation) &&
           observation->schema_version == WORR_ADAPTIVE_INPUT_VERSION &&
           observation->smoothed_rtt_ms <=
               ADAPTIVE_INPUT_MAX_OBSERVED_RTT_MS &&
           observation->queued_commands <=
               ADAPTIVE_INPUT_MAX_OBSERVED_DEPTH &&
           observation->unacknowledged_packets <=
               ADAPTIVE_INPUT_MAX_OBSERVED_DEPTH &&
           observation->rate_bytes_per_second <=
               ADAPTIVE_INPUT_MAX_OBSERVED_RATE &&
           observation->configured_redundancy_frames <=
               ADAPTIVE_INPUT_MAX_REDUNDANCY &&
           observation->maximum_redundancy_frames <=
               ADAPTIVE_INPUT_MAX_REDUNDANCY &&
           (observation->flags & ~ADAPTIVE_INPUT_OBSERVATION_FLAGS) == 0 &&
           observation->reserved0 == 0;
}

static bool state_valid(const worr_adaptive_input_state_v1 *state)
{
    return state && state->struct_size == sizeof(*state) &&
           state->schema_version == WORR_ADAPTIVE_INPUT_VERSION &&
           state->initialized <= 1 && state->loss_sample_ready <= 1 &&
           state->current_packets_per_second <=
               ADAPTIVE_INPUT_MAX_CONFIGURED_PPS &&
           state->current_redundancy_frames <=
               ADAPTIVE_INPUT_MAX_REDUNDANCY &&
           state->reserved0 == 0;
}

static void initialize_output(worr_adaptive_input_output_v1 *output,
                              uint32_t result)
{
    memset(output, 0, sizeof(*output));
    output->struct_size = sizeof(*output);
    output->schema_version = WORR_ADAPTIVE_INPUT_VERSION;
    output->result = result;
}

static uint32_t normalized_packets_per_second(
    const worr_adaptive_input_config_v1 *config,
    const worr_adaptive_input_observation_v1 *observation,
    uint32_t *reason_mask)
{
    uint32_t packets_per_second =
        observation->configured_packets_per_second;

    if (packets_per_second == 0) {
        *reason_mask |= WORR_ADAPTIVE_INPUT_REASON_NO_PACING_LIMIT;
        return 0;
    }
    if (packets_per_second < ADAPTIVE_INPUT_MIN_PPS) {
        packets_per_second = ADAPTIVE_INPUT_MIN_PPS;
        *reason_mask |= WORR_ADAPTIVE_INPUT_REASON_PACING_CLAMPED;
    }
    if (packets_per_second > config->maximum_packets_per_second) {
        packets_per_second = config->maximum_packets_per_second;
        *reason_mask |= WORR_ADAPTIVE_INPUT_REASON_PACING_CLAMPED;
    }
    return packets_per_second;
}

static void populate_output(
    const worr_adaptive_input_state_v1 *state,
    const worr_adaptive_input_observation_v1 *observation,
    uint32_t result, uint32_t window_loss_basis_points,
    uint64_t window_received, uint64_t window_dropped,
    worr_adaptive_input_output_v1 *output)
{
    initialize_output(output, result);
    output->reason_mask = state->current_reason_mask;
    output->decision_serial = state->decision_serial;
    output->evaluated_at_ms = state->last_sample_time_ms;
    output->packets_per_second = state->current_packets_per_second;
    output->redundancy_frames = state->current_redundancy_frames;
    output->send_interval_ms =
        quantized_send_interval(state->current_packets_per_second);
    output->window_loss_basis_points = window_loss_basis_points;
    output->smoothed_loss_basis_points =
        q8_round(state->smoothed_loss_q8);
    output->smoothed_rtt_ms = q8_round(state->smoothed_rtt_q8);
    output->smoothed_jitter_ms = q8_round(state->smoothed_jitter_q8);
    output->window_received_packets = clamp_u64_u32(window_received);
    output->window_dropped_packets = clamp_u64_u32(window_dropped);
    output->queued_commands = observation->queued_commands;
    output->unacknowledged_packets =
        observation->unacknowledged_packets;
    output->rate_bytes_per_second = observation->rate_bytes_per_second;
    output->flags = state->current_output_flags |
                    WORR_ADAPTIVE_INPUT_OUTPUT_VALID;
}

static void decide(
    worr_adaptive_input_state_v1 *state,
    const worr_adaptive_input_config_v1 *config,
    const worr_adaptive_input_observation_v1 *observation,
    uint32_t initial_reasons, bool cold_start)
{
    uint32_t reasons = initial_reasons;
    uint32_t output_flags = WORR_ADAPTIVE_INPUT_OUTPUT_WINDOW_EVALUATED;
    uint32_t packets_per_second;
    uint32_t redundancy_frames;
    uint32_t rate_cap = UINT32_MAX;
    uint32_t configured_redundancy =
        observation->configured_redundancy_frames;
    const uint32_t loss = q8_round(state->smoothed_loss_q8);
    const uint32_t rtt = q8_round(state->smoothed_rtt_q8);
    const uint32_t jitter = q8_round(state->smoothed_jitter_q8);
    bool recovery_held = false;

    if (state->loss_sample_ready) {
        if (loss >= config->severe_loss_basis_points)
            reasons |= WORR_ADAPTIVE_INPUT_REASON_LOSS_SEVERE |
                       WORR_ADAPTIVE_INPUT_REASON_LOSS_ELEVATED;
        else if (loss >= config->elevated_loss_basis_points)
            reasons |= WORR_ADAPTIVE_INPUT_REASON_LOSS_ELEVATED;
    }
    if (rtt >= config->high_rtt_ms)
        reasons |= WORR_ADAPTIVE_INPUT_REASON_RTT_HIGH;
    if (jitter >= config->high_jitter_ms)
        reasons |= WORR_ADAPTIVE_INPUT_REASON_JITTER_HIGH;
    if (observation->queued_commands >= config->queue_critical_commands)
        reasons |= WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE_CRITICAL |
                   WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE;
    else if (observation->queued_commands >=
             config->queue_pressure_commands)
        reasons |= WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE;
    if (observation->unacknowledged_packets >=
        config->ack_critical_packets)
        reasons |= WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG_CRITICAL |
                   WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG;
    else if (observation->unacknowledged_packets >=
             config->ack_backlog_packets)
        reasons |= WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG;

    if (observation->rate_bytes_per_second != 0 &&
        observation->rate_bytes_per_second <=
            config->critical_rate_bytes_per_second) {
        reasons |= WORR_ADAPTIVE_INPUT_REASON_RATE_CRITICAL |
                   WORR_ADAPTIVE_INPUT_REASON_RATE_CONSTRAINED;
        rate_cap = max_u32(
            ADAPTIVE_INPUT_MIN_PPS,
            (config->pressure_packets_per_second * UINT32_C(2)) /
                UINT32_C(5));
        output_flags |= WORR_ADAPTIVE_INPUT_OUTPUT_RATE_CAPPED;
    } else if (observation->rate_bytes_per_second != 0 &&
               observation->rate_bytes_per_second <=
                   config->constrained_rate_bytes_per_second) {
        reasons |= WORR_ADAPTIVE_INPUT_REASON_RATE_CONSTRAINED;
        rate_cap = max_u32(
            ADAPTIVE_INPUT_MIN_PPS,
            (config->pressure_packets_per_second * UINT32_C(3)) /
                UINT32_C(5));
        output_flags |= WORR_ADAPTIVE_INPUT_OUTPUT_RATE_CAPPED;
    }

    packets_per_second = normalized_packets_per_second(
        config, observation, &reasons);
    if (packets_per_second != 0) {
        if (reasons &
            (WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE_CRITICAL |
             WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG_CRITICAL)) {
            packets_per_second = max_u32(
                packets_per_second,
                config->critical_packets_per_second);
        } else if (reasons & ADAPTIVE_INPUT_PROTECTIVE_REASONS) {
            packets_per_second = max_u32(
                packets_per_second,
                config->pressure_packets_per_second);
        }
        packets_per_second = min_u32(
            packets_per_second, config->maximum_packets_per_second);
        packets_per_second = min_u32(packets_per_second, rate_cap);
    }

    if (!(observation->flags &
          WORR_ADAPTIVE_INPUT_OBSERVATION_BATCH_REDUNDANCY)) {
        redundancy_frames = 0;
        reasons |= WORR_ADAPTIVE_INPUT_REASON_NO_BATCH_REDUNDANCY;
    } else {
        output_flags |= WORR_ADAPTIVE_INPUT_OUTPUT_REDUNDANCY_AVAILABLE;
        if (configured_redundancy >
            observation->maximum_redundancy_frames) {
            configured_redundancy =
                observation->maximum_redundancy_frames;
            reasons |= WORR_ADAPTIVE_INPUT_REASON_REDUNDANCY_CLAMPED;
        }
        if (cold_start || !state->loss_sample_ready)
            redundancy_frames = configured_redundancy;
        else if (reasons &
                 (WORR_ADAPTIVE_INPUT_REASON_LOSS_SEVERE |
                  WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE_CRITICAL |
                  WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG_CRITICAL))
            redundancy_frames = max_u32(configured_redundancy, 2);
        else if (reasons & ADAPTIVE_INPUT_PROTECTIVE_REASONS)
            redundancy_frames = max_u32(configured_redundancy, 1);
        else
            /* Downstream receive loss cannot prove an asymmetric upstream is
             * loss-free. Preserve the operator's baseline until a future
             * transport supplies explicit consumed-command recovery evidence. */
            redundancy_frames = configured_redundancy;

        redundancy_frames = min_u32(
            redundancy_frames, observation->maximum_redundancy_frames);
        if (reasons & WORR_ADAPTIVE_INPUT_REASON_RATE_CRITICAL)
            redundancy_frames = 0;
        else if (reasons & WORR_ADAPTIVE_INPUT_REASON_RATE_CONSTRAINED)
            redundancy_frames = min_u32(redundancy_frames, 1);
    }

    if (reasons & ADAPTIVE_INPUT_PROTECTIVE_REASONS) {
        output_flags |= WORR_ADAPTIVE_INPUT_OUTPUT_PROTECTIVE;
        state->recovery_until_ms = saturating_deadline(
            observation->sample_time_ms, config->recovery_hold_ms);
    } else if (state->initialized &&
               observation->sample_time_ms < state->recovery_until_ms) {
        if (!(reasons & WORR_ADAPTIVE_INPUT_REASON_RATE_CONSTRAINED)) {
            /* A configured zero is an explicit request for no pacing limit,
             * not a low numeric tier that hysteresis may replace. */
            if (packets_per_second != 0)
                packets_per_second = max_u32(
                    packets_per_second,
                    state->current_packets_per_second);
            redundancy_frames = max_u32(
                redundancy_frames, state->current_redundancy_frames);
            redundancy_frames = min_u32(
                redundancy_frames,
                observation->maximum_redundancy_frames);
        }
        reasons |= WORR_ADAPTIVE_INPUT_REASON_RECOVERY_HOLD;
        output_flags |= WORR_ADAPTIVE_INPUT_OUTPUT_RECOVERY_HELD;
        recovery_held = true;
    }

    state->current_packets_per_second = packets_per_second;
    state->current_redundancy_frames = redundancy_frames;
    state->current_reason_mask = reasons;
    state->current_output_flags = output_flags;
    saturating_increment(&state->decision_serial);
    if (recovery_held)
        saturating_increment(&state->recovery_holds);
}

void Worr_AdaptiveInputDefaultConfigV1(
    worr_adaptive_input_config_v1 *config_out)
{
    if (!config_out)
        return;
    memset(config_out, 0, sizeof(*config_out));
    config_out->struct_size = sizeof(*config_out);
    config_out->schema_version = WORR_ADAPTIVE_INPUT_VERSION;
    config_out->evaluation_interval_ms = 100;
    config_out->recovery_hold_ms = 1000;
    config_out->minimum_loss_sample_packets = 4;
    config_out->elevated_loss_basis_points = 200;
    config_out->severe_loss_basis_points = 800;
    config_out->high_rtt_ms = 180;
    config_out->high_jitter_ms = 25;
    config_out->queue_pressure_commands = 3;
    config_out->queue_critical_commands = 7;
    config_out->ack_backlog_packets = 8;
    config_out->ack_critical_packets = 24;
    config_out->constrained_rate_bytes_per_second = 8000;
    config_out->critical_rate_bytes_per_second = 4000;
    config_out->maximum_packets_per_second = 125;
    config_out->pressure_packets_per_second = 75;
    config_out->critical_packets_per_second = 90;
}

uint32_t Worr_AdaptiveInputResetV1(worr_adaptive_input_state_v1 *state)
{
    if (!state)
        return WORR_ADAPTIVE_INPUT_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    state->struct_size = sizeof(*state);
    state->schema_version = WORR_ADAPTIVE_INPUT_VERSION;
    return WORR_ADAPTIVE_INPUT_OK;
}

uint32_t Worr_AdaptiveInputEvaluateV1(
    worr_adaptive_input_state_v1 *state,
    const worr_adaptive_input_config_v1 *config,
    const worr_adaptive_input_observation_v1 *observation,
    worr_adaptive_input_output_v1 *output)
{
    uint64_t window_received = 0;
    uint64_t window_dropped = 0;
    uint32_t window_loss = 0;
    uint32_t result = WORR_ADAPTIVE_INPUT_OK;
    uint32_t initial_reasons = 0;
    bool cold_start = false;

    if (!output)
        return WORR_ADAPTIVE_INPUT_INVALID_ARGUMENT;
    initialize_output(output, WORR_ADAPTIVE_INPUT_INVALID_ARGUMENT);
    if (!state)
        return WORR_ADAPTIVE_INPUT_INVALID_ARGUMENT;
    if (!state_valid(state)) {
        output->result = WORR_ADAPTIVE_INPUT_INVALID_STATE;
        return WORR_ADAPTIVE_INPUT_INVALID_STATE;
    }
    saturating_increment(&state->evaluate_calls);
    if (!config_valid(config)) {
        output->result = WORR_ADAPTIVE_INPUT_INVALID_CONFIG;
        return WORR_ADAPTIVE_INPUT_INVALID_CONFIG;
    }
    if (!observation_valid(observation)) {
        saturating_increment(&state->invalid_observations);
        output->result = WORR_ADAPTIVE_INPUT_INVALID_OBSERVATION;
        return WORR_ADAPTIVE_INPUT_INVALID_OBSERVATION;
    }

    if (!state->initialized) {
        state->initialized = 1;
        state->last_sample_time_ms = observation->sample_time_ms;
        state->next_evaluation_time_ms = saturating_deadline(
            observation->sample_time_ms,
            config->evaluation_interval_ms);
        state->last_received_packets =
            observation->total_received_packets;
        state->last_dropped_packets = observation->total_dropped_packets;
        state->smoothed_rtt_q8 =
            observation->smoothed_rtt_ms * UINT32_C(256);
        state->smoothed_jitter_q8 = 0;
        state->smoothed_loss_q8 = 0;
        initial_reasons = WORR_ADAPTIVE_INPUT_REASON_COLD_START |
                          WORR_ADAPTIVE_INPUT_REASON_INSUFFICIENT_LOSS_SAMPLE;
        cold_start = true;
    } else if (observation->sample_time_ms < state->last_sample_time_ms) {
        saturating_increment(&state->clock_resets);
        state->last_sample_time_ms = observation->sample_time_ms;
        state->next_evaluation_time_ms = saturating_deadline(
            observation->sample_time_ms,
            config->evaluation_interval_ms);
        state->recovery_until_ms = 0;
        state->last_received_packets =
            observation->total_received_packets;
        state->last_dropped_packets = observation->total_dropped_packets;
        state->loss_sample_ready = 0;
        state->smoothed_loss_q8 = 0;
        state->smoothed_rtt_q8 =
            observation->smoothed_rtt_ms * UINT32_C(256);
        state->smoothed_jitter_q8 = 0;
        initial_reasons = WORR_ADAPTIVE_INPUT_REASON_CLOCK_RESET |
                          WORR_ADAPTIVE_INPUT_REASON_COLD_START |
                          WORR_ADAPTIVE_INPUT_REASON_INSUFFICIENT_LOSS_SAMPLE;
        result = WORR_ADAPTIVE_INPUT_CLOCK_RESET;
        cold_start = true;
    } else if (observation->total_received_packets <
                   state->last_received_packets ||
               observation->total_dropped_packets <
                   state->last_dropped_packets) {
        saturating_increment(&state->counter_resets);
        state->last_sample_time_ms = observation->sample_time_ms;
        state->next_evaluation_time_ms = saturating_deadline(
            observation->sample_time_ms,
            config->evaluation_interval_ms);
        state->recovery_until_ms = 0;
        state->last_received_packets =
            observation->total_received_packets;
        state->last_dropped_packets = observation->total_dropped_packets;
        state->loss_sample_ready = 0;
        state->smoothed_loss_q8 = 0;
        state->smoothed_rtt_q8 =
            observation->smoothed_rtt_ms * UINT32_C(256);
        state->smoothed_jitter_q8 = 0;
        initial_reasons = WORR_ADAPTIVE_INPUT_REASON_COUNTER_RESET |
                          WORR_ADAPTIVE_INPUT_REASON_COLD_START |
                          WORR_ADAPTIVE_INPUT_REASON_INSUFFICIENT_LOSS_SAMPLE;
        result = WORR_ADAPTIVE_INPUT_COUNTER_RESET;
        cold_start = true;
    } else if (observation->sample_time_ms <
               state->next_evaluation_time_ms) {
        saturating_increment(&state->held_evaluations);
        populate_output(state, observation, WORR_ADAPTIVE_INPUT_HELD,
                        0, 0, 0, output);
        return WORR_ADAPTIVE_INPUT_HELD;
    } else {
        const uint32_t previous_rtt = q8_round(state->smoothed_rtt_q8);
        const uint32_t jitter_sample =
            previous_rtt > observation->smoothed_rtt_ms
                ? previous_rtt - observation->smoothed_rtt_ms
                : observation->smoothed_rtt_ms - previous_rtt;
        uint64_t sample_packets;

        window_received = observation->total_received_packets -
                          state->last_received_packets;
        window_dropped = observation->total_dropped_packets -
                         state->last_dropped_packets;
        state->last_sample_time_ms = observation->sample_time_ms;
        state->next_evaluation_time_ms = saturating_deadline(
            observation->sample_time_ms,
            config->evaluation_interval_ms);
        state->smoothed_rtt_q8 = q8_ewma(
            state->smoothed_rtt_q8, observation->smoothed_rtt_ms);
        state->smoothed_jitter_q8 = q8_ewma(
            state->smoothed_jitter_q8, jitter_sample);

        if (window_dropped > UINT64_MAX - window_received)
            sample_packets = UINT64_MAX;
        else
            sample_packets = window_received + window_dropped;
        if (sample_packets >= config->minimum_loss_sample_packets) {
            window_loss = loss_basis_points(
                window_received, window_dropped);
            /* Commit the loss-counter baseline only when the complete sample
             * is large enough. Smaller evaluation slices accumulate instead
             * of being discarded forever on low packet-rate links. */
            state->last_received_packets =
                observation->total_received_packets;
            state->last_dropped_packets =
                observation->total_dropped_packets;
            if (state->loss_sample_ready)
                state->smoothed_loss_q8 = q8_ewma(
                    state->smoothed_loss_q8, window_loss);
            else {
                state->smoothed_loss_q8 = window_loss * UINT32_C(256);
                state->loss_sample_ready = 1;
            }
            saturating_add(&state->cumulative_window_received,
                           window_received);
            saturating_add(&state->cumulative_window_dropped,
                           window_dropped);
        } else {
            initial_reasons |=
                WORR_ADAPTIVE_INPUT_REASON_INSUFFICIENT_LOSS_SAMPLE;
        }
    }

    saturating_increment(&state->window_evaluations);
    decide(state, config, observation, initial_reasons, cold_start);
    populate_output(state, observation, result, window_loss,
                    window_received, window_dropped, output);
    return result;
}

uint32_t Worr_AdaptiveInputGetTelemetryV1(
    const worr_adaptive_input_state_v1 *state,
    worr_adaptive_input_telemetry_v1 *telemetry_out)
{
    if (!telemetry_out)
        return WORR_ADAPTIVE_INPUT_INVALID_ARGUMENT;
    memset(telemetry_out, 0, sizeof(*telemetry_out));
    telemetry_out->struct_size = sizeof(*telemetry_out);
    telemetry_out->schema_version = WORR_ADAPTIVE_INPUT_VERSION;
    if (!state_valid(state))
        return WORR_ADAPTIVE_INPUT_INVALID_STATE;

    telemetry_out->current_reason_mask = state->current_reason_mask;
    telemetry_out->current_output_flags = state->current_output_flags;
    telemetry_out->decision_serial = state->decision_serial;
    telemetry_out->evaluate_calls = state->evaluate_calls;
    telemetry_out->window_evaluations = state->window_evaluations;
    telemetry_out->held_evaluations = state->held_evaluations;
    telemetry_out->recovery_holds = state->recovery_holds;
    telemetry_out->invalid_observations = state->invalid_observations;
    telemetry_out->counter_resets = state->counter_resets;
    telemetry_out->clock_resets = state->clock_resets;
    telemetry_out->cumulative_window_received =
        state->cumulative_window_received;
    telemetry_out->cumulative_window_dropped =
        state->cumulative_window_dropped;
    telemetry_out->current_packets_per_second =
        state->current_packets_per_second;
    telemetry_out->current_redundancy_frames =
        state->current_redundancy_frames;
    telemetry_out->smoothed_loss_basis_points =
        q8_round(state->smoothed_loss_q8);
    telemetry_out->smoothed_rtt_ms = q8_round(state->smoothed_rtt_q8);
    telemetry_out->smoothed_jitter_ms =
        q8_round(state->smoothed_jitter_q8);
    return WORR_ADAPTIVE_INPUT_OK;
}
