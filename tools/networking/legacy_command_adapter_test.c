/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_command_adapter.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "check failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                        \
            return 1;                                                       \
        }                                                                   \
    } while (0)

#define TEST_MAX_COMMANDS WORR_LEGACY_COMMAND_BATCH_MAX_COUNT

typedef struct stream_image_s {
    worr_command_stream_v1 stream;
    worr_command_stream_slot_v1 slots[TEST_MAX_COMMANDS];
    uint32_t capacity;
} stream_image;

static worr_prediction_command_v1 make_command(uint8_t duration,
                                                uint8_t marker)
{
    worr_prediction_command_v1 command;
    memset(&command, 0, sizeof(command));
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms = duration;
    command.buttons = marker;
    command.view_angles[0] = (float)marker;
    command.forward_move = (float)(int8_t)marker;
    command.side_move = -(float)(int8_t)marker;
    return command;
}

static worr_command_render_watermark_v1 make_watermark(uint32_t tick)
{
    worr_command_render_watermark_v1 watermark;
    memset(&watermark, 0, sizeof(watermark));
    watermark.struct_size = sizeof(watermark);
    watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
    watermark.source_server_tick = tick;
    watermark.tick_interval_us = 10000;
    watermark.source_server_time_us = (uint64_t)tick * UINT64_C(10000);
    watermark.rendered_server_time_us = watermark.source_server_time_us;
    return watermark;
}

static worr_command_record_v1 make_record(uint32_t epoch,
                                           uint32_t sequence,
                                           uint8_t duration,
                                           uint8_t marker,
                                           uint64_t sample_time)
{
    worr_command_record_v1 record;
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id = (worr_command_id_v1){epoch, sequence};
    record.sample_time_us = sample_time;
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command = make_command(duration, marker);
    record.render_watermark = make_watermark(1);
    (void)Worr_CommandRecordCanonicalizeV1(&record, 250);
    return record;
}

static void save_stream(stream_image *image,
                        const worr_command_stream_v1 *stream)
{
    image->stream = *stream;
    image->capacity = stream->capacity;
    memcpy(image->slots, stream->slots,
           sizeof(*stream->slots) * stream->capacity);
}

static bool stream_matches(const stream_image *image,
                           const worr_command_stream_v1 *stream)
{
    return image->capacity == stream->capacity &&
           memcmp(&image->stream, stream, sizeof(*stream)) == 0 &&
           memcmp(image->slots, stream->slots,
                  sizeof(*stream->slots) * stream->capacity) == 0;
}

static bool feed_header(
    worr_legacy_command_sideband_parser_v1 *parser,
    const worr_legacy_command_setting_pair_v1 pairs[
        WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT])
{
    uint32_t i;
    for (i = 0; i < WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT; ++i) {
        const worr_legacy_command_sideband_result_v1 result =
            Worr_LegacyCommandSidebandObserveSettingV1(
                parser, pairs[i].index, pairs[i].value);
        if (i + 1u == WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT) {
            if (result !=
                WORR_LEGACY_COMMAND_SIDEBAND_HEADER_COMMITTED) {
                return false;
            }
        } else if (result !=
                   WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED) {
            return false;
        }
    }
    return true;
}

