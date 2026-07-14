/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/adaptive_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                \
    do {                                                                 \
        if (!(expression)) {                                             \
            fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,    \
                    __LINE__, #expression);                              \
            exit(1);                                                     \
        }                                                                \
    } while (0)

static worr_adaptive_input_observation_v1 observation(
    uint64_t time_ms, uint64_t received, uint64_t dropped)
{
    worr_adaptive_input_observation_v1 value;

    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.schema_version = WORR_ADAPTIVE_INPUT_VERSION;
    value.sample_time_ms = time_ms;
    value.total_received_packets = received;
    value.total_dropped_packets = dropped;
    value.smoothed_rtt_ms = 50;
    value.unacknowledged_packets = 1;
    value.rate_bytes_per_second = 15000;
    value.configured_packets_per_second = 60;
    value.configured_redundancy_frames = 1;
    value.maximum_redundancy_frames = 3;
    value.flags = WORR_ADAPTIVE_INPUT_OBSERVATION_BATCH_REDUNDANCY;
    return value;
}

static void test_stable_and_loss_policy(void)
{
    worr_adaptive_input_config_v1 config;
    worr_adaptive_input_state_v1 state;
    worr_adaptive_input_output_v1 output;
    worr_adaptive_input_observation_v1 sample;
    worr_adaptive_input_telemetry_v1 telemetry;

    Worr_AdaptiveInputDefaultConfigV1(&config);
    CHECK(Worr_AdaptiveInputResetV1(&state) == WORR_ADAPTIVE_INPUT_OK);

    sample = observation(0, 100, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_COLD_START);
    CHECK(output.packets_per_second == 60);
    CHECK(output.send_interval_ms == 16);
    CHECK(output.redundancy_frames == 1);

    sample = observation(50, 102, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_HELD);
    CHECK(output.decision_serial == 1);

    sample = observation(100, 110, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.smoothed_loss_basis_points == 0);
    CHECK(output.redundancy_frames == 1);
    CHECK(output.packets_per_second == 60);

    sample = observation(200, 118, 2);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.window_loss_basis_points == 2000);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_LOSS_ELEVATED);
    CHECK(output.packets_per_second == 75);
    CHECK(output.redundancy_frames == 1);

    sample = observation(300, 126, 4);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_LOSS_SEVERE);
    CHECK(output.redundancy_frames == 2);

    CHECK(Worr_AdaptiveInputGetTelemetryV1(&state, &telemetry) ==
          WORR_ADAPTIVE_INPUT_OK);
    CHECK(telemetry.evaluate_calls == 5);
    CHECK(telemetry.window_evaluations == 4);
    CHECK(telemetry.held_evaluations == 1);
    CHECK(telemetry.cumulative_window_received == 26);
    CHECK(telemetry.cumulative_window_dropped == 4);
}

static void test_queue_rate_and_recovery(void)
{
    worr_adaptive_input_config_v1 config;
    worr_adaptive_input_state_v1 state;
    worr_adaptive_input_output_v1 output;
    worr_adaptive_input_observation_v1 sample;

    Worr_AdaptiveInputDefaultConfigV1(&config);
    CHECK(Worr_AdaptiveInputResetV1(&state) == WORR_ADAPTIVE_INPUT_OK);

    sample = observation(0, 0, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    sample = observation(100, 4, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);

    sample = observation(200, 8, 0);
    sample.queued_commands = 7;
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.reason_mask &
          WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE_CRITICAL);
    CHECK(output.packets_per_second == 90);
    CHECK(output.redundancy_frames == 2);

    sample = observation(300, 12, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_RECOVERY_HOLD);
    CHECK(output.packets_per_second == 90);
    CHECK(output.redundancy_frames == 2);

    sample = observation(400, 16, 0);
    sample.queued_commands = 7;
    sample.rate_bytes_per_second = 4000;
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_RATE_CRITICAL);
    CHECK(output.packets_per_second == 30);
    CHECK(output.redundancy_frames == 0);

    sample = observation(1500, 20, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(!(output.reason_mask &
            WORR_ADAPTIVE_INPUT_REASON_RECOVERY_HOLD));
    CHECK(output.packets_per_second == 60);
    CHECK(output.redundancy_frames == 1);
}

