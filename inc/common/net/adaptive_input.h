/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_ADAPTIVE_INPUT_VERSION UINT32_C(1)

/* All public records in this API are pointer-free and use fixed-width integer
 * fields.  They can therefore be copied into later FR-10-T14 telemetry
 * records without retaining engine or transport storage. */

enum worr_adaptive_input_result_v1_e {
    WORR_ADAPTIVE_INPUT_OK = 0,
    WORR_ADAPTIVE_INPUT_HELD = 1,
    WORR_ADAPTIVE_INPUT_COUNTER_RESET = 2,
    WORR_ADAPTIVE_INPUT_CLOCK_RESET = 3,
    WORR_ADAPTIVE_INPUT_INVALID_ARGUMENT = 4,
    WORR_ADAPTIVE_INPUT_INVALID_CONFIG = 5,
    WORR_ADAPTIVE_INPUT_INVALID_OBSERVATION = 6,
    WORR_ADAPTIVE_INPUT_INVALID_STATE = 7,
};

enum worr_adaptive_input_observation_flags_v1_e {
    WORR_ADAPTIVE_INPUT_OBSERVATION_BATCH_REDUNDANCY = UINT32_C(1) << 0,
};

enum worr_adaptive_input_reason_v1_e {
    WORR_ADAPTIVE_INPUT_REASON_COLD_START = UINT32_C(1) << 0,
    WORR_ADAPTIVE_INPUT_REASON_LOSS_ELEVATED = UINT32_C(1) << 1,
    WORR_ADAPTIVE_INPUT_REASON_LOSS_SEVERE = UINT32_C(1) << 2,
    WORR_ADAPTIVE_INPUT_REASON_RTT_HIGH = UINT32_C(1) << 3,
    WORR_ADAPTIVE_INPUT_REASON_JITTER_HIGH = UINT32_C(1) << 4,
    WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE = UINT32_C(1) << 5,
    WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE_CRITICAL = UINT32_C(1) << 6,
    WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG = UINT32_C(1) << 7,
    WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG_CRITICAL = UINT32_C(1) << 8,
    WORR_ADAPTIVE_INPUT_REASON_RATE_CONSTRAINED = UINT32_C(1) << 9,
    WORR_ADAPTIVE_INPUT_REASON_RATE_CRITICAL = UINT32_C(1) << 10,
    WORR_ADAPTIVE_INPUT_REASON_RECOVERY_HOLD = UINT32_C(1) << 11,
    WORR_ADAPTIVE_INPUT_REASON_COUNTER_RESET = UINT32_C(1) << 12,
    WORR_ADAPTIVE_INPUT_REASON_CLOCK_RESET = UINT32_C(1) << 13,
    WORR_ADAPTIVE_INPUT_REASON_PACING_CLAMPED = UINT32_C(1) << 14,
    WORR_ADAPTIVE_INPUT_REASON_REDUNDANCY_CLAMPED = UINT32_C(1) << 15,
    WORR_ADAPTIVE_INPUT_REASON_NO_BATCH_REDUNDANCY = UINT32_C(1) << 16,
    WORR_ADAPTIVE_INPUT_REASON_NO_PACING_LIMIT = UINT32_C(1) << 17,
    WORR_ADAPTIVE_INPUT_REASON_INSUFFICIENT_LOSS_SAMPLE = UINT32_C(1) << 18,
};

enum worr_adaptive_input_output_flags_v1_e {
    WORR_ADAPTIVE_INPUT_OUTPUT_VALID = UINT32_C(1) << 0,
    WORR_ADAPTIVE_INPUT_OUTPUT_WINDOW_EVALUATED = UINT32_C(1) << 1,
    WORR_ADAPTIVE_INPUT_OUTPUT_PROTECTIVE = UINT32_C(1) << 2,
    WORR_ADAPTIVE_INPUT_OUTPUT_RECOVERY_HELD = UINT32_C(1) << 3,
    WORR_ADAPTIVE_INPUT_OUTPUT_RATE_CAPPED = UINT32_C(1) << 4,
    WORR_ADAPTIVE_INPUT_OUTPUT_REDUNDANCY_AVAILABLE = UINT32_C(1) << 5,
};