static int test_sideband_round_trip_and_exact_move_adjacency(void)
{
    worr_legacy_command_range_v1 range;
    worr_legacy_command_range_v1 decoded;
    worr_legacy_command_range_v1 untouched;
    worr_legacy_command_setting_pair_v1 pairs[
        WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT];
    worr_legacy_command_sideband_parser_v1 parser;

    CHECK(Worr_LegacyCommandRangeInitV1(
        &range, (worr_command_id_v1){UINT32_C(0x89abcdef),
                                     UINT32_C(0xfedcba98)},
        WORR_LEGACY_COMMAND_MOVE_COUNT));
    CHECK(Worr_LegacyCommandRangeValidateV1(&range));
    CHECK(range.header_checksum == UINT32_C(0x634f2487));
    CHECK(Worr_LegacyCommandSidebandEncodeV1(
        &range, pairs, WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT));
    CHECK(pairs[0].index == WORR_LEGACY_COMMAND_SETTING_BEGIN);
    CHECK(pairs[1].index == WORR_LEGACY_COMMAND_SETTING_EPOCH_LOW);
    CHECK(pairs[2].index == WORR_LEGACY_COMMAND_SETTING_EPOCH_HIGH);
    CHECK(pairs[3].index == WORR_LEGACY_COMMAND_SETTING_SEQUENCE_LOW);
    CHECK(pairs[4].index == WORR_LEGACY_COMMAND_SETTING_SEQUENCE_HIGH);
    CHECK(pairs[5].index == WORR_LEGACY_COMMAND_SETTING_COUNT);
    CHECK(pairs[6].index == WORR_LEGACY_COMMAND_SETTING_CHECKSUM_LOW);
    CHECK(pairs[7].index == WORR_LEGACY_COMMAND_SETTING_CHECKSUM_HIGH);
    CHECK(pairs[8].index == WORR_LEGACY_COMMAND_SETTING_COMMIT);
    CHECK(pairs[8].value == INT16_C(0x4741));
    CHECK(pairs[1].value < 0 && pairs[2].value < 0 &&
          pairs[3].value < 0 && pairs[4].value < 0);

    CHECK(Worr_LegacyCommandSidebandParserInitV1(&parser));
    CHECK(Worr_LegacyCommandSidebandParserValidateV1(&parser));
    CHECK(Worr_LegacyCommandSidebandPacketBeginV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(&parser, 4, 60) ==
          WORR_LEGACY_COMMAND_SIDEBAND_NOT_SIDEBAND);
    CHECK(feed_header(&parser, pairs));
    memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(Worr_LegacyCommandSidebandConsumeMoveV1(
              &parser, WORR_LEGACY_COMMAND_CARRIER_MOVE,
              WORR_LEGACY_COMMAND_MOVE_COUNT, &decoded) ==
          WORR_LEGACY_COMMAND_SIDEBAND_MOVE_MATCHED);
    CHECK(memcmp(&decoded, &range, sizeof(range)) == 0);
    CHECK(Worr_LegacyCommandSidebandPacketEndV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_ENDED);
    CHECK(parser.telemetry.fields_accepted ==
          WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT);
    CHECK(parser.telemetry.headers_committed == 1);
    CHECK(parser.telemetry.moves_matched == 1);

    CHECK(Worr_LegacyCommandSidebandPacketBeginV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_STARTED);
    untouched = decoded;
    CHECK(Worr_LegacyCommandSidebandConsumeMoveV1(
              &parser, WORR_LEGACY_COMMAND_CARRIER_MOVE,
              WORR_LEGACY_COMMAND_MOVE_COUNT, &decoded) ==
          WORR_LEGACY_COMMAND_SIDEBAND_MISSING_HEADER);
    CHECK(memcmp(&decoded, &untouched, sizeof(decoded)) == 0);
    CHECK(Worr_LegacyCommandSidebandPacketEndV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_ENDED);
    return 0;
}