static void test_boundaries_and_resets(void)
{
    worr_adaptive_input_config_v1 config;
    worr_adaptive_input_state_v1 state;
    worr_adaptive_input_output_v1 output;
    worr_adaptive_input_observation_v1 sample;

    Worr_AdaptiveInputDefaultConfigV1(&config);
    CHECK(Worr_AdaptiveInputResetV1(&state) == WORR_ADAPTIVE_INPUT_OK);
    sample = observation(1000, 100, 10);
    sample.flags = 0;
    sample.configured_packets_per_second = 0;
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.packets_per_second == 0);
    CHECK(output.send_interval_ms == 0);
    CHECK(output.redundancy_frames == 0);
    CHECK(output.reason_mask &
          WORR_ADAPTIVE_INPUT_REASON_NO_BATCH_REDUNDANCY);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_NO_PACING_LIMIT);

    sample = observation(1100, 2, 1);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) ==
          WORR_ADAPTIVE_INPUT_COUNTER_RESET);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_COUNTER_RESET);

    sample = observation(900, 3, 1);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_CLOCK_RESET);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_CLOCK_RESET);

    sample = observation(1000, 4, 1);
    sample.reserved0 = 1;
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) ==
          WORR_ADAPTIVE_INPUT_INVALID_OBSERVATION);

    config.reserved[0] = 1;
    sample.reserved0 = 0;
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) ==
          WORR_ADAPTIVE_INPUT_INVALID_CONFIG);
}

static void test_unlimited_transition_during_recovery(void)
{
    worr_adaptive_input_config_v1 config;
    worr_adaptive_input_state_v1 state;
    worr_adaptive_input_output_v1 output;
    worr_adaptive_input_observation_v1 sample;

    Worr_AdaptiveInputDefaultConfigV1(&config);
    CHECK(Worr_AdaptiveInputResetV1(&state) == WORR_ADAPTIVE_INPUT_OK);

    sample = observation(0, 0, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    sample = observation(100, 4, 0);
    sample.queued_commands = 7;
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.packets_per_second == 90);

    sample = observation(200, 8, 0);
    sample.configured_packets_per_second = 0;
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_RECOVERY_HOLD);
    CHECK(output.reason_mask & WORR_ADAPTIVE_INPUT_REASON_NO_PACING_LIMIT);
    CHECK(output.packets_per_second == 0);
    CHECK(output.send_interval_ms == 0);
}

static void test_low_rate_loss_sample_accumulates(void)
{
    worr_adaptive_input_config_v1 config;
    worr_adaptive_input_state_v1 state;
    worr_adaptive_input_output_v1 output;
    worr_adaptive_input_observation_v1 sample;
    worr_adaptive_input_telemetry_v1 telemetry;

    Worr_AdaptiveInputDefaultConfigV1(&config);
    CHECK(Worr_AdaptiveInputResetV1(&state) == WORR_ADAPTIVE_INPUT_OK);

    sample = observation(0, 0, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    sample = observation(100, 2, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.window_received_packets == 2);
    CHECK(output.reason_mask &
          WORR_ADAPTIVE_INPUT_REASON_INSUFFICIENT_LOSS_SAMPLE);

    sample = observation(200, 4, 0);
    CHECK(Worr_AdaptiveInputEvaluateV1(&state, &config, &sample,
                                       &output) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(output.window_received_packets == 4);
    CHECK(!(output.reason_mask &
            WORR_ADAPTIVE_INPUT_REASON_INSUFFICIENT_LOSS_SAMPLE));
    CHECK(state.loss_sample_ready == 1);
    CHECK(Worr_AdaptiveInputGetTelemetryV1(&state, &telemetry) ==
          WORR_ADAPTIVE_INPUT_OK);
    CHECK(telemetry.cumulative_window_received == 4);
    CHECK(telemetry.cumulative_window_dropped == 0);
}

static void test_determinism(void)
{
    worr_adaptive_input_config_v1 config;
    worr_adaptive_input_state_v1 left;
    worr_adaptive_input_state_v1 right;
    worr_adaptive_input_output_v1 left_output;
    worr_adaptive_input_output_v1 right_output;
    unsigned i;

    Worr_AdaptiveInputDefaultConfigV1(&config);
    CHECK(Worr_AdaptiveInputResetV1(&left) == WORR_ADAPTIVE_INPUT_OK);
    CHECK(Worr_AdaptiveInputResetV1(&right) == WORR_ADAPTIVE_INPUT_OK);
    for (i = 0; i < 64; ++i) {
        worr_adaptive_input_observation_v1 sample = observation(
            (uint64_t)i * 100, (uint64_t)i * 7,
            (uint64_t)(i / 9));
        sample.smoothed_rtt_ms = 35 + (i % 11) * 7;
        sample.queued_commands = i % 9;
        sample.unacknowledged_packets = i % 27;
        sample.rate_bytes_per_second =
            (i % 13 == 0) ? 4000 : 15000;
        CHECK(Worr_AdaptiveInputEvaluateV1(
                  &left, &config, &sample, &left_output) ==
              Worr_AdaptiveInputEvaluateV1(
                  &right, &config, &sample, &right_output));
        CHECK(memcmp(&left_output, &right_output,
                     sizeof(left_output)) == 0);
    }
    CHECK(memcmp(&left, &right, sizeof(left)) == 0);
}

int main(void)
{
    test_stable_and_loss_policy();
    test_queue_rate_and_recovery();
    test_boundaries_and_resets();
    test_unlimited_transition_during_recovery();
    test_low_rate_loss_sample_accumulates();
    test_determinism();
    puts("adaptive input policy tests passed");
    return 0;
}
