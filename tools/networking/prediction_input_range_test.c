/* Standalone FR-10-T08/T09 immutable prediction-input range tests. */

#include "common/net/prediction_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "prediction_input_range_test:%d: %s\n",      \
                    __LINE__, #expression);                                 \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

static worr_prediction_command_v1 command(uint8_t duration, float forward)
{
    worr_prediction_command_v1 output;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_PREDICTION_ABI_VERSION;
    output.duration_ms = duration;
    output.view_angles[1] = 90.0f;
    output.forward_move = forward;
    return output;
}

static worr_cgame_prediction_input_command_v1 entry(
    uint32_t legacy, uint32_t epoch, uint32_t sequence)
{
    worr_cgame_prediction_input_command_v1 output;
    memset(&output, 0, sizeof(output));
    output.legacy_sequence = legacy;
    output.command_id = (worr_command_id_v1){epoch, sequence};
    output.command = command(8, (float)(legacy & 31u));
    return output;
}

static worr_prediction_input_resolve_request_v1 request(
    const worr_cgame_prediction_input_command_v1 *history,
    uint32_t history_count)
{
    worr_prediction_input_resolve_request_v1 output;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_PREDICTION_INPUT_RESOLVER_VERSION;
    output.history = history;
    output.history_count = history_count;
    return output;
}

static void test_canonical_cursor_ignores_packet_ack(void)
{
    worr_cgame_prediction_input_command_v1 history[4];
    worr_cgame_prediction_input_command_v1 before[4];
    worr_prediction_input_resolve_request_v1 input;
    worr_cgame_prediction_input_range_v1 output;
    uint32_t i;

    for (i = 0; i < 4; ++i)
        history[i] = entry(100 + i, 7, 11 + i);
    memcpy(before, history, sizeof(history));
    input = request(history, 4);
    input.flags = WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY |
                  WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED;
    input.identity_initial_epoch = 7;
    input.identity_baseline_legacy_sequence = 89;
    input.current_legacy_sequence = 103;
    input.legacy_acknowledged_sequence = 102; /* deliberately newer */
    input.consumed_command.cursor = (worr_command_cursor_v1){7, 11};
    input.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    input.pending_present = 1;
    input.pending_command = entry(104, 0, 0);
    input.pending_command.command = command(4, 17.0f);

    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(output.source ==
          WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR);
    CHECK(output.authoritative_legacy_sequence == 100);
    CHECK(output.current_legacy_sequence == 103);
    CHECK(output.command_count == 3);
    CHECK(output.commands[0].legacy_sequence == 101 &&
          output.commands[0].command_id.sequence == 12);
    CHECK(output.commands[2].legacy_sequence == 103 &&
          output.commands[2].command_id.sequence == 14);
    CHECK((output.flags & WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) != 0);
    CHECK(output.pending_command.legacy_sequence == 104);
    CHECK(memcmp(history, before, sizeof(history)) == 0);
}

static void test_canonical_zero_cursor_replays_from_identity_baseline(void)
{
    worr_cgame_prediction_input_command_v1 history[3] = {
        entry(501, 19, 1), entry(502, 19, 2), entry(503, 19, 3)};
    worr_prediction_input_resolve_request_v1 input = request(history, 3);
    worr_cgame_prediction_input_range_v1 output;

    input.flags = WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY |
                  WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED;
    input.identity_initial_epoch = 19;
    input.identity_baseline_legacy_sequence = 500;
    input.current_legacy_sequence = 503;
    input.legacy_acknowledged_sequence = 503;
    input.consumed_command.cursor = (worr_command_cursor_v1){19, 0};
    input.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;

    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(output.authoritative_legacy_sequence == 500);
    CHECK(output.command_count == 3);
    CHECK(output.commands[0].command_id.sequence == 1);
}