static int test_sideband_fail_closed_malformed_headers(void)
{
    worr_legacy_command_range_v1 first;
    worr_legacy_command_range_v1 second;
    worr_legacy_command_range_v1 output;
    worr_legacy_command_setting_pair_v1 a[
        WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT];
    worr_legacy_command_setting_pair_v1 b[
        WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT];
    worr_legacy_command_sideband_parser_v1 parser;
    uint32_t i;

    CHECK(Worr_LegacyCommandRangeInitV1(
        &first, (worr_command_id_v1){7, 10}, 3));
    CHECK(Worr_LegacyCommandRangeInitV1(
        &second, (worr_command_id_v1){8, 20}, 4));
    CHECK(Worr_LegacyCommandSidebandEncodeV1(
        &first, a, WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT));
    CHECK(Worr_LegacyCommandSidebandEncodeV1(
        &second, b, WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT));
    CHECK(Worr_LegacyCommandSidebandParserInitV1(&parser));

    CHECK(Worr_LegacyCommandSidebandPacketBeginV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, WORR_LEGACY_COMMAND_SETTING_BEGIN, 2) ==
          WORR_LEGACY_COMMAND_SIDEBAND_UNSUPPORTED_VERSION);
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, a[0].index, a[0].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(Worr_LegacyCommandSidebandObserveInterveningServiceV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE);

    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, a[0].index, a[0].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, a[3].index, a[3].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(Worr_LegacyCommandSidebandConsumeMoveV1(
              &parser, WORR_LEGACY_COMMAND_CARRIER_MOVE, 3, &output) ==
          WORR_LEGACY_COMMAND_SIDEBAND_MISSING_HEADER);

    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, a[0].index, a[0].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, a[1].index, a[1].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, a[1].index, a[1].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(Worr_LegacyCommandSidebandPacketBeginV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_RESET_PACKET_BOUNDARY);

    /* A non-sideband setting and any other service break adjacency. */
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, a[0].index, a[0].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(&parser, 1, 2) ==
          WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE);
    CHECK(feed_header(&parser, a));
    CHECK(Worr_LegacyCommandSidebandObserveInterveningServiceV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE);
    CHECK(Worr_LegacyCommandSidebandConsumeMoveV1(
              &parser, WORR_LEGACY_COMMAND_CARRIER_MOVE, 3, &output) ==
          WORR_LEGACY_COMMAND_SIDEBAND_MISSING_HEADER);

    /* Corrupt checksum and commit words are independently rejected. */
    a[6].value ^= 1;
    for (i = 0; i <= 7; ++i) {
        const worr_legacy_command_sideband_result_v1 result =
            Worr_LegacyCommandSidebandObserveSettingV1(
                &parser, a[i].index, a[i].value);
        if (i == 7) {
            CHECK(result ==
                  WORR_LEGACY_COMMAND_SIDEBAND_CHECKSUM_MISMATCH);
        } else {
            CHECK(result ==
                  WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED);
        }
    }
    CHECK(Worr_LegacyCommandSidebandObserveInterveningServiceV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE);
    a[6].value ^= 1;
    a[8].value ^= 1;
    for (i = 0; i < WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT; ++i) {
        const worr_legacy_command_sideband_result_v1 result =
            Worr_LegacyCommandSidebandObserveSettingV1(
                &parser, a[i].index, a[i].value);
        if (i + 1u == WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT) {
            CHECK(result ==
                  WORR_LEGACY_COMMAND_SIDEBAND_COMMIT_MISMATCH);
        } else {
            CHECK(result ==
                  WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED);
        }
    }
    CHECK(Worr_LegacyCommandSidebandObserveInterveningServiceV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE);
    a[8].value ^= 1;

    /* A syntactically ordered header assembled from two packets is mixed. */
    for (i = 0; i < 6; ++i) {
        CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
                  &parser, a[i].index, a[i].value) ==
              WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED);
    }
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, b[6].index, b[6].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(
              &parser, b[7].index, b[7].value) ==
          WORR_LEGACY_COMMAND_SIDEBAND_CHECKSUM_MISMATCH);
    CHECK(Worr_LegacyCommandSidebandPacketEndV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_RESET_PACKET_BOUNDARY);

    CHECK(parser.telemetry.unsupported_versions == 1);
    CHECK(parser.telemetry.checksum_failures == 2);
    CHECK(parser.telemetry.commit_failures == 1);
    CHECK(parser.telemetry.intervening_resets >= 4);
    CHECK(parser.telemetry.packet_boundary_resets >= 2);
    CHECK(Worr_LegacyCommandSidebandParserValidateV1(&parser));
    return 0;
}

