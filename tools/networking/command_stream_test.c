/* Deterministic hostile checks for the FR-10-T09 Phase-1 command core. */
#include "common/net/command_stream.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,         \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static worr_command_render_watermark_v1 make_watermark(uint32_t provenance)
{
    worr_command_render_watermark_v1 watermark;
    memset(&watermark, 0, sizeof(watermark));
    watermark.struct_size = sizeof(watermark);
    watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    watermark.provenance = provenance;
    if (provenance != WORR_COMMAND_RENDER_PROVENANCE_NONE) {
        watermark.source_server_tick = 10;
        watermark.tick_interval_us = 10000;
        watermark.source_server_time_us = 100000;
        watermark.rendered_server_time_us = 100000;
    }
    return watermark;
}

static worr_command_record_v1 make_record(
    uint32_t epoch,
    uint32_t sequence,
    uint8_t duration_ms,
    uint64_t sample_time_us,
    uint32_t provenance)
{
    worr_command_record_v1 record;
    memset(&record, 0, sizeof(record));
    record.struct_size = sizeof(record);
    record.schema_version = WORR_COMMAND_ABI_VERSION;
    record.command_id.epoch = epoch;
    record.command_id.sequence = sequence;
    record.sample_time_us = sample_time_us;
    record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    record.command.struct_size = sizeof(record.command);
    record.command.schema_version = WORR_PREDICTION_ABI_VERSION;
    record.command.duration_ms = duration_ms;
    record.command.buttons = 3;
    record.command.view_angles[0] = 0.0f;
    record.command.view_angles[1] = 90.0f;
    record.command.view_angles[2] = -90.0f;
    record.command.forward_move = 100.0f;
    record.command.side_move = -50.0f;
    record.render_watermark = make_watermark(provenance);
    return record;
}

static bool operational_stream_equal(const worr_command_stream_v1 *a,
                                     const worr_command_stream_v1 *b)
{
    worr_command_stream_v1 left = *a;
    worr_command_stream_v1 right = *b;
    memset(&left.telemetry, 0, sizeof(left.telemetry));
    memset(&right.telemetry, 0, sizeof(right.telemetry));
    return memcmp(&left, &right, sizeof(left)) == 0;
}

