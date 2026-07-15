/*
Copyright (C) 2026 WORR contributors

Deterministic tests for the isolated, default-off native command shadow core.
*/

#include "common/net/native_command_shadow.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "native command shadow check failed at "     \
                            "%s:%d: %s\n",                               \
                    __FILE__, __LINE__, #condition);                       \
            return 1;                                                      \
        }                                                                  \
    } while (0)

static worr_prediction_command_v1 make_command(uint8_t duration,
                                                uint8_t marker)
{
    worr_prediction_command_v1 command;
    memset(&command, 0, sizeof(command));
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms = duration;
    command.buttons = marker;
    command.view_angles[0] = (float)marker + 0.125f;
    command.view_angles[1] = -(float)marker - 0.25f;
    command.view_angles[2] = (float)marker * 0.5f;
    command.forward_move = (float)(int8_t)marker;
    command.side_move = -(float)(int8_t)marker;
    return command;
}

static worr_command_record_v1 make_native_record(uint32_t epoch,
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
    record.render_watermark.struct_size =
        sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    record.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_NONE;
    (void)Worr_CommandRecordCanonicalizeV1(
        &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS);
    return record;
}

static worr_command_record_v1 make_legacy_record(
    const worr_command_record_v1 *native_record,
    uint64_t sample_time,
    uint32_t source_tick)
{
    worr_command_record_v1 record = *native_record;
    memset(&record.render_watermark, 0,
           sizeof(record.render_watermark));
    record.sample_time_us = sample_time;
    record.render_watermark.struct_size =
        sizeof(record.render_watermark);
    record.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    record.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED;
    record.render_watermark.source_server_tick = source_tick;
    record.render_watermark.tick_interval_us = 10000;
    record.render_watermark.source_server_time_us =
        (uint64_t)source_tick * UINT64_C(10000);
    record.render_watermark.rendered_server_time_us =
        record.render_watermark.source_server_time_us;
    return record;
}

static bool builder_progress_equal(
    const worr_native_command_shadow_builder_v1 *a,
    const worr_native_command_shadow_builder_v1 *b)
{
    return a->state_flags == b->state_flags &&
           a->command_epoch == b->command_epoch &&
           a->last_sequence == b->last_sequence &&
           a->sample_time_us == b->sample_time_us &&
           a->max_duration_ms == b->max_duration_ms;
}