static int test_sideband_carrier_counts_and_saturation(void)
{
    worr_legacy_command_range_v1 range;
    worr_legacy_command_range_v1 output;
    worr_legacy_command_setting_pair_v1 pairs[
        WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT];
    worr_legacy_command_sideband_parser_v1 parser;

    CHECK(!Worr_LegacyCommandRangeInitV1(
        &range, (worr_command_id_v1){1, 1}, 0));
    CHECK(!Worr_LegacyCommandRangeInitV1(
        &range, (worr_command_id_v1){1, 1},
        WORR_LEGACY_COMMAND_BATCH_MAX_COUNT + 1u));
    CHECK(Worr_LegacyCommandRangeInitV1(
        &range, (worr_command_id_v1){1, 1},
        WORR_LEGACY_COMMAND_BATCH_MAX_COUNT));
    CHECK(Worr_LegacyCommandSidebandEncodeV1(
        &range, pairs, WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT));
    CHECK(Worr_LegacyCommandSidebandParserInitV1(&parser));
    CHECK(Worr_LegacyCommandSidebandPacketBeginV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_STARTED);
    CHECK(feed_header(&parser, pairs));
    CHECK(Worr_LegacyCommandSidebandConsumeMoveV1(
              &parser, WORR_LEGACY_COMMAND_CARRIER_MOVE,
              WORR_LEGACY_COMMAND_MOVE_COUNT, &output) ==
          WORR_LEGACY_COMMAND_SIDEBAND_COUNT_MISMATCH);
    CHECK(feed_header(&parser, pairs));
    CHECK(Worr_LegacyCommandSidebandConsumeMoveV1(
              &parser, WORR_LEGACY_COMMAND_CARRIER_BATCH_MOVE,
              WORR_LEGACY_COMMAND_BATCH_MAX_COUNT, &output) ==
          WORR_LEGACY_COMMAND_SIDEBAND_MOVE_MATCHED);

    CHECK(feed_header(&parser, pairs));
    CHECK(Worr_LegacyCommandSidebandConsumeMoveV1(
              &parser, 99, WORR_LEGACY_COMMAND_BATCH_MAX_COUNT,
              &output) ==
          WORR_LEGACY_COMMAND_SIDEBAND_INVALID_CARRIER);
    parser.telemetry.settings_seen = UINT64_MAX;
    CHECK(Worr_LegacyCommandSidebandObserveSettingV1(&parser, 1, 2) ==
          WORR_LEGACY_COMMAND_SIDEBAND_NOT_SIDEBAND);
    CHECK(parser.telemetry.settings_seen == UINT64_MAX);
    CHECK(Worr_LegacyCommandSidebandPacketEndV1(&parser) ==
          WORR_LEGACY_COMMAND_SIDEBAND_PACKET_ENDED);
    return 0;
}

static int apply_range(
    worr_command_stream_v1 *stream,
    worr_command_id_v1 first,
    worr_prediction_command_v1 *commands,
    uint32_t count,
    uint32_t watermark_tick,
    worr_legacy_command_adapter_report_v1 *report,
    worr_command_record_v1 record_scratch[TEST_MAX_COMMANDS],
    worr_command_stream_slot_v1 stream_scratch[TEST_MAX_COMMANDS])
{
    worr_legacy_command_range_v1 range;
    worr_command_render_watermark_v1 watermark =
        make_watermark(watermark_tick);
    if (count > UINT16_MAX ||
        !Worr_LegacyCommandRangeInitV1(
            &range, first, (uint16_t)count)) {
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_RANGE;
    }
    return Worr_LegacyCommandAdapterApplyV1(
        stream, &range, commands, count,
        WORR_PREDICTION_MODEL_REVISION, &watermark,
        record_scratch, TEST_MAX_COMMANDS,
        stream_scratch, TEST_MAX_COMMANDS, report);
}