static bool prime_consumed_retention(
    worr_command_stream_v1 *stream,
    worr_command_stream_slot_v1 *slots,
    uint32_t capacity,
    uint32_t epoch,
    uint32_t ending_sequence,
    uint32_t inserted_commands)
{
    uint32_t index;
    uint32_t baseline_sequence;

    if (inserted_commands > ending_sequence)
        return false;
    baseline_sequence = ending_sequence - inserted_commands;
    if (!Worr_CommandStreamInitV1(
            stream, slots, capacity, 250,
            (worr_command_cursor_v1){epoch, baseline_sequence},
            (uint64_t)baseline_sequence * UINT64_C(1000))) {
        return false;
    }
    for (index = 1; index <= inserted_commands; ++index) {
        const uint32_t sequence = baseline_sequence + index;
        worr_command_record_v1 record = make_record(
            epoch, sequence, 1,
            (uint64_t)sequence * UINT64_C(1000),
            WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
        if (Worr_CommandStreamInsertV1(stream, &record) !=
                WORR_COMMAND_STREAM_INSERTED ||
            Worr_CommandStreamConsumeV1(
                stream, record.command_id, NULL) !=
                WORR_COMMAND_STREAM_CONSUMED) {
            return false;
        }
    }
    return true;
}

static int test_identity_duration_watermark_and_hashes(void)
{
    worr_command_id_v1 next;
    uint32_t gap;
    worr_command_render_watermark_v1 watermark;
    worr_command_record_v1 record;
    worr_command_record_v1 changed;
    worr_command_record_v1 before;
    uint64_t semantic_hash;
    uint64_t changed_semantic_hash;
    uint64_t content_hash;
    uint64_t changed_content_hash;
    uint64_t untouched_hash = UINT64_C(0x1122334455667788);

    CHECK(sizeof(worr_command_id_v1) == 8);
    CHECK(sizeof(worr_command_cursor_v1) == 8);
    CHECK(sizeof(worr_command_render_watermark_v1) == 40);
    CHECK(sizeof(worr_command_record_v1) == 104);
    CHECK(sizeof(worr_command_stream_slot_v1) == 128);
    CHECK(sizeof(worr_command_stream_telemetry_v1) == 168);

    CHECK(Worr_CommandIdValidV1((worr_command_id_v1){1, 1}, false));
    CHECK(!Worr_CommandIdValidV1((worr_command_id_v1){0, 0}, false));
    CHECK(Worr_CommandIdValidV1((worr_command_id_v1){0, 0}, true));
    CHECK(!Worr_CommandIdValidV1((worr_command_id_v1){1, 0}, true));
    CHECK(!Worr_CommandIdValidV1((worr_command_id_v1){0, 1}, true));
    CHECK(Worr_CommandIdNextV1((worr_command_id_v1){7, 41}, &next));
    CHECK(next.epoch == 7 && next.sequence == 42);
    CHECK(Worr_CommandIdNextV1(
        (worr_command_id_v1){7, UINT32_MAX}, &next));
    CHECK(next.epoch == 8 && next.sequence == 1);
    CHECK(!Worr_CommandIdNextV1(
        (worr_command_id_v1){UINT32_MAX, UINT32_MAX}, &next));
    CHECK(Worr_CommandCursorNextIdV1(
        (worr_command_cursor_v1){4, 0}, &next));
    CHECK(next.epoch == 4 && next.sequence == 1);
    CHECK(Worr_CommandCursorNextIdV1(
        (worr_command_cursor_v1){4, UINT32_MAX}, &next));
    CHECK(next.epoch == 5 && next.sequence == 1);
    CHECK(!Worr_CommandCursorNextIdV1(
        (worr_command_cursor_v1){UINT32_MAX, UINT32_MAX}, &next));

    gap = UINT32_C(0xdeadbeef);
    CHECK(Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){1, 8749},
        (worr_command_id_v1){1, 8911}, 161, &gap));
    CHECK(gap == 161);
    gap = UINT32_C(0xdeadbeef);
    CHECK(!Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){1, 8749},
        (worr_command_id_v1){1, 8911}, 160, &gap));
    CHECK(gap == UINT32_C(0xdeadbeef));
    CHECK(Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){1, 1581},
        (worr_command_id_v1){1, 1983}, 401, &gap));
    CHECK(gap == 401);
    CHECK(Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){9, 100},
        (worr_command_id_v1){9, 4197}, 4096, &gap));
    CHECK(gap == 4096);
    gap = UINT32_C(0xdeadbeef);
    CHECK(!Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){9, 100},
        (worr_command_id_v1){9, 4198}, 4096, &gap));
    CHECK(gap == UINT32_C(0xdeadbeef));
    CHECK(Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){7, UINT32_MAX - 2u},
        (worr_command_id_v1){8, 2}, 3, &gap));
    CHECK(gap == 3);
    CHECK(Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){7, UINT32_MAX},
        (worr_command_id_v1){8, 1}, 0, &gap));
    CHECK(gap == 0);
    gap = UINT32_C(0xdeadbeef);
    CHECK(!Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){7, 50},
        (worr_command_id_v1){7, 50}, UINT32_MAX, &gap));
    CHECK(gap == UINT32_C(0xdeadbeef));
    CHECK(!Worr_CommandCursorGapBeforeV1(
        (worr_command_cursor_v1){7, 50},
        (worr_command_id_v1){6, 100}, UINT32_MAX, &gap));
    CHECK(gap == UINT32_C(0xdeadbeef));

    CHECK(Worr_CommandDurationLimitValidV1(0));
    CHECK(Worr_CommandDurationLimitValidV1(250));
    CHECK(!Worr_CommandDurationLimitValidV1(251));

    watermark = make_watermark(WORR_COMMAND_RENDER_PROVENANCE_NONE);
    CHECK(Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.flags = WORR_COMMAND_RENDER_INTERPOLATED;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark = make_watermark(WORR_COMMAND_RENDER_PROVENANCE_NONE);
    watermark.source_server_tick = 1;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark = make_watermark(WORR_COMMAND_RENDER_PROVENANCE_NONE);
    watermark.tick_interval_us = 1;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark = make_watermark(WORR_COMMAND_RENDER_PROVENANCE_NONE);
    watermark.source_server_time_us = 1;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark = make_watermark(WORR_COMMAND_RENDER_PROVENANCE_NONE);
    watermark.rendered_server_time_us = 1;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark = make_watermark(
        WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED + 1u);
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));

    /* Tick zero and time zero are a valid initial exact watermark. */
    watermark = make_watermark(
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    watermark.source_server_tick = 0;
    watermark.source_server_time_us = 0;
    watermark.rendered_server_time_us = 0;
    CHECK(Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.tick_interval_us = 0;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.tick_interval_us = WORR_COMMAND_MAX_TICK_INTERVAL_US + 1u;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));

    watermark = make_watermark(
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    watermark.flags = WORR_COMMAND_RENDER_INTERPOLATED |
                      WORR_COMMAND_RENDER_EXTRAPOLATED;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.flags = 0;
    watermark.rendered_server_time_us = 99999;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.rendered_server_time_us = 100001;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));

    watermark.flags = WORR_COMMAND_RENDER_INTERPOLATED;
    watermark.source_server_time_us = 300000;
    watermark.rendered_server_time_us = 50000;
    CHECK(Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.rendered_server_time_us = 49999;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.rendered_server_time_us = 300000;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.rendered_server_time_us = 300001;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));

    watermark.flags = WORR_COMMAND_RENDER_EXTRAPOLATED;
    watermark.tick_interval_us = 10000;
    watermark.source_server_time_us = 0;
    watermark.rendered_server_time_us = 40000;
    CHECK(Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.rendered_server_time_us = 40001;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.tick_interval_us = 100000;
    watermark.rendered_server_time_us = 250000;
    CHECK(Worr_CommandRenderWatermarkValidateV1(&watermark));
    watermark.rendered_server_time_us = 250001;
    CHECK(!Worr_CommandRenderWatermarkValidateV1(&watermark));

    record = make_record(3, 1, 0, 0,
                         WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    CHECK(Worr_CommandRecordValidateV1(&record, 0));
    record.command.duration_ms = 250;
    CHECK(Worr_CommandRecordValidateV1(&record, 250));
    record.command.duration_ms = 251;
    CHECK(!Worr_CommandRecordValidateV1(&record, 250));
    record.command.duration_ms = 0;

    changed = record;
    changed.command.view_angles[0] = 1.0f;
    CHECK(!Worr_CommandRecordValidateV1(&changed, 250));
    CHECK(Worr_CommandRecordCanonicalizeV1(&changed, 250));
    CHECK(Worr_CommandRecordValidateV1(&changed, 250));
    CHECK(changed.command.view_angles[0] != 1.0f);

    changed = record;
    changed.command.forward_move = NAN;
    before = changed;
    CHECK(!Worr_CommandRecordCanonicalizeV1(&changed, 250));
    CHECK(memcmp(&changed, &before, sizeof(changed)) == 0);

    CHECK(Worr_CommandRecordSemanticHashV1(
        &record, 250, &semantic_hash));
    CHECK(Worr_CommandRecordContentHashV1(&record, 250, &content_hash));
    CHECK(semantic_hash == UINT64_C(0xdbbfb822917044b4));
    CHECK(content_hash == UINT64_C(0xca0389fe242397cc));
    changed = record;
    changed.render_watermark.source_server_tick = 11;
    CHECK(Worr_CommandRecordSemanticHashV1(
        &changed, 250, &changed_semantic_hash));
    CHECK(Worr_CommandRecordContentHashV1(
        &changed, 250, &changed_content_hash));
    CHECK(semantic_hash != changed_semantic_hash);
    CHECK(content_hash != changed_content_hash);
    CHECK(!Worr_CommandRecordSemanticallyEqualV1(&record, &changed, 250));

    record.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
    changed = record;
    changed.render_watermark.source_server_tick = 99;
    changed.render_watermark.source_server_time_us = 200000;
    changed.render_watermark.rendered_server_time_us = 200000;
    CHECK(Worr_CommandRecordSemanticHashV1(
        &record, 250, &semantic_hash));
    CHECK(Worr_CommandRecordSemanticHashV1(
        &changed, 250, &changed_semantic_hash));
    CHECK(Worr_CommandRecordContentHashV1(&record, 250, &content_hash));
    CHECK(Worr_CommandRecordContentHashV1(
        &changed, 250, &changed_content_hash));
    CHECK(semantic_hash == changed_semantic_hash);
    CHECK(content_hash != changed_content_hash);
    CHECK(Worr_CommandRecordSemanticallyEqualV1(&record, &changed, 250));
    changed.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
    CHECK(!Worr_CommandRecordSemanticallyEqualV1(&record, &changed, 250));

    changed.command.reserved0 = 1;
    CHECK(!Worr_CommandRecordSemanticHashV1(
        &changed, 250, &untouched_hash));
    CHECK(untouched_hash == UINT64_C(0x1122334455667788));
    return 0;
}

static int test_transactional_init_and_insert_semantics(void)
{
    union {
        max_align_t alignment;
        unsigned char bytes[512];
    } overlap;
    unsigned char overlap_before[sizeof(overlap.bytes)];
    worr_command_stream_v1 *overlap_stream;
    worr_command_stream_slot_v1 *overlap_slots;
    worr_command_stream_v1 stream;
    worr_command_stream_v1 stream_before;
    worr_command_stream_slot_v1 slots[4];
    worr_command_stream_slot_v1 slots_before[4];
    worr_command_record_v1 first;
    worr_command_record_v1 second;
    worr_command_record_v1 duplicate;
    worr_command_record_v1 stored;

    memset(&stream, 0xa5, sizeof(stream));
    memset(slots, 0x5a, sizeof(slots));
    stream_before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(!Worr_CommandStreamInitV1(
        &stream, slots, 4, 251, (worr_command_cursor_v1){4, 0}, 0));
    CHECK(memcmp(&stream, &stream_before, sizeof(stream)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(!Worr_CommandStreamInitV1(
        &stream, slots, 0, 250, (worr_command_cursor_v1){4, 0}, 0));
    CHECK(memcmp(&stream, &stream_before, sizeof(stream)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    memset(overlap.bytes, 0x96, sizeof(overlap.bytes));
    memcpy(overlap_before, overlap.bytes, sizeof(overlap.bytes));
    overlap_stream = (worr_command_stream_v1 *)(void *)overlap.bytes;
    overlap_slots = (worr_command_stream_slot_v1 *)(void *)overlap.bytes;
    CHECK(!Worr_CommandStreamInitV1(
        overlap_stream, overlap_slots, 1, 250,
        (worr_command_cursor_v1){4, 0}, 0));
    CHECK(memcmp(overlap.bytes, overlap_before, sizeof(overlap.bytes)) == 0);

    memset(overlap.bytes, 0x69, sizeof(overlap.bytes));
    memcpy(overlap_before, overlap.bytes, sizeof(overlap.bytes));
    overlap_stream =
        (worr_command_stream_v1 *)(void *)(overlap.bytes + 64);
    overlap_slots = (worr_command_stream_slot_v1 *)(void *)overlap.bytes;
    CHECK(!Worr_CommandStreamInitV1(
        overlap_stream, overlap_slots, 1, 250,
        (worr_command_cursor_v1){4, 0}, 0));
    CHECK(memcmp(overlap.bytes, overlap_before, sizeof(overlap.bytes)) == 0);

    CHECK(!Worr_CommandStreamInitV1(
        &stream, slots, 4, 250, (worr_command_cursor_v1){4, 0}, 1));
    CHECK(memcmp(&stream, &stream_before, sizeof(stream)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 4, 250, (worr_command_cursor_v1){4, 0}, 0));
    CHECK(Worr_CommandStreamValidateV1(&stream));
    overlap_slots = stream.slots;
    stream.slots = (worr_command_stream_slot_v1 *)(void *)&stream;
    CHECK(!Worr_CommandStreamValidateV1(&stream));
    stream.slots = overlap_slots;
    CHECK(Worr_CommandStreamValidateV1(&stream));
    first = make_record(4, 1, 0, 0,
                        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    CHECK(Worr_CommandStreamInsertV1(&stream, &first) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(stream.received_cursor.epoch == 4 &&
          stream.received_cursor.contiguous_sequence == 1);
    CHECK(stream.consumed_cursor.epoch == 4 &&
          stream.consumed_cursor.contiguous_sequence == 0);

    stream_before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamInsertV1(&stream, &first) ==
          WORR_COMMAND_STREAM_DUPLICATE);
    CHECK(operational_stream_equal(&stream, &stream_before));
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    duplicate = first;
    duplicate.command.buttons ^= 1u;
    stream_before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamInsertV1(&stream, &duplicate) ==
          WORR_COMMAND_STREAM_CONFLICT);
    CHECK(operational_stream_equal(&stream, &stream_before));
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    duplicate = first;
    duplicate.render_watermark.source_server_tick = 11;
    stream_before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamInsertV1(&stream, &duplicate) ==
          WORR_COMMAND_STREAM_CONFLICT);
    CHECK(operational_stream_equal(&stream, &stream_before));
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    second = make_record(4, 3, 5, 5000,
                         WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    stream_before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamInsertV1(&stream, &second) ==
          WORR_COMMAND_STREAM_FUTURE_GAP);
    CHECK(operational_stream_equal(&stream, &stream_before));
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    second.command_id = (worr_command_id_v1){6, 1};
    CHECK(Worr_CommandStreamInsertV1(&stream, &second) ==
          WORR_COMMAND_STREAM_WRONG_EPOCH);
    second.command_id = (worr_command_id_v1){4, 2};
    second.sample_time_us = 4999;
    CHECK(Worr_CommandStreamInsertV1(&stream, &second) ==
          WORR_COMMAND_STREAM_SAMPLE_TIME_MISMATCH);
    second.sample_time_us = 5000;
    second.command.duration_ms = 251;
    CHECK(Worr_CommandStreamInsertV1(&stream, &second) ==
          WORR_COMMAND_STREAM_INVALID_RECORD);

    second = make_record(
        4, 2, 5, 5000,
        WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED);
    CHECK(Worr_CommandStreamInsertV1(&stream, &second) ==
          WORR_COMMAND_STREAM_INSERTED);
    duplicate = second;
    duplicate.render_watermark.source_server_tick = 99;
    duplicate.render_watermark.source_server_time_us = 200000;
    duplicate.render_watermark.rendered_server_time_us = 200000;
    CHECK(Worr_CommandStreamInsertV1(&stream, &duplicate) ==
          WORR_COMMAND_STREAM_DUPLICATE);
    CHECK(Worr_CommandStreamCopyRecordV1(
        &stream, second.command_id, &stored));
    CHECK(stored.render_watermark.source_server_tick ==
          second.render_watermark.source_server_tick);
    CHECK(stored.render_watermark.source_server_time_us ==
          second.render_watermark.source_server_time_us);
    duplicate.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
    CHECK(Worr_CommandStreamInsertV1(&stream, &duplicate) ==
          WORR_COMMAND_STREAM_CONFLICT);

    stream_before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamInsertV1(&stream, NULL) ==
          WORR_COMMAND_STREAM_INVALID_ARGUMENT);
    CHECK(operational_stream_equal(&stream, &stream_before));
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(stream.telemetry.receive_attempts == 12);
    CHECK(stream.telemetry.inserted == 2);
    CHECK(stream.telemetry.duplicates == 2);
    CHECK(stream.telemetry.conflicts == 3);
    CHECK(stream.telemetry.future_gaps == 1);
    CHECK(stream.telemetry.wrong_epoch == 1);
    CHECK(stream.telemetry.sample_time_rejections == 1);
    CHECK(stream.telemetry.invalid_records == 1);
    CHECK(stream.telemetry.invalid_arguments == 1);
    CHECK(Worr_CommandStreamValidateV1(&stream));
    return 0;
}

static int test_contiguous_consumption_capacity_and_reclamation(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_v1 stream_before;
    worr_command_stream_slot_v1 slots[2];
    worr_command_stream_slot_v1 slots_before[2];
    worr_command_record_v1 first = make_record(
        20, 1, 1, 1000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    worr_command_record_v1 second = make_record(
        20, 2, 0, 1000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    worr_command_record_v1 third = make_record(
        20, 3, 2, 3000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    worr_command_record_v1 output;
    worr_command_record_v1 untouched;

    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 2, 250, (worr_command_cursor_v1){20, 0}, 0));
    CHECK(Worr_CommandStreamInsertV1(&stream, &first) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamInsertV1(&stream, &second) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamPeekNextV1(&stream, &output));
    CHECK(output.command_id.sequence == 1);

    stream_before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamInsertV1(&stream, &third) ==
          WORR_COMMAND_STREAM_CAPACITY);
    CHECK(operational_stream_equal(&stream, &stream_before));
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    memset(&output, 0x6d, sizeof(output));
    untouched = output;
    CHECK(Worr_CommandStreamConsumeV1(&stream, second.command_id, &output) ==
          WORR_COMMAND_STREAM_NOT_READY);
    CHECK(memcmp(&output, &untouched, sizeof(output)) == 0);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, (worr_command_id_v1){21, 1}, &output) ==
          WORR_COMMAND_STREAM_WRONG_EPOCH);
    CHECK(memcmp(&output, &untouched, sizeof(output)) == 0);

    CHECK(Worr_CommandStreamConsumeV1(&stream, first.command_id, &output) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(output.command_id.sequence == 1);
    memset(&output, 0x3c, sizeof(output));
    untouched = output;
    CHECK(Worr_CommandStreamConsumeV1(&stream, first.command_id, &output) ==
          WORR_COMMAND_STREAM_ALREADY_CONSUMED);
    CHECK(memcmp(&output, &untouched, sizeof(output)) == 0);
    CHECK(stream.received_cursor.contiguous_sequence == 2);
    CHECK(stream.consumed_cursor.contiguous_sequence == 1);

    /* Only the contiguous consumed head can be reclaimed. */
    CHECK(Worr_CommandStreamInsertV1(&stream, &third) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(stream.count == 2);
    memset(&output, 0x7e, sizeof(output));
    untouched = output;
    CHECK(!Worr_CommandStreamCopyRecordV1(
        &stream, first.command_id, &output));
    CHECK(memcmp(&output, &untouched, sizeof(output)) == 0);
    CHECK(Worr_CommandStreamCopyRecordV1(
        &stream, second.command_id, &output));
    CHECK(Worr_CommandStreamCopyRecordV1(
        &stream, third.command_id, &output));
    CHECK(Worr_CommandStreamInsertV1(&stream, &first) ==
          WORR_COMMAND_STREAM_STALE);

    CHECK(Worr_CommandStreamConsumeV1(&stream, second.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(Worr_CommandStreamConsumeV1(&stream, third.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(stream.received_cursor.contiguous_sequence == 3);
    CHECK(stream.consumed_cursor.contiguous_sequence == 3);
    memset(&output, 0x2a, sizeof(output));
    untouched = output;
    CHECK(!Worr_CommandStreamPeekNextV1(&stream, &output));
    CHECK(memcmp(&output, &untouched, sizeof(output)) == 0);
    CHECK(stream.telemetry.capacity_stalls == 1);
    CHECK(stream.telemetry.stale == 1);
    CHECK(stream.telemetry.consumed == 3);
    CHECK(stream.telemetry.already_consumed == 1);
    CHECK(stream.telemetry.not_ready == 1);
    CHECK(Worr_CommandStreamValidateV1(&stream));
    return 0;
}

static int test_wrap_reset_exhaustion_overflow_and_saturation(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_v1 stream_before;
    worr_command_stream_v1 exhausted;
    worr_command_stream_v1 terminal;
    worr_command_stream_v1 overflow;
    worr_command_stream_slot_v1 slots[3];
    worr_command_stream_slot_v1 slots_before[3];
    worr_command_stream_slot_v1 exhausted_slot[1];
    worr_command_stream_slot_v1 terminal_slot[1];
    worr_command_stream_slot_v1 overflow_slot[1];
    worr_command_stream_slot_v1 zero_slots[3];
    worr_command_record_v1 last;
    worr_command_record_v1 wrapped;
    worr_command_record_v1 candidate;

    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 3, 250,
        (worr_command_cursor_v1){7, UINT32_MAX - 1u}, 9000));
    last = make_record(
        7, UINT32_MAX, 0, 9000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    wrapped = make_record(
        8, 1, 1, 10000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    CHECK(Worr_CommandStreamInsertV1(&stream, &last) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamInsertV1(&stream, &wrapped) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(stream.received_cursor.epoch == 8 &&
          stream.received_cursor.contiguous_sequence == 1);
    CHECK(stream.consumed_cursor.epoch == 7 &&
          stream.consumed_cursor.contiguous_sequence == UINT32_MAX - 1u);
    CHECK(stream.telemetry.epoch_wraps == 1);
    CHECK(Worr_CommandStreamConsumeV1(&stream, last.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(Worr_CommandStreamConsumeV1(&stream, wrapped.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(stream.consumed_cursor.epoch == 8 &&
          stream.consumed_cursor.contiguous_sequence == 1);

    CHECK(Worr_CommandStreamResetV1(&stream, 9) ==
          WORR_COMMAND_STREAM_RESET);
    memset(zero_slots, 0, sizeof(zero_slots));
    CHECK(memcmp(slots, zero_slots, sizeof(slots)) == 0);
    CHECK(stream.received_cursor.epoch == 9 &&
          stream.received_cursor.contiguous_sequence == 0);
    CHECK(stream.consumed_cursor.epoch == 9 &&
          stream.consumed_cursor.contiguous_sequence == 0);
    CHECK(stream.last_received_sample_time_us == 0);

    stream_before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamResetV1(&stream, 9) ==
          WORR_COMMAND_STREAM_WRONG_EPOCH);
    CHECK(operational_stream_equal(&stream, &stream_before));
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(Worr_CommandStreamResetV1(&stream, 0) ==
          WORR_COMMAND_STREAM_INVALID_ARGUMENT);
    CHECK(operational_stream_equal(&stream, &stream_before));
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    CHECK(Worr_CommandStreamInitV1(
        &exhausted, exhausted_slot, 1, 250,
        (worr_command_cursor_v1){UINT32_MAX, UINT32_MAX}, 123));
    candidate = make_record(
        UINT32_MAX, UINT32_MAX, 0, 123,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    CHECK(Worr_CommandStreamInsertV1(&exhausted, &candidate) ==
          WORR_COMMAND_STREAM_EPOCH_EXHAUSTED);
    CHECK(Worr_CommandStreamResetV1(&exhausted, UINT32_MAX) ==
          WORR_COMMAND_STREAM_EPOCH_EXHAUSTED);
    CHECK(exhausted.telemetry.epoch_exhaustions == 2);

    CHECK(Worr_CommandStreamInitV1(
        &terminal, terminal_slot, 1, 250,
        (worr_command_cursor_v1){UINT32_MAX, UINT32_MAX - 1u}, 123));
    candidate = make_record(
        UINT32_MAX, UINT32_MAX, 0, 123,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    CHECK(Worr_CommandStreamInsertV1(&terminal, &candidate) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamInsertV1(&terminal, &candidate) ==
          WORR_COMMAND_STREAM_DUPLICATE);
    candidate.command.buttons ^= 1u;
    CHECK(Worr_CommandStreamInsertV1(&terminal, &candidate) ==
          WORR_COMMAND_STREAM_CONFLICT);

    CHECK(Worr_CommandStreamInitV1(
        &overflow, overflow_slot, 1, 250,
        (worr_command_cursor_v1){12, 4}, UINT64_MAX - 999u));
    candidate = make_record(
        12, 5, 1, UINT64_MAX,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    CHECK(Worr_CommandStreamInsertV1(&overflow, &candidate) ==
          WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW);
    overflow.telemetry.receive_attempts = UINT64_MAX;
    overflow.telemetry.sample_time_rejections = UINT64_MAX;
    CHECK(Worr_CommandStreamInsertV1(&overflow, &candidate) ==
          WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW);
    CHECK(overflow.telemetry.receive_attempts == UINT64_MAX);
    CHECK(overflow.telemetry.sample_time_rejections == UINT64_MAX);
    CHECK(Worr_CommandStreamValidateV1(&overflow));
    return 0;
}

static int test_bounded_fast_forward(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_v1 before;
    worr_command_stream_v1 pending;
    worr_command_stream_v1 terminal;
    worr_command_stream_v1 overflow;
    worr_command_stream_slot_v1 slots[128];
    worr_command_stream_slot_v1 slots_before[128];
    worr_command_stream_slot_v1 pending_slots[2];
    worr_command_stream_slot_v1 pending_slots_before[2];
    worr_command_stream_slot_v1 terminal_slot[1];
    worr_command_stream_slot_v1 terminal_slot_before[1];
    worr_command_stream_slot_v1 overflow_slot[1];
    worr_command_stream_slot_v1 overflow_slot_before[1];
    worr_command_stream_slot_v1 zero_slots[128];
    worr_command_record_v1 incoming = make_record(
        1, 8911, 1, 8911000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    worr_command_record_v1 pending_record = make_record(
        2, 11, 1, 11000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    worr_command_record_v1 stress_incoming = make_record(
        1, 1983, 1, 1983000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);

    CHECK(prime_consumed_retention(
        &stream, slots, 128, 1, 8749, 173));
    CHECK(stream.count == 128 && stream.head == 45);

    before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamFastForwardV1(&stream, 0, 1) ==
          WORR_COMMAND_STREAM_INVALID_ARGUMENT);
    CHECK(memcmp(&stream, &before, sizeof(stream)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(Worr_CommandStreamFastForwardV1(&stream, 161, 251) ==
          WORR_COMMAND_STREAM_INVALID_ARGUMENT);
    CHECK(memcmp(&stream, &before, sizeof(stream)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);

    CHECK(Worr_CommandStreamFastForwardV1(&stream, 161, 1) ==
          WORR_COMMAND_STREAM_FAST_FORWARDED);
    memset(zero_slots, 0, sizeof(zero_slots));
    CHECK(memcmp(slots, zero_slots, sizeof(slots)) == 0);
    CHECK(stream.head == 0 && stream.count == 0);
    CHECK(stream.received_cursor.epoch == 1 &&
          stream.received_cursor.contiguous_sequence == 8910);
    CHECK(stream.consumed_cursor.epoch == 1 &&
          stream.consumed_cursor.contiguous_sequence == 8910);
    CHECK(stream.last_received_sample_time_us == 8910000);
    CHECK(stream.telemetry.reset_attempts == 0);
    CHECK(stream.telemetry.reset_rejections == 0);
    CHECK(stream.telemetry.resets == 0);
    CHECK(Worr_CommandStreamValidateV1(&stream));
    CHECK(Worr_CommandStreamInsertV1(&stream, &incoming) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, incoming.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(Worr_CommandStreamValidateV1(&stream));

    CHECK(prime_consumed_retention(
        &stream, slots, 128, 1, 1581, 173));
    CHECK(stream.count == 128 && stream.head == 45);
    CHECK(Worr_CommandStreamFastForwardV1(&stream, 401, 1) ==
          WORR_COMMAND_STREAM_FAST_FORWARDED);
    CHECK(stream.received_cursor.epoch == 1 &&
          stream.received_cursor.contiguous_sequence == 1982);
    CHECK(stream.last_received_sample_time_us == 1982000);
    CHECK(Worr_CommandStreamInsertV1(&stream, &stress_incoming) ==
          WORR_COMMAND_STREAM_INSERTED);
    CHECK(Worr_CommandStreamConsumeV1(
              &stream, stress_incoming.command_id, NULL) ==
          WORR_COMMAND_STREAM_CONSUMED);
    CHECK(Worr_CommandStreamValidateV1(&stream));

    CHECK(Worr_CommandStreamInitV1(
        &pending, pending_slots, 2, 250,
        (worr_command_cursor_v1){2, 10}, 10000));
    CHECK(Worr_CommandStreamInsertV1(&pending, &pending_record) ==
          WORR_COMMAND_STREAM_INSERTED);
    before = pending;
    memcpy(pending_slots_before, pending_slots, sizeof(pending_slots));
    CHECK(Worr_CommandStreamFastForwardV1(&pending, 1, 1) ==
          WORR_COMMAND_STREAM_NOT_READY);
    CHECK(memcmp(&pending, &before, sizeof(pending)) == 0);
    CHECK(memcmp(pending_slots, pending_slots_before,
                 sizeof(pending_slots)) == 0);

    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 128, 250,
        (worr_command_cursor_v1){7, UINT32_MAX - 2u}, 1000));
    CHECK(Worr_CommandStreamFastForwardV1(&stream, 3, 0) ==
          WORR_COMMAND_STREAM_FAST_FORWARDED);
    CHECK(stream.received_cursor.epoch == 8 &&
          stream.received_cursor.contiguous_sequence == 1);
    CHECK(stream.last_received_sample_time_us == 1000);
    CHECK(stream.telemetry.epoch_wraps == 1);
    CHECK(Worr_CommandStreamValidateV1(&stream));

    CHECK(Worr_CommandStreamInitV1(
        &terminal, terminal_slot, 1, 250,
        (worr_command_cursor_v1){UINT32_MAX, UINT32_MAX - 1u}, 1));
    before = terminal;
    memcpy(terminal_slot_before, terminal_slot, sizeof(terminal_slot));
    CHECK(Worr_CommandStreamFastForwardV1(&terminal, 2, 0) ==
          WORR_COMMAND_STREAM_EPOCH_EXHAUSTED);
    CHECK(memcmp(&terminal, &before, sizeof(terminal)) == 0);
    CHECK(memcmp(terminal_slot, terminal_slot_before,
                 sizeof(terminal_slot)) == 0);

    CHECK(Worr_CommandStreamInitV1(
        &terminal, terminal_slot, 1, 250,
        (worr_command_cursor_v1){UINT32_MAX, UINT32_MAX - 1u}, 1));
    CHECK(Worr_CommandStreamFastForwardV1(&terminal, 1, 0) ==
          WORR_COMMAND_STREAM_FAST_FORWARDED);
    CHECK(terminal.received_cursor.epoch == UINT32_MAX &&
          terminal.received_cursor.contiguous_sequence == UINT32_MAX);
    before = terminal;
    memcpy(terminal_slot_before, terminal_slot, sizeof(terminal_slot));
    CHECK(Worr_CommandStreamFastForwardV1(&terminal, 1, 0) ==
          WORR_COMMAND_STREAM_EPOCH_EXHAUSTED);
    CHECK(memcmp(&terminal, &before, sizeof(terminal)) == 0);
    CHECK(memcmp(terminal_slot, terminal_slot_before,
                 sizeof(terminal_slot)) == 0);

    CHECK(Worr_CommandStreamInitV1(
        &overflow, overflow_slot, 1, 250,
        (worr_command_cursor_v1){12, 10}, UINT64_MAX - 999u));
    before = overflow;
    memcpy(overflow_slot_before, overflow_slot, sizeof(overflow_slot));
    CHECK(Worr_CommandStreamFastForwardV1(&overflow, 1, 1) ==
          WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW);
    CHECK(memcmp(&overflow, &before, sizeof(overflow)) == 0);
    CHECK(memcmp(overflow_slot, overflow_slot_before,
                 sizeof(overflow_slot)) == 0);
    CHECK(Worr_CommandStreamInitV1(
        &overflow, overflow_slot, 1, 250,
        (worr_command_cursor_v1){12, 10}, UINT64_MAX - 1000u));
    CHECK(Worr_CommandStreamFastForwardV1(&overflow, 1, 1) ==
          WORR_COMMAND_STREAM_FAST_FORWARDED);
    CHECK(overflow.last_received_sample_time_us == UINT64_MAX);
    before = overflow;
    memcpy(overflow_slot_before, overflow_slot, sizeof(overflow_slot));
    CHECK(Worr_CommandStreamFastForwardV1(&overflow, 1, 1) ==
          WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW);
    CHECK(memcmp(&overflow, &before, sizeof(overflow)) == 0);
    CHECK(memcmp(overflow_slot, overflow_slot_before,
                 sizeof(overflow_slot)) == 0);
    return 0;
}

static int test_corrupt_state_fails_without_mutation(void)
{
    worr_command_stream_v1 stream;
    worr_command_stream_v1 before;
    worr_command_stream_slot_v1 slots[2];
    worr_command_stream_slot_v1 slots_before[2];
    worr_command_record_v1 first = make_record(
        30, 1, 1, 1000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);
    worr_command_record_v1 second = make_record(
        30, 2, 1, 2000,
        WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND);

    CHECK(Worr_CommandStreamInitV1(
        &stream, slots, 2, 250, (worr_command_cursor_v1){30, 0}, 0));
    CHECK(Worr_CommandStreamInsertV1(&stream, &first) ==
          WORR_COMMAND_STREAM_INSERTED);
    slots[stream.head].semantic_hash ^= 1;
    CHECK(!Worr_CommandStreamValidateV1(&stream));
    before = stream;
    memcpy(slots_before, slots, sizeof(slots));
    CHECK(Worr_CommandStreamFastForwardV1(&stream, 1, 1) ==
          WORR_COMMAND_STREAM_INVALID_STATE);
    CHECK(memcmp(&stream, &before, sizeof(stream)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    CHECK(Worr_CommandStreamInsertV1(&stream, &second) ==
          WORR_COMMAND_STREAM_INVALID_STATE);
    CHECK(memcmp(&stream, &before, sizeof(stream)) == 0);
    CHECK(memcmp(slots, slots_before, sizeof(slots)) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_identity_duration_watermark_and_hashes() == 0);
    CHECK(test_transactional_init_and_insert_semantics() == 0);
    CHECK(test_contiguous_consumption_capacity_and_reclamation() == 0);
    CHECK(test_wrap_reset_exhaustion_overflow_and_saturation() == 0);
    CHECK(test_bounded_fast_forward() == 0);
    CHECK(test_corrupt_state_fails_without_mutation() == 0);
    printf("command stream: all deterministic ABI/timeline checks passed\n");
    return 0;
}