typedef struct worr_adaptive_input_config_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t evaluation_interval_ms;
    uint32_t recovery_hold_ms;
    uint32_t minimum_loss_sample_packets;
    uint32_t elevated_loss_basis_points;
    uint32_t severe_loss_basis_points;
    uint32_t high_rtt_ms;
    uint32_t high_jitter_ms;
    uint32_t queue_pressure_commands;
    uint32_t queue_critical_commands;
    uint32_t ack_backlog_packets;
    uint32_t ack_critical_packets;
    uint32_t constrained_rate_bytes_per_second;
    uint32_t critical_rate_bytes_per_second;
    uint32_t maximum_packets_per_second;
    uint32_t pressure_packets_per_second;
    uint32_t critical_packets_per_second;
    uint32_t reserved[2];
} worr_adaptive_input_config_v1;

typedef struct worr_adaptive_input_observation_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t sample_time_ms;
    /* Disjoint cumulative counters for successfully received and inferred-lost
     * packets. They are monotonic within one transport session; rollback is an
     * explicit reconnect/counter-wrap rebaseline, never unsigned loss. */
    uint64_t total_received_packets;
    uint64_t total_dropped_packets;
    uint32_t smoothed_rtt_ms;
    uint32_t queued_commands;
    uint32_t unacknowledged_packets;
    uint32_t rate_bytes_per_second;
    uint32_t configured_packets_per_second;
    uint32_t configured_redundancy_frames;
    uint32_t maximum_redundancy_frames;
    uint32_t flags;
    uint32_t reserved0;
} worr_adaptive_input_observation_v1;

typedef struct worr_adaptive_input_output_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t result;
    uint32_t reason_mask;
    uint64_t decision_serial;
    uint64_t evaluated_at_ms;
    uint32_t packets_per_second;
    uint32_t redundancy_frames;
    uint32_t send_interval_ms;
    uint32_t window_loss_basis_points;
    uint32_t smoothed_loss_basis_points;
    uint32_t smoothed_rtt_ms;
    uint32_t smoothed_jitter_ms;
    uint32_t window_received_packets;
    uint32_t window_dropped_packets;
    uint32_t queued_commands;
    uint32_t unacknowledged_packets;
    uint32_t rate_bytes_per_second;
    uint32_t flags;
    uint32_t reserved0;
} worr_adaptive_input_output_v1;

typedef struct worr_adaptive_input_state_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t initialized;
    uint32_t loss_sample_ready;
    uint64_t last_sample_time_ms;
    uint64_t next_evaluation_time_ms;
    uint64_t recovery_until_ms;
    uint64_t last_received_packets;
    uint64_t last_dropped_packets;
    uint32_t smoothed_loss_q8;
    uint32_t smoothed_rtt_q8;
    uint32_t smoothed_jitter_q8;
    uint32_t current_packets_per_second;
    uint32_t current_redundancy_frames;
    uint32_t current_reason_mask;
    uint32_t current_output_flags;
    uint32_t reserved0;
    uint64_t decision_serial;
    uint64_t evaluate_calls;
    uint64_t window_evaluations;
    uint64_t held_evaluations;
    uint64_t recovery_holds;
    uint64_t invalid_observations;
    uint64_t counter_resets;
    uint64_t clock_resets;
    uint64_t cumulative_window_received;
    uint64_t cumulative_window_dropped;
} worr_adaptive_input_state_v1;

typedef struct worr_adaptive_input_telemetry_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t current_reason_mask;
    uint32_t current_output_flags;
    uint64_t decision_serial;
    uint64_t evaluate_calls;
    uint64_t window_evaluations;
    uint64_t held_evaluations;
    uint64_t recovery_holds;
    uint64_t invalid_observations;
    uint64_t counter_resets;
    uint64_t clock_resets;
    uint64_t cumulative_window_received;
    uint64_t cumulative_window_dropped;
    uint32_t current_packets_per_second;
    uint32_t current_redundancy_frames;
    uint32_t smoothed_loss_basis_points;
    uint32_t smoothed_rtt_ms;
    uint32_t smoothed_jitter_ms;
    uint32_t reserved0;
} worr_adaptive_input_telemetry_v1;

void Worr_AdaptiveInputDefaultConfigV1(
    worr_adaptive_input_config_v1 *config_out);

uint32_t Worr_AdaptiveInputResetV1(worr_adaptive_input_state_v1 *state);

uint32_t Worr_AdaptiveInputEvaluateV1(
    worr_adaptive_input_state_v1 *state,
    const worr_adaptive_input_config_v1 *config,
    const worr_adaptive_input_observation_v1 *observation,
    worr_adaptive_input_output_v1 *output);

uint32_t Worr_AdaptiveInputGetTelemetryV1(
    const worr_adaptive_input_state_v1 *state,
    worr_adaptive_input_telemetry_v1 *telemetry_out);

#ifdef __cplusplus
}
#endif