static int test_default_move_backup_and_bootstrap_mapping(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_v1 bootstrap;
    worr_command_stream_slot_v1 slots[8];
    worr_command_stream_slot_v1 bootstrap_slots[4];
    worr_command_stream_slot_v1 stream_scratch[TEST_MAX_COMMANDS];
    worr_command_record_v1 record_scratch[TEST_MAX_COMMANDS];
    worr_prediction_command_v1 commands[3];
    worr_prediction_command_v1 bootstrap_commands[3];
    worr_legacy_command_adapter_report_v1 report;
    worr_command_record_v1 stored;
    stream_image before;

    commands[0] = make_command(5, 1);
    commands[1] = make_command(0, 2);
    commands[2] = make_command(7, 3);
    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 8, 250, (worr_command_cursor_v1){10, 0}, 0));
    CHECK(apply_range(
              &stream, (worr_command_id_v1){10, 1}, commands, 3, 10,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
    CHECK(report.inserted_count == 3 && report.duplicate_count == 0 &&
          report.stale_count == 0);
    CHECK(Worr_CommandStreamCopyRecordV1(
        &stream, (worr_command_id_v1){10, 1}, &stored));
    CHECK(stored.command.buttons == 1 && stored.sample_time_us == 5000);
    CHECK(Worr_CommandStreamCopyRecordV1(
        &stream, (worr_command_id_v1){10, 2}, &stored));
    CHECK(stored.command.buttons == 2 && stored.sample_time_us == 5000);
    CHECK(Worr_CommandStreamCopyRecordV1(
        &stream, (worr_command_id_v1){10, 3}, &stored));
    CHECK(stored.command.buttons == 3 && stored.sample_time_us == 12000);

    save_stream(&before, &stream);
    CHECK(apply_range(
              &stream, (worr_command_id_v1){10, 1}, commands, 3, 99,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_IDEMPOTENT);
    CHECK(report.duplicate_count == 3 && report.inserted_count == 0);
    CHECK(stream.telemetry.duplicates == before.stream.telemetry.duplicates);
    CHECK(stream_matches(&before, &stream));

    commands[1].buttons ^= 1u;
    CHECK(apply_range(
              &stream, (worr_command_id_v1){10, 1}, commands, 3, 100,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_CONFLICT);
    CHECK(stream_matches(&before, &stream));

    /*
     * Bootstrap after two real commands have already been consumed.  The
     * MOVE carrier still has three decoded slots, but the prefix is stale and
     * only the third command receives a new canonical ID.
     */
    bootstrap_commands[0] = make_command(9, 41);
    bootstrap_commands[1] = make_command(8, 42);
    bootstrap_commands[2] = make_command(4, 43);
    CHECK(Worr_CommandStreamInitV1(
        &bootstrap, bootstrap_slots, 4, 250,
        (worr_command_cursor_v1){12, 101}, 9000));
    CHECK(apply_range(
              &bootstrap, (worr_command_id_v1){12, 100},
              bootstrap_commands, 3, 1,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
    CHECK(report.stale_count == 2 && report.duplicate_count == 0 &&
          report.inserted_count == 1);
    CHECK(Worr_CommandStreamCopyRecordV1(
        &bootstrap, (worr_command_id_v1){12, 102}, &stored));
    CHECK(stored.command.buttons == 43 && stored.sample_time_us == 13000);
    CHECK(bootstrap.received_cursor.contiguous_sequence == 102);
    return 0;
}

static int test_variable_batch_loss_retry_duplicate_reorder_and_stale(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_v1 reordered;
    worr_command_stream_slot_v1 slots[8];
    worr_command_stream_slot_v1 reordered_slots[8];
    worr_command_stream_slot_v1 stream_scratch[TEST_MAX_COMMANDS];
    worr_command_record_v1 record_scratch[TEST_MAX_COMMANDS];
    worr_prediction_command_v1 first[4];
    worr_prediction_command_v1 overlap[4];
    worr_prediction_command_v1 newer[6];
    worr_prediction_command_v1 stale[3];
    worr_legacy_command_adapter_report_v1 report;
    worr_command_record_v1 stored;
    stream_image before;
    uint32_t i;

    for (i = 0; i < 4; ++i)
        first[i] = make_command(1, (uint8_t)(11u + i));
    overlap[0] = first[2];
    overlap[1] = first[3];
    overlap[2] = make_command(2, 21);
    overlap[3] = make_command(3, 22);
    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 8, 250, (worr_command_cursor_v1){20, 0}, 0));
    CHECK(apply_range(
              &stream, (worr_command_id_v1){20, 1}, first, 4, 1,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);

    /* Loss recovery: retained backups collide safely, new suffix commits. */
    CHECK(apply_range(
              &stream, (worr_command_id_v1){20, 3}, overlap, 4, 2,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
    CHECK(report.duplicate_count == 2 && report.inserted_count == 2);
    CHECK(Worr_CommandStreamCopyRecordV1(
        &stream, (worr_command_id_v1){20, 5}, &stored));
    CHECK(stored.command.buttons == 21 && stored.sample_time_us == 6000);
    CHECK(Worr_CommandStreamCopyRecordV1(
        &stream, (worr_command_id_v1){20, 6}, &stored));
    CHECK(stored.command.buttons == 22 && stored.sample_time_us == 9000);

    save_stream(&before, &stream);
    CHECK(apply_range(
              &stream, (worr_command_id_v1){20, 3}, overlap, 4, 77,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_IDEMPOTENT);
    CHECK(report.duplicate_count == 4);
    CHECK(stream_matches(&before, &stream));

    /* Reordered future range fails atomically; retry succeeds after prefix. */
    CHECK(Worr_CommandStreamInitV1(
        &reordered, reordered_slots, 8, 250,
        (worr_command_cursor_v1){30, 0}, 0));
    save_stream(&before, &reordered);
    CHECK(apply_range(
              &reordered, (worr_command_id_v1){30, 3}, overlap, 4, 1,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_FUTURE_GAP);
    CHECK(stream_matches(&before, &reordered));
    CHECK(apply_range(
              &reordered, (worr_command_id_v1){30, 1}, first, 4, 1,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
    CHECK(apply_range(
              &reordered, (worr_command_id_v1){30, 3}, overlap, 4, 2,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);

    /* Evict consumed commands, then accept old IDs as opaque stale retries. */
    for (i = 1; i <= 6; ++i) {
        CHECK(Worr_CommandStreamConsumeV1(
                  &reordered, (worr_command_id_v1){30, i}, NULL) ==
              WORR_COMMAND_STREAM_CONSUMED);
    }
    for (i = 0; i < 6; ++i)
        newer[i] = make_command(1, (uint8_t)(31u + i));
    CHECK(apply_range(
              &reordered, (worr_command_id_v1){30, 7}, newer, 6, 3,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
    for (i = 0; i < 3; ++i)
        stale[i] = make_command(9, (uint8_t)(90u + i));
    save_stream(&before, &reordered);
    CHECK(apply_range(
              &reordered, (worr_command_id_v1){30, 1}, stale, 3, 4,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_IDEMPOTENT);
    CHECK(report.stale_count == 3 && report.duplicate_count == 0);
    CHECK(stream_matches(&before, &reordered));
    return 0;
}

static int test_late_real_command_after_synthesized_disposition(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_slot_v1 slots[4];
    worr_command_stream_slot_v1 stream_scratch[TEST_MAX_COMMANDS];
    worr_command_record_v1 record_scratch[TEST_MAX_COMMANDS];
    worr_command_record_v1 synthesized;
    worr_prediction_command_v1 late_real;
    worr_legacy_command_adapter_report_v1 report;
    stream_image before;

    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 4, 250, (worr_command_cursor_v1){35, 0}, 0));
    synthesized = make_record(35, 1, 10, 7, 10000);
    synthesized.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED;
    CHECK(Worr_CommandRecordCanonicalizeV1(&synthesized, 250));
    CHECK(Worr_CommandStreamInsertV1(&stream, &synthesized) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, synthesized.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);

    /* The real input differs, but its ID has already been disposed. */
    late_real = make_command(10, 99);
    save_stream(&before, &stream);
    CHECK(apply_range(
              &stream, synthesized.command_id, &late_real, 1, 2,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_IDEMPOTENT);
    CHECK(report.stale_count == 1 && report.duplicate_count == 0 &&
          report.inserted_count == 0);
    CHECK(stream_matches(&before, &stream));
    return 0;
}

static int test_variable_frame_flattening_and_maximum_batch(void)
{
    static const uint8_t frame_counts[4] = {2, 0, 3, 1};
    static const uint8_t markers[6] = {11, 12, 21, 22, 23, 31};
    worr_command_stream_v1 stream;
    worr_command_stream_slot_v1 slots[TEST_MAX_COMMANDS];
    worr_command_stream_slot_v1 stream_scratch[TEST_MAX_COMMANDS];
    worr_command_record_v1 record_scratch[TEST_MAX_COMMANDS];
    worr_prediction_command_v1 commands[TEST_MAX_COMMANDS];
    worr_legacy_command_adapter_report_v1 report;
    worr_command_record_v1 stored;
    uint32_t flattened = 0;
    uint32_t frame;
    uint32_t i;

    for (frame = 0; frame < 4; ++frame) {
        for (i = 0; i < frame_counts[frame]; ++i) {
            commands[flattened] =
                make_command(1, markers[flattened]);
            ++flattened;
        }
    }
    CHECK(flattened == 6);
    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, TEST_MAX_COMMANDS, 250,
        (worr_command_cursor_v1){40, 0}, 0));
    CHECK(apply_range(
              &stream, (worr_command_id_v1){40, 1}, commands,
              flattened, 1, &report,
              record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
    for (i = 0; i < flattened; ++i) {
        CHECK(Worr_CommandStreamCopyRecordV1(
            &stream, (worr_command_id_v1){40, i + 1u}, &stored));
        CHECK(stored.command.buttons == markers[i]);
    }

    CHECK(Worr_CommandStreamResetV1(&stream, 41) ==
          WORR_COMMAND_STREAM_RESET);
    for (i = 0; i < TEST_MAX_COMMANDS; ++i)
        commands[i] = make_command(1, (uint8_t)i);
    CHECK(apply_range(
              &stream, (worr_command_id_v1){41, 1}, commands,
              TEST_MAX_COMMANDS, 2, &report,
              record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
    CHECK(report.inserted_count == TEST_MAX_COMMANDS);
    CHECK(stream.received_cursor.contiguous_sequence == TEST_MAX_COMMANDS);
    CHECK(stream.last_received_sample_time_us ==
          (uint64_t)TEST_MAX_COMMANDS * UINT64_C(1000));
    return 0;
}

static int test_wrap_overflow_capacity_and_transactionality(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_slot_v1 slots[3];
    worr_command_stream_slot_v1 stream_scratch[TEST_MAX_COMMANDS];
    worr_command_record_v1 record_scratch[TEST_MAX_COMMANDS];
    worr_prediction_command_v1 commands[3];
    worr_legacy_command_adapter_report_v1 report;
    worr_legacy_command_range_v1 invalid_range;
    stream_image before;

    commands[0] = make_command(1, 1);
    commands[1] = make_command(2, 2);
    commands[2] = make_command(3, 3);
    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 3, 250,
        (worr_command_cursor_v1){50, UINT32_MAX - 1u}, 7000));
    CHECK(apply_range(
              &stream,
              (worr_command_id_v1){50, UINT32_MAX}, commands, 2, 1,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_APPLIED);
    CHECK(stream.received_cursor.epoch == 51 &&
          stream.received_cursor.contiguous_sequence == 1);
    CHECK(stream.last_received_sample_time_us == 10000);
    CHECK(stream.telemetry.epoch_wraps == 1);
    CHECK(!Worr_LegacyCommandRangeInitV1(
        &invalid_range,
        (worr_command_id_v1){UINT32_MAX, UINT32_MAX}, 2));

    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 3, 250,
        (worr_command_cursor_v1){60, 5}, UINT64_MAX - 999u));
    save_stream(&before, &stream);
    CHECK(apply_range(
              &stream, (worr_command_id_v1){60, 6}, commands, 1, 1,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_SAMPLE_TIME_OVERFLOW);
    CHECK(stream_matches(&before, &stream));

    /* Third insert stalls behind two unconsumed slots: no prefix commits. */
    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 2, 250, (worr_command_cursor_v1){70, 0}, 0));
    save_stream(&before, &stream);
    CHECK(apply_range(
              &stream, (worr_command_id_v1){70, 1}, commands, 3, 1,
              &report, record_scratch, stream_scratch) ==
          WORR_LEGACY_COMMAND_ADAPTER_CAPACITY);
    CHECK(stream_matches(&before, &stream));
    return 0;
}

static int test_transaction_api_and_adapter_contract_failures(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_slot_v1 slots[3];
    worr_command_stream_slot_v1 transaction_scratch[3];
    worr_command_stream_slot_v1 adapter_stream_scratch[TEST_MAX_COMMANDS];
    worr_command_record_v1 adapter_record_scratch[TEST_MAX_COMMANDS];
    worr_command_record_v1 records[2];
    worr_prediction_command_v1 commands[2];
    worr_legacy_command_range_v1 range;
    worr_command_render_watermark_v1 watermark = make_watermark(1);
    worr_legacy_command_adapter_report_v1 report;
    stream_image before;

    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 3, 250, (worr_command_cursor_v1){80, 0}, 0));
    records[0] = make_record(80, 1, 1, 1, 1000);
    records[1] = make_record(80, 3, 1, 2, 2000);
    save_stream(&before, &stream);
    CHECK(Worr_CommandStreamInsertBatchV1(
              &stream, records, 2, transaction_scratch, 3) ==
          WORR_COMMAND_STREAM_FUTURE_GAP);
    CHECK(stream_matches(&before, &stream));
    CHECK(Worr_CommandStreamInsertBatchV1(
              &stream, records, 2, slots, 3) ==
          WORR_COMMAND_STREAM_INVALID_ARGUMENT);
    CHECK(stream_matches(&before, &stream));

    records[1] = make_record(80, 2, 1, 2, 2000);
    CHECK(Worr_CommandStreamInsertBatchV1(
              &stream, records, 2, transaction_scratch, 3) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamInsertBatchV1(
              &stream, records, 2, transaction_scratch, 3) ==
          WORR_COMMAND_STREAM_DUPLICATE);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, (worr_command_id_v1){80, 1}, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, (worr_command_id_v1){80, 2}, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    records[0] = make_record(80, 3, 1, 3, 3000);
    records[1] = make_record(80, 4, 1, 4, 4000);
    CHECK(Worr_CommandStreamInsertBatchV1(
              &stream, records, 2, transaction_scratch, 3) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, (worr_command_id_v1){80, 3}, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, (worr_command_id_v1){80, 4}, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    records[0] = make_record(80, 5, 1, 5, 5000);
    records[1] = make_record(80, 6, 1, 6, 6000);
    CHECK(Worr_CommandStreamInsertBatchV1(
              &stream, records, 2, transaction_scratch, 3) ==
          WORR_COMMAND_STREAM_INSERTED);
    records[0] = make_record(80, 1, 1, 99, 1000);
    CHECK(Worr_CommandStreamInsertBatchV1(
              &stream, records, 1, transaction_scratch, 3) ==
          WORR_COMMAND_STREAM_STALE);

    CHECK(Worr_CommandStreamResetV1(&stream, 81) ==
          WORR_COMMAND_STREAM_RESET);
    save_stream(&before, &stream);

    commands[0] = make_command(1, 1);
    commands[1] = make_command(1, 2);
    CHECK(Worr_LegacyCommandRangeInitV1(
        &range, (worr_command_id_v1){81, 1}, 2));
    CHECK(Worr_LegacyCommandAdapterApplyV1(
              &stream, &range, commands, 1,
              WORR_PREDICTION_MODEL_REVISION, &watermark,
              adapter_record_scratch, TEST_MAX_COMMANDS,
              adapter_stream_scratch, TEST_MAX_COMMANDS, &report) ==
          WORR_LEGACY_COMMAND_ADAPTER_COUNT_MISMATCH);
    CHECK(stream_matches(&before, &stream));
    CHECK(Worr_LegacyCommandAdapterApplyV1(
              &stream, &range, commands, 2,
              WORR_PREDICTION_MODEL_REVISION, &watermark,
              adapter_record_scratch, 1,
              adapter_stream_scratch, TEST_MAX_COMMANDS, &report) ==
          WORR_LEGACY_COMMAND_ADAPTER_INSUFFICIENT_SCRATCH);
    CHECK(stream_matches(&before, &stream));
    watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
    CHECK(Worr_LegacyCommandAdapterApplyV1(
              &stream, &range, commands, 2,
              WORR_PREDICTION_MODEL_REVISION, &watermark,
              adapter_record_scratch, TEST_MAX_COMMANDS,
              adapter_stream_scratch, TEST_MAX_COMMANDS, &report) ==
          WORR_LEGACY_COMMAND_ADAPTER_INVALID_WATERMARK);
    CHECK(stream_matches(&before, &stream));
    watermark = make_watermark(1);
    commands[1].reserved0 = 1;
    CHECK(Worr_LegacyCommandAdapterApplyV1(
              &stream, &range, commands, 2,
              WORR_PREDICTION_MODEL_REVISION, &watermark,
              adapter_record_scratch, TEST_MAX_COMMANDS,
              adapter_stream_scratch, TEST_MAX_COMMANDS, &report) ==
          WORR_LEGACY_COMMAND_ADAPTER_INVALID_COMMAND);
    CHECK(stream_matches(&before, &stream));
    commands[1].reserved0 = 0;
    CHECK(Worr_LegacyCommandAdapterApplyV1(
              &stream, &range, commands, 2,
              WORR_PREDICTION_MODEL_REVISION, &watermark,
              (worr_command_record_v1 *)(void *)adapter_stream_scratch,
              TEST_MAX_COMMANDS,
              adapter_stream_scratch, TEST_MAX_COMMANDS, &report) ==
          WORR_LEGACY_COMMAND_ADAPTER_ALIAS_VIOLATION);
    CHECK(stream_matches(&before, &stream));
    return 0;
}

int main(void)
{
    CHECK(test_sideband_round_trip_and_exact_move_adjacency() == 0);
    CHECK(test_sideband_fail_closed_malformed_headers() == 0);
    CHECK(test_sideband_carrier_counts_and_saturation() == 0);
    CHECK(test_default_move_backup_and_bootstrap_mapping() == 0);
    CHECK(test_variable_batch_loss_retry_duplicate_reorder_and_stale() == 0);
    CHECK(test_late_real_command_after_synthesized_disposition() == 0);
    CHECK(test_variable_frame_flattening_and_maximum_batch() == 0);
    CHECK(test_wrap_overflow_capacity_and_transactionality() == 0);
    CHECK(test_transaction_api_and_adapter_contract_failures() == 0);
    printf("legacy command adapter: all sideband/transaction checks passed\n");
    return 0;
}