static int test_builder(void)
{
    worr_native_command_shadow_builder_v1 builder;
    worr_native_command_shadow_builder_v1 before;
    worr_native_command_shadow_builder_v1 untouched;
    worr_prediction_command_v1 command;
    worr_command_record_v1 record;
    worr_command_record_v1 output_before;

    memset(&builder, 0xa5, sizeof(builder));
    untouched = builder;
    CHECK(!Worr_NativeCommandShadowBuilderInitV1(
        &builder, 0, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(memcmp(&builder, &untouched, sizeof(builder)) == 0);
    CHECK(!Worr_NativeCommandShadowBuilderInitV1(
        &builder, 7, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS + 1u));
    CHECK(memcmp(&builder, &untouched, sizeof(builder)) == 0);

    CHECK(Worr_NativeCommandShadowBuilderInitV1(
        &builder, 7, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_NativeCommandShadowBuilderValidateV1(&builder));
    command = make_command(10, 3);
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){7, 1}, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_BUILT);
    CHECK(Worr_CommandRecordValidateV1(
        &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(record.command_id.epoch == 7 && record.command_id.sequence == 1);
    CHECK(record.sample_time_us == 10000);
    CHECK(record.movement_model_revision == WORR_PREDICTION_MODEL_REVISION);
    CHECK(record.render_watermark.provenance ==
          WORR_COMMAND_RENDER_PROVENANCE_NONE);
    CHECK(record.render_watermark.flags == 0 &&
          record.render_watermark.source_server_tick == 0 &&
          record.render_watermark.tick_interval_us == 0 &&
          record.render_watermark.source_server_time_us == 0 &&
          record.render_watermark.rendered_server_time_us == 0);

    command = make_command(0, 4);
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){7, 2}, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_BUILT);
    CHECK(record.sample_time_us == 10000 && builder.sample_time_us == 10000);
    CHECK(builder.telemetry.build_attempts == 2 &&
          builder.telemetry.built == 2);

    before = builder;
    memset(&record, 0xa5, sizeof(record));
    output_before = record;
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){8, 3}, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_WRONG_EPOCH);
    CHECK(builder_progress_equal(&builder, &before));
    CHECK(memcmp(&record, &output_before, sizeof(record)) == 0);

    before = builder;
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){7, 4}, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_OUT_OF_ORDER);
    CHECK(builder_progress_equal(&builder, &before));
    CHECK(memcmp(&record, &output_before, sizeof(record)) == 0);

    command = make_command(251, 5);
    before = builder;
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){7, 3}, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_COMMAND);
    CHECK(builder_progress_equal(&builder, &before));
    CHECK(memcmp(&record, &output_before, sizeof(record)) == 0);

    command = make_command(1, 5);
    command.reserved0 = 1;
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){7, 3}, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_COMMAND);
    CHECK(builder_progress_equal(&builder, &before));

    builder.sample_time_us = UINT64_MAX - 999u;
    command = make_command(1, 6);
    before = builder;
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){7, 3}, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_SAMPLE_TIME_OVERFLOW);
    CHECK(builder_progress_equal(&builder, &before));
    CHECK(memcmp(&record, &output_before, sizeof(record)) == 0);

    CHECK(Worr_NativeCommandShadowBuilderInitV1(
        &builder, 9, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    builder.last_sequence = UINT32_MAX - 1u;
    command = make_command(0, 7);
    CHECK(Worr_NativeCommandShadowBuilderValidateV1(&builder));
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){9, UINT32_MAX},
              &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_BUILT);
    CHECK(record.command_id.sequence == UINT32_MAX &&
          builder.last_sequence == UINT32_MAX &&
          (builder.state_flags &
           WORR_NATIVE_COMMAND_SHADOW_BUILDER_SEQUENCE_EXHAUSTED) != 0);
    CHECK(Worr_NativeCommandShadowBuilderValidateV1(&builder));
    before = builder;
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){9, 1}, &command, &record) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_SEQUENCE_EXHAUSTED);
    CHECK(builder_progress_equal(&builder, &before));

    CHECK(Worr_NativeCommandShadowBuilderInitV1(
        &builder, 11, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    builder.telemetry.invalid_arguments = UINT64_MAX;
    before = builder;
    CHECK(Worr_NativeCommandShadowBuilderBuildV1(
              &builder, (worr_command_id_v1){11, 1}, &command,
              (worr_command_record_v1 *)(void *)&builder) ==
          WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_ARGUMENT);
    CHECK(builder_progress_equal(&builder, &before));
    CHECK(builder.telemetry.invalid_arguments == UINT64_MAX);
    CHECK(memcmp(&builder, &before, sizeof(builder)) == 0);
    CHECK(Worr_NativeCommandShadowBuilderValidateV1(&builder));
    return 0;
}

static int test_payload_registry(void)
{
    worr_native_command_shadow_payload_registry_v1 registry;
    worr_native_command_shadow_payload_registry_v1 untouched;
    worr_command_record_v1 record;
    worr_command_record_v1 decoded;
    worr_command_id_v1 copied_id;
    worr_command_id_v1 copied_id_before;
    uint8_t encoded[WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES];
    uint8_t direct[WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES];
    uint8_t encoded_before[WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES];
    size_t copied_bytes;
    size_t copied_bytes_before;
    size_t direct_bytes;
    uint32_t first_handle;
    uint32_t second_handle;
    uint32_t handles[WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY];
    uint32_t handle_before;
    uint32_t index;

    memset(&registry, 0xa5, sizeof(registry));
    untouched = registry;
    CHECK(!Worr_NativeCommandShadowPayloadRegistryInitV1(
        &registry, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS + 1u));
    CHECK(memcmp(&registry, &untouched, sizeof(registry)) == 0);
    CHECK(Worr_NativeCommandShadowPayloadRegistryInitV1(
        &registry, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry));

    record = make_native_record(21, 1, 10, 3, 10000);
    CHECK(Worr_NativeCommandShadowPayloadRetainV1(
              &registry, &record, &first_handle) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RETAINED);
    CHECK(first_handle != 0 && registry.occupied_count == 1);
    CHECK(Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry));

    memset(encoded, 0xa5, sizeof(encoded));
    copied_bytes = 0;
    copied_id = (worr_command_id_v1){0, 0};
    CHECK(Worr_NativeCommandShadowPayloadCopyV1(
              &registry, first_handle, encoded, sizeof(encoded),
              &copied_bytes, &copied_id) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_COPIED);
    CHECK(copied_bytes == WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES);
    CHECK(copied_id.epoch == 21 && copied_id.sequence == 1);
    CHECK(Worr_NativeCodecCommandEncodeV1(
              &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
              direct, sizeof(direct), &direct_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(direct_bytes == copied_bytes &&
          memcmp(encoded, direct, copied_bytes) == 0);
    CHECK(Worr_NativeCodecCommandDecodeV1(
              encoded, copied_bytes,
              WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, &decoded) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(memcmp(&decoded, &record, sizeof(record)) == 0);

    memcpy(encoded_before, encoded, sizeof(encoded));
    copied_bytes_before = UINT64_C(0xa5a5a5a5);
    copied_bytes = copied_bytes_before;
    copied_id_before = (worr_command_id_v1){0xa5a5a5a5u, 0xa5a5a5a5u};
    copied_id = copied_id_before;
    CHECK(Worr_NativeCommandShadowPayloadCopyV1(
              &registry, first_handle, encoded, sizeof(encoded) - 1u,
              &copied_bytes, &copied_id) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_OUTPUT_TOO_SMALL);
    CHECK(memcmp(encoded, encoded_before, sizeof(encoded)) == 0);
    CHECK(copied_bytes == copied_bytes_before &&
          memcmp(&copied_id, &copied_id_before, sizeof(copied_id)) == 0);

    CHECK(Worr_NativeCommandShadowPayloadCopyV1(
              &registry, first_handle + 1u, encoded, sizeof(encoded),
              &copied_bytes, &copied_id) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_HANDLE);
    untouched = registry;
    CHECK(Worr_NativeCommandShadowPayloadCopyV1(
              &registry, first_handle, &registry, sizeof(encoded),
              &copied_bytes, &copied_id) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_ARGUMENT);
    CHECK(memcmp(&registry, &untouched, sizeof(registry)) == 0);
    CHECK(Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry));

    record.command.buttons ^= 0x7fu;
    CHECK(Worr_NativeCommandShadowPayloadCopyV1(
              &registry, first_handle, encoded, sizeof(encoded),
              &copied_bytes, &copied_id) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_COPIED);
    CHECK(memcmp(encoded, direct, copied_bytes) == 0);

    CHECK(Worr_NativeCommandShadowPayloadReleaseV1(
              &registry, first_handle) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RELEASED);
    CHECK(Worr_NativeCommandShadowPayloadReleaseV1(
              &registry, first_handle) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_HANDLE);
    record = make_native_record(21, 2, 10, 4, 20000);
    CHECK(Worr_NativeCommandShadowPayloadRetainV1(
              &registry, &record, &second_handle) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RETAINED);
    CHECK(second_handle != first_handle);
    CHECK((second_handle & WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_MASK) ==
          (first_handle & WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_MASK));

    CHECK(Worr_NativeCommandShadowPayloadRegistryInitV1(
        &registry, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    for (index = 0; index < WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY;
         ++index) {
        record = make_native_record(22, index + 1u, 1,
                                    (uint8_t)(index + 1u),
                                    (uint64_t)(index + 1u) * 1000u);
        CHECK(Worr_NativeCommandShadowPayloadRetainV1(
                  &registry, &record, &handles[index]) ==
              WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RETAINED);
    }
    CHECK(registry.occupied_count ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY);
    registry.telemetry.capacity_stalls = UINT64_MAX;
    record = make_native_record(22, 65, 1, 66, 65000);
    handle_before = UINT32_C(0xa5a5a5a5);
    second_handle = handle_before;
    CHECK(Worr_NativeCommandShadowPayloadRetainV1(
              &registry, &record, &second_handle) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY_STALL);
    CHECK(second_handle == handle_before &&
          registry.telemetry.capacity_stalls == UINT64_MAX);
    for (index = 0; index < WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY;
         ++index) {
        CHECK(Worr_NativeCommandShadowPayloadReleaseV1(
                  &registry, handles[index]) ==
              WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RELEASED);
    }
    CHECK(registry.occupied_count == 0 &&
          Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry));

    CHECK(Worr_NativeCommandShadowPayloadRegistryInitV1(
        &registry, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    registry.slots[0].generation =
        WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_GENERATION_MAX - 1u;
    CHECK(Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry));
    record = make_native_record(23, 1, 1, 1, 1000);
    CHECK(Worr_NativeCommandShadowPayloadRetainV1(
              &registry, &record, &first_handle) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RETAINED);
    CHECK((first_handle >>
           WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_BITS) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_GENERATION_MAX);
    CHECK(Worr_NativeCommandShadowPayloadReleaseV1(
              &registry, first_handle) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RELEASED);
    CHECK(registry.retired_count == 1 &&
          registry.slots[0].state_flags ==
              WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_RETIRED &&
          registry.telemetry.slots_retired == 1);
    CHECK(Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry));

    handle_before = registry.struct_size;
    CHECK(Worr_NativeCommandShadowPayloadRetainV1(
              &registry, &record, &registry.struct_size) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_ARGUMENT);
    CHECK(registry.struct_size == handle_before &&
          Worr_NativeCommandShadowPayloadRegistryValidateV1(&registry));
    untouched = registry;
    CHECK(Worr_NativeCommandShadowPayloadRetainV1(
              &registry, &record,
              (uint32_t *)(void *)&registry.telemetry.retain_attempts) ==
          WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_ARGUMENT);
    CHECK(memcmp(&registry, &untouched, sizeof(registry)) == 0);
    return 0;
}

static int test_comparator(void)
{
    worr_native_command_shadow_comparator_v1 comparator;
    worr_native_command_shadow_comparator_v1 fresh;
    worr_native_command_shadow_comparator_v1 fresh_before;
    worr_command_record_v1 native_record;
    worr_command_record_v1 legacy_record;
    worr_native_command_shadow_compare_report_v1 report;
    worr_native_command_shadow_compare_report_v1 report_before;

    CHECK(Worr_NativeCommandShadowComparatorInitV1(
        &comparator, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_NativeCommandShadowComparatorValidateV1(&comparator));
    native_record = make_native_record(31, 1, 10, 3, 10000);
    legacy_record = make_legacy_record(&native_record, 60000, 5);
    CHECK(!Worr_CommandRecordSemanticallyEqualV1(
        &native_record, &legacy_record,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_NativeCommandShadowCompareV1(
              &comparator, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED);
    CHECK((report.flags &
           (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
            WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
            WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH |
            WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW |
            WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH |
            WORR_NATIVE_COMMAND_SHADOW_COMPARE_WATERMARK_UNVERIFIED |
            WORR_NATIVE_COMMAND_SHADOW_COMPARE_FULL_RECORD_PARITY_NOT_CLAIMED)) ==
          (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH |
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW |
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH |
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_WATERMARK_UNVERIFIED |
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_FULL_RECORD_PARITY_NOT_CLAIMED));
    CHECK(report.observed_offset_direction ==
              WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_LEGACY_AHEAD &&
          report.expected_offset_direction ==
              WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_LEGACY_AHEAD &&
          report.observed_offset_us == 50000 &&
          report.expected_offset_us == 50000);
    CHECK((comparator.state_flags &
           WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED) != 0 &&
          comparator.sample_offset_us == 50000);

    native_record = make_native_record(31, 2, 10, 4, 20000);
    legacy_record = make_legacy_record(&native_record, 70000, 6);
    CHECK(Worr_NativeCommandShadowCompareV1(
              &comparator, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED);
    CHECK((report.flags &
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW) == 0 &&
          (report.flags &
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH) != 0);

    native_record = make_native_record(31, 3, 10, 5, 30000);
    legacy_record = make_legacy_record(&native_record, 81000, 7);
    CHECK(Worr_NativeCommandShadowCompareV1(
              &comparator, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_SAMPLE_OFFSET_MISMATCH);
    CHECK(report.observed_offset_us == 51000 &&
          report.expected_offset_us == 50000 &&
          (report.flags &
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH) == 0);

    native_record = make_native_record(31, 4, 10, 6, 40000);
    legacy_record = make_legacy_record(&native_record, 90000, 8);
    legacy_record.command.buttons ^= 1u;
    CHECK(Worr_CommandRecordValidateV1(
        &legacy_record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_NativeCommandShadowCompareV1(
              &comparator, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MISMATCH);

    native_record = make_native_record(31, 5, 10, 7, 50000);
    legacy_record = make_legacy_record(&native_record, 100000, 9);
    legacy_record.command_id.sequence = 6;
    CHECK(Worr_NativeCommandShadowCompareV1(
              &comparator, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MISMATCH);
    CHECK(report.native_command_id.sequence == 5 &&
          report.legacy_command_id.sequence == 6 &&
          (report.flags & WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH) == 0);

    CHECK(Worr_NativeCommandShadowComparatorInitV1(
        &fresh, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    native_record = make_native_record(32, 1, 10, 8, 90000);
    legacy_record = make_legacy_record(&native_record, 10000, 1);
    legacy_record.command_id.sequence = 2;
    CHECK(Worr_NativeCommandShadowCompareV1(
              &fresh, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MISMATCH);
    CHECK((fresh.state_flags &
           WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED) == 0 &&
          report.expected_offset_direction ==
              WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_UNSET);
    legacy_record.command_id = native_record.command_id;
    legacy_record.command.buttons ^= 1u;
    CHECK(Worr_NativeCommandShadowCompareV1(
              &fresh, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MISMATCH);
    CHECK((fresh.state_flags &
           WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED) == 0 &&
          report.expected_offset_direction ==
              WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_UNSET);
    legacy_record.command.buttons ^= 1u;
    CHECK(Worr_NativeCommandShadowCompareV1(
              &fresh, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED);
    CHECK(fresh.sample_offset_direction ==
              WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_NATIVE_AHEAD &&
          fresh.sample_offset_us == 80000);

    CHECK(Worr_NativeCommandShadowComparatorInitV1(
        &fresh, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    native_record = make_native_record(34, 1, 0, 10, 0);
    legacy_record = make_legacy_record(&native_record, UINT64_MAX, 1);
    CHECK(Worr_NativeCommandShadowCompareV1(
              &fresh, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED);
    CHECK(report.observed_offset_direction ==
              WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_LEGACY_AHEAD &&
          report.observed_offset_us == UINT64_MAX &&
          report.expected_offset_us == UINT64_MAX);

    CHECK(Worr_NativeCommandShadowComparatorInitV1(
        &fresh, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    native_record = make_native_record(33, 1, 10, 9, 10000);
    legacy_record = make_legacy_record(&native_record, 10000, 1);
    native_record.command.forward_move = 0.0f;
    legacy_record.command.forward_move = -0.0f;
    CHECK(Worr_CommandRecordValidateV1(
              &native_record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) &&
          Worr_CommandRecordValidateV1(
              &legacy_record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_NativeCommandShadowCompareV1(
              &fresh, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED);
    CHECK(fresh.sample_offset_direction ==
          WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL);

    memset(&report, 0xa5, sizeof(report));
    report_before = report;
    native_record.movement_model_revision++;
    CHECK(Worr_NativeCommandShadowCompareV1(
              &fresh, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_RECORD);
    CHECK(memcmp(&report, &report_before, sizeof(report)) == 0);

    native_record.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    fresh.telemetry.watermarks_unverified = UINT64_MAX;
    CHECK(Worr_NativeCommandShadowCompareV1(
              &fresh, &native_record, &legacy_record, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED);
    CHECK(fresh.telemetry.watermarks_unverified == UINT64_MAX);
    fresh_before = fresh;
    CHECK(Worr_NativeCommandShadowCompareV1(
              &fresh, &native_record, &legacy_record,
              (worr_native_command_shadow_compare_report_v1 *)(void *)
                  &fresh.telemetry.compare_attempts) ==
          WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_ARGUMENT);
    CHECK(memcmp(&fresh, &fresh_before, sizeof(fresh)) == 0);
    CHECK(Worr_NativeCommandShadowComparatorValidateV1(&fresh));
    CHECK(comparator.telemetry.matched == 2 &&
          comparator.telemetry.offsets_established == 1 &&
          comparator.telemetry.offset_mismatches == 1 &&
          comparator.telemetry.command_mismatches == 1 &&
          comparator.telemetry.id_mismatches == 1 &&
          comparator.telemetry.watermarks_unverified == 5);
    return 0;
}

static int test_join_orders_conflicts_and_prune(void)
{
    worr_native_command_shadow_join_v1 join;
    worr_native_command_shadow_join_v1 join_before;
    worr_command_record_v1 native_record;
    worr_command_record_v1 legacy_record;
    worr_command_record_v1 conflict;
    worr_native_command_shadow_compare_report_v1 report;
    worr_native_command_shadow_compare_report_v1 report_before;
    worr_native_command_shadow_join_slot_v1 slot;
    uint32_t pruned;
    uint32_t pruned_before;

    CHECK(Worr_NativeCommandShadowJoinInitV1(
        &join, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, 10));
    CHECK(Worr_NativeCommandShadowJoinValidateV1(&join));

    native_record = make_native_record(41, 1, 1, 1, 1000);
    native_record.command.forward_move = 0.0f;
    legacy_record = make_legacy_record(&native_record, 11000, 1);
    memset(&report, 0xa5, sizeof(report));
    report_before = report;
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &native_record, 1, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    CHECK(memcmp(&report, &report_before, sizeof(report)) == 0);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &legacy_record, 2, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED);
    CHECK(report.observed_offset_us == 10000 &&
          (report.flags &
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_WATERMARK_UNVERIFIED) != 0 &&
          (report.flags &
           WORR_NATIVE_COMMAND_SHADOW_COMPARE_FULL_RECORD_PARITY_NOT_CLAIMED) !=
              0);
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, native_record.command_id, &slot) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND);
    CHECK((slot.state_flags &
           (WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE |
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY |
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_COMPARED)) ==
          (WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE |
           WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY |
           WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_COMPARED));

    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &join.slots[0].native_record, 3, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_DUPLICATE_NATIVE);
    conflict = native_record;
    conflict.command.forward_move = -0.0f;
    CHECK(Worr_CommandRecordValidateV1(
        &conflict, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &conflict, 4, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_DUPLICATE_NATIVE);
    conflict = native_record;
    conflict.command.buttons ^= 1u;
    CHECK(Worr_CommandRecordValidateV1(
        &conflict, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &conflict, 4, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_CONFLICTING_NATIVE);
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, native_record.command_id, &slot) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND);
    CHECK(memcmp(&slot.native_record, &native_record,
                 sizeof(native_record)) == 0);

    native_record = make_native_record(41, 2, 1, 2, 2000);
    legacy_record = make_legacy_record(&native_record, 12000, 2);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &legacy_record, 5, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_LEGACY);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &native_record, 6, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED);

    native_record = make_native_record(41, 3, 1, 3, 3000);
    legacy_record = make_legacy_record(&native_record, 13000, 3);
    legacy_record.command.buttons ^= 1u;
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &native_record, 7, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &legacy_record, 8, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_COMMAND_MISMATCH);

    native_record = make_native_record(41, 4, 1, 4, 4000);
    legacy_record = make_legacy_record(&native_record, 14001, 4);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &native_record, 9, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &legacy_record, 10, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_SAMPLE_OFFSET_MISMATCH);
    CHECK(report.observed_offset_us == 10001 &&
          report.expected_offset_us == 10000);

    pruned = UINT32_C(0xa5a5a5a5);
    CHECK(Worr_NativeCommandShadowJoinPruneV1(&join, 11, &pruned) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE);
    CHECK(pruned == 0);
    CHECK(Worr_NativeCommandShadowJoinPruneV1(&join, 12, &pruned) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE);
    CHECK(pruned == 1 && join.occupied_count == 3);
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, (worr_command_id_v1){41, 1}, &slot) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND);

    native_record = make_native_record(41, 5, 1, 5, 5000);
    memset(&report, 0xa5, sizeof(report));
    report_before = report;
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &native_record, 11, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_CLOCK_REGRESSION);
    CHECK(memcmp(&report, &report_before, sizeof(report)) == 0);

    join_before = join;
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &native_record, 13,
              (worr_native_command_shadow_compare_report_v1 *)(void *)
                  &join.telemetry.observe_attempts) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT);
    CHECK(memcmp(&join, &join_before, sizeof(join)) == 0);
    CHECK(Worr_NativeCommandShadowJoinValidateV1(&join));
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &native_record, 13, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);

    conflict = native_record;
    conflict.reserved0 = 1;
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &conflict, 14, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_RECORD);
    CHECK(Worr_NativeCommandShadowJoinValidateV1(&join));

    pruned_before = UINT32_C(0xa5a5a5a5);
    pruned = pruned_before;
    CHECK(Worr_NativeCommandShadowJoinPruneV1(
              &join, 12, &pruned) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_CLOCK_REGRESSION);
    CHECK(pruned == pruned_before);
    join_before = join;
    CHECK(Worr_NativeCommandShadowJoinPruneV1(
              &join, 14,
              (uint32_t *)(void *)&join.telemetry.prune_attempts) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT);
    CHECK(memcmp(&join, &join_before, sizeof(join)) == 0);
    CHECK(Worr_NativeCommandShadowJoinValidateV1(&join));

    CHECK(join.telemetry.native_stored == 5 &&
          join.telemetry.legacy_stored == 4 &&
          join.telemetry.native_duplicates == 2 &&
          join.telemetry.native_conflicts == 1 &&
          join.telemetry.comparisons_completed == 4 &&
          join.telemetry.matches == 2 &&
          join.telemetry.command_mismatches == 1 &&
          join.telemetry.sample_offset_mismatches == 1 &&
          join.telemetry.slots_pruned == 1);
    return 0;
}

static int test_join_deep_validation(void)
{
    worr_native_command_shadow_join_v1 valid;
    worr_native_command_shadow_join_v1 invalid;
    worr_command_record_v1 native_record;
    worr_command_record_v1 legacy_record;
    worr_command_record_v1 second_record;
    worr_command_record_v1 third_record;
    worr_native_command_shadow_compare_report_v1 report;

    CHECK(Worr_NativeCommandShadowJoinInitV1(
        &valid, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, 100));
    native_record = make_native_record(61, 1, 1, 1, 1000);
    legacy_record = make_legacy_record(&native_record, 11000, 1);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &valid, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &native_record, 1, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &valid, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &legacy_record, 2, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED);
    second_record = make_native_record(61, 2, 1, 2, 2000);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &valid, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &second_record, 3, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    legacy_record = make_legacy_record(&second_record, 12000, 2);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &valid, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &legacy_record, 4, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED);
    third_record = make_native_record(61, 3, 1, 3, 3000);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &valid, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &third_record, 5, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    CHECK(Worr_NativeCommandShadowJoinValidateV1(&valid));

    invalid = valid;
    ++invalid.slots[0].comparison.observed_offset_us;
    ++invalid.slots[0].comparison.expected_offset_us;
    CHECK(!Worr_NativeCommandShadowJoinValidateV1(&invalid));

    invalid = valid;
    ++invalid.slots[0].comparison.native_sample_time_us;
    CHECK(!Worr_NativeCommandShadowJoinValidateV1(&invalid));

    invalid = valid;
    ++invalid.slots[0].comparison.native_model_revision;
    CHECK(!Worr_NativeCommandShadowJoinValidateV1(&invalid));

    invalid = valid;
    invalid.slots[0].native_record.command.buttons ^= 1u;
    CHECK(Worr_CommandRecordValidateV1(
        &invalid.slots[0].native_record,
        WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS));
    CHECK(!Worr_NativeCommandShadowJoinValidateV1(&invalid));

    invalid = valid;
    ++invalid.comparator.sample_offset_us;
    CHECK(Worr_NativeCommandShadowComparatorValidateV1(
        &invalid.comparator));
    CHECK(!Worr_NativeCommandShadowJoinValidateV1(&invalid));

    invalid = valid;
    invalid.comparator.state_flags &=
        (uint16_t)~WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED;
    invalid.comparator.sample_offset_direction =
        WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_UNSET;
    invalid.comparator.sample_offset_us = 0;
    CHECK(Worr_NativeCommandShadowComparatorValidateV1(
        &invalid.comparator));
    CHECK(!Worr_NativeCommandShadowJoinValidateV1(&invalid));

    invalid = valid;
    invalid.slots[1].comparison.flags |=
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW;
    CHECK(!Worr_NativeCommandShadowJoinValidateV1(&invalid));

    invalid = valid;
    invalid.slots[2].command_id = invalid.slots[0].command_id;
    invalid.slots[2].native_record.command_id = invalid.slots[0].command_id;
    CHECK(!Worr_NativeCommandShadowJoinValidateV1(&invalid));
    return 0;
}

static int test_join_retire_compared(void)
{
    worr_native_command_shadow_join_v1 join;
    worr_native_command_shadow_join_v1 before;
    worr_native_command_shadow_join_v1 expected;
    worr_command_record_v1 incomplete_record;
    worr_command_record_v1 survivor_record;
    worr_command_record_v1 survivor_legacy;
    worr_command_record_v1 target_record;
    worr_command_record_v1 target_legacy;
    worr_native_command_shadow_join_slot_v1 survivor_slot;
    worr_native_command_shadow_join_slot_v1 incomplete_slot;
    worr_command_id_v1 missing_id = {73, 65};
    uint32_t index;
    uint32_t target_index = UINT32_MAX;

    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              NULL, missing_id) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE);
    memset(&join, 0, sizeof(join));
    before = join;
    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              &join, missing_id) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE);
    CHECK(memcmp(&join, &before, sizeof(join)) == 0);

    CHECK(Worr_NativeCommandShadowJoinInitV1(
        &join, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, 100));
    before = join;
    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              &join, (worr_command_id_v1){0, 0}) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT);
    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              &join, (worr_command_id_v1){71, 0}) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT);
    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              &join, (worr_command_id_v1){0, 65}) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT);
    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              &join, missing_id) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND);
    CHECK(memcmp(&join, &before, sizeof(join)) == 0);

    /*
     * The incomplete record shares the target sequence in another epoch.
     * The survivor shares its epoch and sequence modulo the table capacity.
     */
    incomplete_record = make_native_record(72, 65, 1, 3, 65000);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &incomplete_record, 1, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    before = join;
    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              &join, incomplete_record.command_id) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE);
    CHECK(memcmp(&join, &before, sizeof(join)) == 0);

    target_record = make_native_record(71, 65, 1, 5, 65000);
    target_legacy = make_legacy_record(&target_record, 75000, 65);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &target_record, 2, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &target_legacy, 3, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED);

    survivor_record = make_native_record(71, 1, 1, 4, 1000);
    survivor_legacy = make_legacy_record(&survivor_record, 11000, 1);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &survivor_record, 4, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY,
              &survivor_legacy, 5, NULL) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED);
    CHECK(join.occupied_count == 3 &&
          Worr_NativeCommandShadowJoinValidateV1(&join));

    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, survivor_record.command_id, &survivor_slot) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND);
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, incomplete_record.command_id, &incomplete_slot) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND);
    for (index = 0; index < join.capacity; ++index) {
        if (join.slots[index].command_id.epoch ==
                target_record.command_id.epoch &&
            join.slots[index].command_id.sequence ==
                target_record.command_id.sequence) {
            target_index = index;
            break;
        }
    }
    CHECK(target_index != UINT32_MAX);

    before = join;
    expected = before;
    memset(&expected.slots[target_index], 0,
           sizeof(expected.slots[target_index]));
    --expected.occupied_count;
    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              &join, target_record.command_id) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_RETIRED);
    CHECK(memcmp(&join, &expected, sizeof(join)) == 0);
    CHECK(join.occupied_count == 2 &&
          Worr_NativeCommandShadowJoinValidateV1(&join));
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, target_record.command_id,
              &expected.slots[target_index]) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND);
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, survivor_record.command_id,
              &expected.slots[target_index]) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND);
    CHECK(memcmp(&expected.slots[target_index], &survivor_slot,
                 sizeof(survivor_slot)) == 0);
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, incomplete_record.command_id,
              &expected.slots[target_index]) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND);
    CHECK(memcmp(&expected.slots[target_index], &incomplete_slot,
                 sizeof(incomplete_slot)) == 0);

    before = join;
    CHECK(Worr_NativeCommandShadowJoinRetireComparedV1(
              &join, target_record.command_id) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND);
    CHECK(memcmp(&join, &before, sizeof(join)) == 0);
    return 0;
}