static void test_bootstrap_and_true_legacy_fallback(void)
{
    worr_cgame_prediction_input_command_v1 history[3] = {
        entry(41, 0, 0), entry(42, 0, 0), entry(43, 0, 0)};
    worr_prediction_input_resolve_request_v1 input = request(history, 3);
    worr_cgame_prediction_input_range_v1 output;

    input.flags = WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY;
    input.current_legacy_sequence = 43;
    input.legacy_acknowledged_sequence = 41;
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(output.source ==
          WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_BOOTSTRAP);
    CHECK((output.flags &
           WORR_CGAME_PREDICTION_INPUT_CANONICAL_BOOTSTRAP) != 0);
    CHECK(output.command_count == 2);

    input.flags = 0;
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(output.source ==
          WORR_CGAME_PREDICTION_INPUT_SOURCE_LEGACY_PACKET_ACK);
    CHECK(output.command_count == 2);
}

static void test_fail_closed_canonical_invariants(void)
{
    worr_cgame_prediction_input_command_v1 history[4] = {
        entry(20, 3, 5), entry(21, 3, 6),
        entry(22, 3, 7), entry(23, 3, 8)};
    worr_prediction_input_resolve_request_v1 input = request(history, 4);
    worr_cgame_prediction_input_range_v1 output;

    input.flags = WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY |
                  WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED;
    input.identity_initial_epoch = 3;
    input.current_legacy_sequence = 23;
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_CANONICAL_METADATA_REQUIRED);
    CHECK((output.flags &
           WORR_CGAME_PREDICTION_INPUT_HARD_RESYNC_REQUIRED) != 0);

    input.consumed_command.cursor = (worr_command_cursor_v1){3, 5};
    input.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    history[0].command_id = (worr_command_id_v1){0, 0};
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_HISTORY_MISSING);

    history[0].command_id = (worr_command_id_v1){3, 5};
    history[1].command_id = (worr_command_id_v1){3, 5};
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_HISTORY_AMBIGUOUS);

    history[1].command_id = (worr_command_id_v1){3, 99};
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_IDENTITY_DISCONTINUITY);

    input.consumed_command.cursor = (worr_command_cursor_v1){4, 0};
    input.identity_initial_epoch = 3;
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_IDENTITY_EPOCH_MISMATCH);
}

static void test_wrap_overflow_and_invalid_command(void)
{
    worr_cgame_prediction_input_command_v1 wrap[3] = {
        entry(UINT32_MAX, 9, 1), entry(0, 9, 2), entry(1, 9, 3)};
    worr_prediction_input_resolve_request_v1 input = request(wrap, 3);
    worr_cgame_prediction_input_range_v1 output;

    input.current_legacy_sequence = 1;
    input.legacy_acknowledged_sequence = UINT32_MAX;
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(output.command_count == 2);
    CHECK(output.commands[0].legacy_sequence == 0);
    CHECK(output.commands[1].legacy_sequence == 1);

    input.current_legacy_sequence = 200;
    input.legacy_acknowledged_sequence = 72;
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_RANGE_EXHAUSTED);

    input.current_legacy_sequence = 1;
    input.legacy_acknowledged_sequence = UINT32_MAX;
    wrap[1].command.reserved0 = 1;
    CHECK(Worr_PredictionInputResolveV1(&input, &output) ==
          WORR_CGAME_PREDICTION_INPUT_COMMAND_INVALID);
}

int main(void)
{
    CHECK(WORR_CGAME_PREDICTION_INPUT_REPLAY_REJECTED == 10);
    CHECK(WORR_CGAME_PREDICTION_INPUT_RETAINED_STATE_MISSING == 11);
    CHECK(WORR_CGAME_PREDICTION_INPUT_CONFIG_DISCONTINUITY == 12);
    test_canonical_cursor_ignores_packet_ack();
    test_canonical_zero_cursor_replays_from_identity_baseline();
    test_bootstrap_and_true_legacy_fallback();
    test_fail_closed_canonical_invariants();
    test_wrap_overflow_and_invalid_command();
    puts("prediction input range tests passed");
    return EXIT_SUCCESS;
}