static int test_join_capacity_and_expiry(void)
{
    worr_native_command_shadow_join_v1 join;
    worr_command_record_v1 record;
    worr_native_command_shadow_compare_report_v1 report;
    worr_native_command_shadow_compare_report_v1 report_before;
    worr_native_command_shadow_join_slot_v1 slot;
    uint32_t index;
    uint32_t pruned;

    CHECK(Worr_NativeCommandShadowJoinInitV1(
        &join, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, 1000));
    for (index = 0; index < WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY;
         ++index) {
        record = make_native_record(51, index + 1u, 1,
                                    (uint8_t)(index + 1u),
                                    (uint64_t)(index + 1u) * 1000u);
        CHECK(Worr_NativeCommandShadowJoinObserveV1(
                  &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
                  &record, 0, NULL) ==
              WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE);
    }
    CHECK(join.occupied_count == WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY);
    join.telemetry.capacity_stalls = UINT64_MAX;
    record = make_native_record(51, 65, 1, 65, 65000);
    memset(&report, 0xa5, sizeof(report));
    report_before = report;
    CHECK(Worr_NativeCommandShadowJoinObserveV1(
              &join, WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE,
              &record, 1, &report) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY_STALL);
    CHECK(memcmp(&report, &report_before, sizeof(report)) == 0 &&
          join.telemetry.capacity_stalls == UINT64_MAX);
    CHECK(Worr_NativeCommandShadowJoinValidateV1(&join));

    CHECK(Worr_NativeCommandShadowJoinPruneV1(&join, 999, &pruned) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE);
    CHECK(pruned == 0 && join.occupied_count ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY);
    CHECK(Worr_NativeCommandShadowJoinPruneV1(&join, 1000, &pruned) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE);
    CHECK(pruned == WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY &&
          join.occupied_count == 0);
    CHECK(Worr_NativeCommandShadowJoinValidateV1(&join));

    memset(&slot, 0xa5, sizeof(slot));
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, (worr_command_id_v1){51, 1}, &slot) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND);
    CHECK(slot.state_flags == UINT32_C(0xa5a5a5a5));
    CHECK(Worr_NativeCommandShadowJoinFindV1(
              &join, (worr_command_id_v1){51, 1},
              (worr_native_command_shadow_join_slot_v1 *)(void *)&join) ==
          WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT);
    CHECK(Worr_NativeCommandShadowJoinValidateV1(&join));
    return 0;
}

int main(void)
{
    if (test_builder() != 0 ||
        test_payload_registry() != 0 ||
        test_comparator() != 0 ||
        test_join_orders_conflicts_and_prune() != 0 ||
        test_join_deep_validation() != 0 ||
        test_join_retire_compared() != 0 ||
        test_join_capacity_and_expiry() != 0) {
        return 1;
    }
    puts("native command shadow tests passed");
    return 0;
}
