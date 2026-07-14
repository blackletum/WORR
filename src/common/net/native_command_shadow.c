/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_command_shadow.h"

#include <string.h>

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static void saturating_add(uint64_t *value, uint64_t amount)
{
    *value = *value > UINT64_MAX - amount ? UINT64_MAX : *value + amount;
}

static bool all_zero(const void *data, size_t bytes)
{
    const uint8_t *cursor = (const uint8_t *)data;
    size_t index;
    for (index = 0; index < bytes; ++index) {
        if (cursor[index] != 0)
            return false;
    }
    return true;
}

static bool ranges_overlap(const void *a, size_t a_bytes,
                           const void *b, size_t b_bytes)
{
    uintptr_t a_begin;
    uintptr_t a_end;
    uintptr_t b_begin;
    uintptr_t b_end;
    if (!a || !b || a_bytes == 0 || b_bytes == 0)
        return false;
    a_begin = (uintptr_t)a;
    b_begin = (uintptr_t)b;
    if (a_bytes > UINTPTR_MAX - a_begin ||
        b_bytes > UINTPTR_MAX - b_begin) {
        return true;
    }
    a_end = a_begin + a_bytes;
    b_end = b_begin + b_bytes;
    return a_begin < b_end && b_begin < a_end;
}

static bool command_id_equal(worr_command_id_v1 a,
                             worr_command_id_v1 b)
{
    return a.epoch == b.epoch && a.sequence == b.sequence;
}

static uint32_t canonical_float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    if ((bits & UINT32_C(0x7fffffff)) == 0)
        return 0;
    return bits;
}

static bool prediction_command_equal(
    const worr_prediction_command_v1 *a,
    const worr_prediction_command_v1 *b)
{
    unsigned index;
    if (a->struct_size != b->struct_size ||
        a->schema_version != b->schema_version ||
        a->duration_ms != b->duration_ms ||
        a->buttons != b->buttons ||
        a->reserved0 != b->reserved0 ||
        canonical_float_bits(a->forward_move) !=
            canonical_float_bits(b->forward_move) ||
        canonical_float_bits(a->side_move) !=
            canonical_float_bits(b->side_move)) {
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (canonical_float_bits(a->view_angles[index]) !=
            canonical_float_bits(b->view_angles[index])) {
            return false;
        }
    }
    return true;
}

static void sample_offset(uint64_t native_time, uint64_t legacy_time,
                          uint8_t *direction_out, uint64_t *offset_out)
{
    if (native_time == legacy_time) {
        *direction_out = WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL;
        *offset_out = 0;
    } else if (legacy_time > native_time) {
        *direction_out =
            WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_LEGACY_AHEAD;
        *offset_out = legacy_time - native_time;
    } else {
        *direction_out =
            WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_NATIVE_AHEAD;
        *offset_out = native_time - legacy_time;
    }
}

bool Worr_NativeCommandShadowBuilderInitV1(
    worr_native_command_shadow_builder_v1 *builder_out,
    uint32_t command_epoch,
    uint16_t max_duration_ms)
{
    worr_native_command_shadow_builder_v1 initialized;
    if (!builder_out || command_epoch == 0 ||
        !Worr_CommandDurationLimitValidV1(max_duration_ms)) {
        return false;
    }
    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION;
    initialized.state_flags =
        WORR_NATIVE_COMMAND_SHADOW_BUILDER_INITIALIZED;
    initialized.command_epoch = command_epoch;
    initialized.max_duration_ms = max_duration_ms;
    *builder_out = initialized;
    return true;
}

bool Worr_NativeCommandShadowBuilderValidateV1(
    const worr_native_command_shadow_builder_v1 *builder)
{
    const uint16_t allowed_flags =
        WORR_NATIVE_COMMAND_SHADOW_BUILDER_INITIALIZED |
        WORR_NATIVE_COMMAND_SHADOW_BUILDER_SEQUENCE_EXHAUSTED;
    if (!builder || builder->struct_size != sizeof(*builder) ||
        builder->schema_version !=
            WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION ||
        (builder->state_flags &
         WORR_NATIVE_COMMAND_SHADOW_BUILDER_INITIALIZED) == 0 ||
        (builder->state_flags & ~allowed_flags) != 0 ||
        builder->command_epoch == 0 ||
        !Worr_CommandDurationLimitValidV1(builder->max_duration_ms) ||
        builder->reserved0 != 0 || builder->reserved1 != 0) {
        return false;
    }
    return ((builder->state_flags &
             WORR_NATIVE_COMMAND_SHADOW_BUILDER_SEQUENCE_EXHAUSTED) != 0) ==
           (builder->last_sequence == UINT32_MAX);
}

worr_native_command_shadow_build_result_v1
Worr_NativeCommandShadowBuilderBuildV1(
    worr_native_command_shadow_builder_v1 *builder,
    worr_command_id_v1 command_id,
    const worr_prediction_command_v1 *command,
    worr_command_record_v1 *record_out)
{
    worr_native_command_shadow_builder_v1 updated;
    worr_prediction_command_v1 command_copy;
    worr_command_record_v1 candidate;
    uint64_t duration_us;

    if (!Worr_NativeCommandShadowBuilderValidateV1(builder))
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_STATE;
    if (record_out &&
        ranges_overlap(builder, sizeof(*builder),
                       record_out, sizeof(*record_out))) {
        /* Telemetry is part of the aliased output region.  Preserve the
         * promised failure image instead of trying to count this rejection. */
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_ARGUMENT;
    }
    if (!command || !record_out) {
        saturating_increment(&builder->telemetry.build_attempts);
        saturating_increment(&builder->telemetry.invalid_arguments);
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_ARGUMENT;
    }
    command_copy = *command;
    saturating_increment(&builder->telemetry.build_attempts);
    if (!Worr_CommandIdValidV1(command_id, false)) {
        saturating_increment(&builder->telemetry.invalid_arguments);
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_ARGUMENT;
    }
    if (command_id.epoch != builder->command_epoch) {
        saturating_increment(&builder->telemetry.wrong_epochs);
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_WRONG_EPOCH;
    }
    if ((builder->state_flags &
         WORR_NATIVE_COMMAND_SHADOW_BUILDER_SEQUENCE_EXHAUSTED) != 0) {
        saturating_increment(&builder->telemetry.sequence_exhaustions);
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_SEQUENCE_EXHAUSTED;
    }
    if (command_id.sequence != builder->last_sequence + 1u) {
        saturating_increment(&builder->telemetry.out_of_order);
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_OUT_OF_ORDER;
    }
    if (command_copy.duration_ms > builder->max_duration_ms) {
        saturating_increment(&builder->telemetry.invalid_commands);
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_COMMAND;
    }

    duration_us = (uint64_t)command_copy.duration_ms * UINT64_C(1000);
    if (builder->sample_time_us > UINT64_MAX - duration_us) {
        saturating_increment(&builder->telemetry.sample_time_overflows);
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_SAMPLE_TIME_OVERFLOW;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_COMMAND_ABI_VERSION;
    candidate.command_id = command_id;
    candidate.sample_time_us = builder->sample_time_us + duration_us;
    candidate.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    candidate.command = command_copy;
    candidate.render_watermark.struct_size =
        sizeof(candidate.render_watermark);
    candidate.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    candidate.render_watermark.provenance =
        WORR_COMMAND_RENDER_PROVENANCE_NONE;
    if (!Worr_CommandRecordCanonicalizeV1(
            &candidate, builder->max_duration_ms) ||
        !Worr_CommandRecordValidateV1(
            &candidate, builder->max_duration_ms)) {
        saturating_increment(&builder->telemetry.invalid_commands);
        return WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_COMMAND;
    }

    updated = *builder;
    updated.last_sequence = command_id.sequence;
    updated.sample_time_us = candidate.sample_time_us;
    saturating_increment(&updated.telemetry.built);
    if (updated.last_sequence == UINT32_MAX) {
        updated.state_flags |=
            WORR_NATIVE_COMMAND_SHADOW_BUILDER_SEQUENCE_EXHAUSTED;
    }
    *builder = updated;
    *record_out = candidate;
    return WORR_NATIVE_COMMAND_SHADOW_BUILD_BUILT;
}

static bool payload_registry_header_valid(
    const worr_native_command_shadow_payload_registry_v1 *registry)
{
    return registry && registry->struct_size == sizeof(*registry) &&
           registry->schema_version ==
               WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION &&
           registry->state_flags ==
               WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_REGISTRY_INITIALIZED &&
           registry->capacity ==
               WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY &&
           registry->occupied_count <= registry->capacity &&
           registry->retired_count <= registry->capacity &&
           (uint32_t)registry->occupied_count + registry->retired_count <=
               registry->capacity &&
           registry->next_slot < registry->capacity &&
           Worr_CommandDurationLimitValidV1(registry->max_duration_ms) &&
           registry->reserved0 == 0 && registry->reserved1 == 0;
}

static uint32_t payload_handle(uint32_t generation, uint32_t index)
{
    return (generation <<
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_BITS) |
           index;
}

static bool payload_slot_zero_except_generation(
    const worr_native_command_shadow_payload_slot_v1 *slot)
{
    return slot->handle == 0 &&
           slot->command_id.epoch == 0 &&
           slot->command_id.sequence == 0 &&
           slot->encoded_bytes == 0 && slot->reserved0 == 0 &&
           slot->reserved1 == 0 &&
           all_zero(slot->encoded, sizeof(slot->encoded));
}

bool Worr_NativeCommandShadowPayloadRegistryInitV1(
    worr_native_command_shadow_payload_registry_v1 *registry_out,
    uint16_t max_duration_ms)
{
    worr_native_command_shadow_payload_registry_v1 initialized;
    if (!registry_out ||
        !Worr_CommandDurationLimitValidV1(max_duration_ms)) {
        return false;
    }
    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION;
    initialized.state_flags =
        WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_REGISTRY_INITIALIZED;
    initialized.capacity = WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY;
    initialized.max_duration_ms = max_duration_ms;
    *registry_out = initialized;
    return true;
}

bool Worr_NativeCommandShadowPayloadRegistryValidateV1(
    const worr_native_command_shadow_payload_registry_v1 *registry)
{
    uint16_t occupied = 0;
    uint16_t retired = 0;
    uint32_t index;
    if (!payload_registry_header_valid(registry))
        return false;
    for (index = 0; index < registry->capacity; ++index) {
        const worr_native_command_shadow_payload_slot_v1 *slot =
            &registry->slots[index];
        const uint16_t allowed_flags =
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_OCCUPIED |
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_RETIRED;
        if ((slot->state_flags & ~allowed_flags) != 0 ||
            slot->reserved0 != 0 || slot->reserved1 != 0) {
            return false;
        }
        if ((slot->state_flags &
             WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_OCCUPIED) != 0) {
            worr_command_record_v1 decoded;
            if ((slot->state_flags &
                 WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_RETIRED) != 0 ||
                slot->generation == 0 ||
                slot->generation >
                    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_GENERATION_MAX ||
                slot->handle != payload_handle(slot->generation, index) ||
                slot->encoded_bytes !=
                    WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES ||
                !Worr_CommandIdValidV1(slot->command_id, false) ||
                Worr_NativeCodecCommandDecodeV1(
                    slot->encoded, slot->encoded_bytes,
                    registry->max_duration_ms, &decoded) !=
                    WORR_NATIVE_CODEC_OK ||
                !command_id_equal(decoded.command_id, slot->command_id)) {
                return false;
            }
            ++occupied;
        } else if ((slot->state_flags &
                    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_RETIRED) != 0) {
            if (slot->generation !=
                    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_GENERATION_MAX ||
                !payload_slot_zero_except_generation(slot)) {
                return false;
            }
            ++retired;
        } else if (slot->generation >=
                       WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_GENERATION_MAX ||
                   !payload_slot_zero_except_generation(slot)) {
            return false;
        }
    }
    return occupied == registry->occupied_count &&
           retired == registry->retired_count;
}

static worr_native_command_shadow_payload_slot_v1 *payload_find(
    worr_native_command_shadow_payload_registry_v1 *registry,
    uint32_t handle,
    uint32_t *index_out)
{
    const uint32_t index =
        handle & WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_MASK;
    const uint32_t generation =
        handle >> WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_BITS;
    worr_native_command_shadow_payload_slot_v1 *slot;
    if (generation == 0 || index >= registry->capacity)
        return NULL;
    slot = &registry->slots[index];
    if ((slot->state_flags &
         WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_OCCUPIED) == 0 ||
        slot->handle != handle || slot->generation != generation) {
        return NULL;
    }
    if (index_out)
        *index_out = index;
    return slot;
}

worr_native_command_shadow_payload_result_v1
Worr_NativeCommandShadowPayloadRetainV1(
    worr_native_command_shadow_payload_registry_v1 *registry,
    const worr_command_record_v1 *record,
    uint32_t *handle_out)
{
    worr_command_record_v1 record_copy;
    uint8_t encoded[WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES];
    size_t encoded_bytes = 0;
    uint32_t offset;
    uint32_t selected = UINT32_MAX;
    uint32_t handle;
    worr_native_command_shadow_payload_slot_v1 retained;

    if (!Worr_NativeCommandShadowPayloadRegistryValidateV1(registry))
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_STATE;
    if (handle_out &&
        ranges_overlap(registry, sizeof(*registry),
                       handle_out, sizeof(*handle_out))) {
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_ARGUMENT;
    }
    if (!record || !handle_out) {
        saturating_increment(&registry->telemetry.retain_attempts);
        saturating_increment(&registry->telemetry.invalid_arguments);
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_ARGUMENT;
    }
    record_copy = *record;
    saturating_increment(&registry->telemetry.retain_attempts);
    if (!Worr_CommandRecordValidateV1(
            &record_copy, registry->max_duration_ms) ||
        Worr_NativeCodecCommandEncodeV1(
            &record_copy, registry->max_duration_ms,
            encoded, sizeof(encoded), &encoded_bytes) !=
            WORR_NATIVE_CODEC_OK ||
        encoded_bytes != WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES) {
        saturating_increment(&registry->telemetry.invalid_records);
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_RECORD;
    }
    for (offset = 0; offset < registry->capacity; ++offset) {
        const uint32_t index =
            (registry->next_slot + offset) % registry->capacity;
        if (registry->slots[index].state_flags == 0) {
            selected = index;
            break;
        }
    }
    if (selected == UINT32_MAX) {
        saturating_increment(&registry->telemetry.capacity_stalls);
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY_STALL;
    }

    memset(&retained, 0, sizeof(retained));
    retained.generation = registry->slots[selected].generation + 1u;
    retained.handle = payload_handle(retained.generation, selected);
    retained.command_id = record_copy.command_id;
    retained.encoded_bytes = WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES;
    retained.state_flags =
        WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_OCCUPIED;
    memcpy(retained.encoded, encoded, sizeof(encoded));
    handle = retained.handle;

    registry->slots[selected] = retained;
    ++registry->occupied_count;
    registry->next_slot =
        (uint16_t)((selected + 1u) % registry->capacity);
    saturating_increment(&registry->telemetry.retained);
    *handle_out = handle;
    return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RETAINED;
}

worr_native_command_shadow_payload_result_v1
Worr_NativeCommandShadowPayloadCopyV1(
    worr_native_command_shadow_payload_registry_v1 *registry,
    uint32_t handle,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out,
    worr_command_id_v1 *command_id_out)
{
    worr_native_command_shadow_payload_slot_v1 *slot;
    if (!Worr_NativeCommandShadowPayloadRegistryValidateV1(registry))
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_STATE;
    if ((encoded_out &&
         ranges_overlap(registry, sizeof(*registry),
                       encoded_out,
                       WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES)) ||
        ranges_overlap(registry, sizeof(*registry),
                       encoded_bytes_out, sizeof(*encoded_bytes_out)) ||
        (command_id_out &&
         ranges_overlap(registry, sizeof(*registry),
                        command_id_out, sizeof(*command_id_out)))) {
        /* Counting would mutate a requested output that aliases registry. */
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_ARGUMENT;
    }
    saturating_increment(&registry->telemetry.copy_attempts);
    if (!encoded_out || !encoded_bytes_out ||
        ranges_overlap(encoded_out,
                       WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES,
                       encoded_bytes_out, sizeof(*encoded_bytes_out)) ||
        (command_id_out &&
         (ranges_overlap(encoded_out,
                         WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES,
                         command_id_out, sizeof(*command_id_out)) ||
          ranges_overlap(encoded_bytes_out, sizeof(*encoded_bytes_out),
                         command_id_out, sizeof(*command_id_out))))) {
        saturating_increment(&registry->telemetry.invalid_arguments);
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_ARGUMENT;
    }
    slot = payload_find(registry, handle, NULL);
    if (!slot) {
        saturating_increment(&registry->telemetry.invalid_handles);
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_HANDLE;
    }
    if (encoded_capacity < slot->encoded_bytes) {
        saturating_increment(&registry->telemetry.output_too_small);
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_OUTPUT_TOO_SMALL;
    }
    memcpy(encoded_out, slot->encoded, slot->encoded_bytes);
    *encoded_bytes_out = slot->encoded_bytes;
    if (command_id_out)
        *command_id_out = slot->command_id;
    saturating_increment(&registry->telemetry.copied);
    return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_COPIED;
}

worr_native_command_shadow_payload_result_v1
Worr_NativeCommandShadowPayloadReleaseV1(
    worr_native_command_shadow_payload_registry_v1 *registry,
    uint32_t handle)
{
    worr_native_command_shadow_payload_slot_v1 *slot;
    uint32_t index;
    uint32_t generation;
    if (!Worr_NativeCommandShadowPayloadRegistryValidateV1(registry))
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_STATE;
    saturating_increment(&registry->telemetry.release_attempts);
    slot = payload_find(registry, handle, &index);
    if (!slot) {
        saturating_increment(&registry->telemetry.invalid_handles);
        return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_HANDLE;
    }
    generation = slot->generation;
    memset(slot, 0, sizeof(*slot));
    slot->generation = generation;
    --registry->occupied_count;
    if (generation ==
        WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_GENERATION_MAX) {
        slot->state_flags =
            WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_RETIRED;
        ++registry->retired_count;
        saturating_increment(&registry->telemetry.slots_retired);
    } else {
        registry->next_slot = (uint16_t)index;
    }
    saturating_increment(&registry->telemetry.released);
    return WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RELEASED;
}

bool Worr_NativeCommandShadowComparatorInitV1(
    worr_native_command_shadow_comparator_v1 *comparator_out,
    uint16_t max_duration_ms)
{
    worr_native_command_shadow_comparator_v1 initialized;
    if (!comparator_out ||
        !Worr_CommandDurationLimitValidV1(max_duration_ms)) {
        return false;
    }
    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION;
    initialized.state_flags =
        WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_INITIALIZED;
    initialized.max_duration_ms = max_duration_ms;
    *comparator_out = initialized;
    return true;
}

bool Worr_NativeCommandShadowComparatorValidateV1(
    const worr_native_command_shadow_comparator_v1 *comparator)
{
    const uint16_t allowed_flags =
        WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_INITIALIZED |
        WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED;
    const bool established =
        comparator &&
        (comparator->state_flags &
         WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED) != 0;
    if (!comparator || comparator->struct_size != sizeof(*comparator) ||
        comparator->schema_version !=
            WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION ||
        (comparator->state_flags &
         WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_INITIALIZED) == 0 ||
        (comparator->state_flags & ~allowed_flags) != 0 ||
        !Worr_CommandDurationLimitValidV1(comparator->max_duration_ms) ||
        comparator->reserved0 != 0 || comparator->reserved1 != 0) {
        return false;
    }
    if (!established) {
        return comparator->sample_offset_direction ==
                   WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_UNSET &&
               comparator->sample_offset_us == 0;
    }
    if (comparator->sample_offset_direction <
            WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL ||
        comparator->sample_offset_direction >
            WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_NATIVE_AHEAD) {
        return false;
    }
    return (comparator->sample_offset_direction ==
            WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL) ==
           (comparator->sample_offset_us == 0);
}

static bool compare_report_validate(
    const worr_native_command_shadow_compare_report_v1 *report)
{
    const uint32_t common_flags =
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_WATERMARK_UNVERIFIED |
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_FULL_RECORD_PARITY_NOT_CLAIMED;
    const uint32_t allowed_flags =
        common_flags | WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH |
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW |
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH;
    uint8_t computed_direction;
    uint64_t computed_offset;
    if (!report || report->struct_size != sizeof(*report) ||
        report->schema_version !=
            WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION ||
        report->result >
            WORR_NATIVE_COMMAND_SHADOW_COMPARE_SAMPLE_OFFSET_MISMATCH ||
        (report->flags & ~allowed_flags) != 0 ||
        (report->flags & common_flags) != common_flags ||
        !all_zero(report->reserved0, sizeof(report->reserved0)) ||
        report->reserved1 != 0 ||
        !Worr_CommandIdValidV1(report->native_command_id, false) ||
        !Worr_CommandIdValidV1(report->legacy_command_id, false) ||
        report->native_model_revision != WORR_PREDICTION_MODEL_REVISION ||
        report->legacy_model_revision != WORR_PREDICTION_MODEL_REVISION ||
        report->observed_offset_direction <
            WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL ||
        report->observed_offset_direction >
            WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_NATIVE_AHEAD ||
        ((report->observed_offset_direction ==
          WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL) !=
         (report->observed_offset_us == 0))) {
        return false;
    }
    sample_offset(report->native_sample_time_us,
                  report->legacy_sample_time_us,
                  &computed_direction, &computed_offset);
    if (report->observed_offset_direction != computed_direction ||
        report->observed_offset_us != computed_offset) {
        return false;
    }
    if (report->expected_offset_direction ==
        WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_UNSET) {
        if (report->expected_offset_us != 0)
            return false;
    } else if (report->expected_offset_direction <
                   WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL ||
               report->expected_offset_direction >
                   WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_NATIVE_AHEAD ||
               ((report->expected_offset_direction ==
                 WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL) !=
                (report->expected_offset_us == 0))) {
        return false;
    }
    if ((report->flags & WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH) != 0 &&
        (report->expected_offset_direction !=
             report->observed_offset_direction ||
         report->expected_offset_us != report->observed_offset_us)) {
        return false;
    }
    if ((report->flags &
         WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW) != 0 &&
        (report->flags & WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH) == 0) {
        return false;
    }
    switch ((worr_native_command_shadow_compare_result_v1)report->result) {
    case WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED:
        return (report->flags &
                (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH)) ==
               (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH |
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH);
    case WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MISMATCH:
        return (report->flags &
                (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW)) ==
               0;
    case WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MISMATCH:
        return (report->flags &
                (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW)) ==
               (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH);
    case WORR_NATIVE_COMMAND_SHADOW_COMPARE_SAMPLE_OFFSET_MISMATCH:
        return (report->flags &
                (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH |
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW)) ==
               (WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH);
    default:
        return false;
    }
}

worr_native_command_shadow_compare_result_v1
Worr_NativeCommandShadowCompareV1(
    worr_native_command_shadow_comparator_v1 *comparator,
    const worr_command_record_v1 *native_record,
    const worr_command_record_v1 *legacy_record,
    worr_native_command_shadow_compare_report_v1 *report_out)
{
    worr_native_command_shadow_comparator_v1 updated;
    worr_command_record_v1 native_copy;
    worr_command_record_v1 legacy_copy;
    worr_native_command_shadow_compare_report_v1 report;
    uint8_t observed_direction;
    uint64_t observed_offset;
    worr_native_command_shadow_compare_result_v1 result;

    if (!Worr_NativeCommandShadowComparatorValidateV1(comparator))
        return WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_STATE;
    if (report_out &&
        ranges_overlap(comparator, sizeof(*comparator),
                       report_out, sizeof(*report_out))) {
        return WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_ARGUMENT;
    }
    if (!native_record || !legacy_record || !report_out) {
        saturating_increment(&comparator->telemetry.compare_attempts);
        return WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_ARGUMENT;
    }
    native_copy = *native_record;
    legacy_copy = *legacy_record;
    saturating_increment(&comparator->telemetry.compare_attempts);
    if (!Worr_CommandRecordValidateV1(
            &native_copy, comparator->max_duration_ms) ||
        !Worr_CommandRecordValidateV1(
            &legacy_copy, comparator->max_duration_ms)) {
        saturating_increment(&comparator->telemetry.invalid_records);
        return WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_RECORD;
    }

    sample_offset(native_copy.sample_time_us, legacy_copy.sample_time_us,
                  &observed_direction, &observed_offset);
    memset(&report, 0, sizeof(report));
    report.struct_size = sizeof(report);
    report.schema_version = WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION;
    report.flags =
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_WATERMARK_UNVERIFIED |
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_FULL_RECORD_PARITY_NOT_CLAIMED;
    report.observed_offset_direction = observed_direction;
    report.native_command_id = native_copy.command_id;
    report.legacy_command_id = legacy_copy.command_id;
    report.native_sample_time_us = native_copy.sample_time_us;
    report.legacy_sample_time_us = legacy_copy.sample_time_us;
    report.observed_offset_us = observed_offset;
    report.native_model_revision = native_copy.movement_model_revision;
    report.legacy_model_revision = legacy_copy.movement_model_revision;
    if ((comparator->state_flags &
         WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED) != 0) {
        report.expected_offset_direction =
            comparator->sample_offset_direction;
        report.expected_offset_us = comparator->sample_offset_us;
    }
    saturating_increment(&comparator->telemetry.watermarks_unverified);

    if (!command_id_equal(native_copy.command_id,
                          legacy_copy.command_id)) {
        result = WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MISMATCH;
        saturating_increment(&comparator->telemetry.id_mismatches);
    } else if (!prediction_command_equal(&native_copy.command,
                                         &legacy_copy.command)) {
        report.flags |= WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                        WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH;
        result = WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MISMATCH;
        saturating_increment(&comparator->telemetry.command_mismatches);
    } else {
        report.flags |= WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH |
                        WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH |
                        WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH;
        if ((comparator->state_flags &
             WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED) == 0) {
            updated = *comparator;
            updated.state_flags |=
                WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED;
            updated.sample_offset_direction = observed_direction;
            updated.sample_offset_us = observed_offset;
            report.expected_offset_direction = observed_direction;
            report.expected_offset_us = observed_offset;
            report.flags |=
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW |
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH;
            saturating_increment(&updated.telemetry.offsets_established);
            saturating_increment(&updated.telemetry.matched);
            *comparator = updated;
            result =
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED;
        } else if (comparator->sample_offset_direction !=
                       observed_direction ||
                   comparator->sample_offset_us != observed_offset) {
            result =
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_SAMPLE_OFFSET_MISMATCH;
            saturating_increment(&comparator->telemetry.offset_mismatches);
        } else {
            report.flags |=
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH;
            result =
                WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED;
            saturating_increment(&comparator->telemetry.matched);
        }
    }
    report.result = (uint16_t)result;
    *report_out = report;
    return result;
}

static bool compare_report_is_join_result(
    const worr_native_command_shadow_compare_report_v1 *report,
    const worr_command_record_v1 *native_record,
    const worr_command_record_v1 *legacy_record)
{
    if (!compare_report_validate(report) ||
        !command_id_equal(report->native_command_id,
                          native_record->command_id) ||
        !command_id_equal(report->legacy_command_id,
                          legacy_record->command_id) ||
        report->native_sample_time_us != native_record->sample_time_us ||
        report->legacy_sample_time_us != legacy_record->sample_time_us ||
        report->native_model_revision !=
            native_record->movement_model_revision ||
        report->legacy_model_revision !=
            legacy_record->movement_model_revision) {
        return false;
    }
    if (!prediction_command_equal(&native_record->command,
                                  &legacy_record->command)) {
        return report->result ==
               WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MISMATCH;
    }
    if (report->result ==
        WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED) {
        return report->expected_offset_direction ==
                   report->observed_offset_direction &&
               report->expected_offset_us == report->observed_offset_us;
    }
    return report->result ==
               WORR_NATIVE_COMMAND_SHADOW_COMPARE_SAMPLE_OFFSET_MISMATCH &&
           report->expected_offset_direction !=
               WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_UNSET &&
           (report->expected_offset_direction !=
                report->observed_offset_direction ||
            report->expected_offset_us != report->observed_offset_us);
}

bool Worr_NativeCommandShadowJoinInitV1(
    worr_native_command_shadow_join_v1 *join_out,
    uint16_t max_duration_ms,
    uint64_t expiry_ticks)
{
    worr_native_command_shadow_join_v1 initialized;
    if (!join_out || expiry_ticks == 0 ||
        !Worr_CommandDurationLimitValidV1(max_duration_ms)) {
        return false;
    }
    memset(&initialized, 0, sizeof(initialized));
    initialized.struct_size = sizeof(initialized);
    initialized.schema_version = WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION;
    initialized.state_flags = WORR_NATIVE_COMMAND_SHADOW_JOIN_INITIALIZED;
    initialized.capacity = WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY;
    initialized.max_duration_ms = max_duration_ms;
    initialized.expiry_ticks = expiry_ticks;
    if (!Worr_NativeCommandShadowComparatorInitV1(
            &initialized.comparator, max_duration_ms)) {
        return false;
    }
    *join_out = initialized;
    return true;
}

bool Worr_NativeCommandShadowJoinValidateV1(
    const worr_native_command_shadow_join_v1 *join)
{
    uint16_t occupied = 0;
    uint16_t offset_establishers = 0;
    uint32_t index;
    uint32_t earlier;
    if (!join || join->struct_size != sizeof(*join) ||
        join->schema_version != WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION ||
        join->state_flags != WORR_NATIVE_COMMAND_SHADOW_JOIN_INITIALIZED ||
        join->capacity != WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY ||
        join->occupied_count > join->capacity ||
        !Worr_CommandDurationLimitValidV1(join->max_duration_ms) ||
        join->reserved0 != 0 || join->expiry_ticks == 0 ||
        !Worr_NativeCommandShadowComparatorValidateV1(&join->comparator) ||
        join->comparator.max_duration_ms != join->max_duration_ms) {
        return false;
    }
    for (index = 0; index < join->capacity; ++index) {
        const worr_native_command_shadow_join_slot_v1 *slot =
            &join->slots[index];
        const uint32_t allowed_flags =
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_OCCUPIED |
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE |
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY |
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_COMPARED;
        if (slot->state_flags == 0) {
            if (!all_zero(slot, sizeof(*slot)))
                return false;
            continue;
        }
        if ((slot->state_flags & ~allowed_flags) != 0 ||
            (slot->state_flags &
             WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_OCCUPIED) == 0 ||
            (slot->state_flags &
             (WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE |
              WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY)) == 0 ||
            slot->reserved0 != 0 ||
            !Worr_CommandIdValidV1(slot->command_id, false) ||
            slot->last_update_tick > join->last_tick) {
            return false;
        }
        for (earlier = 0; earlier < index; ++earlier) {
            if ((join->slots[earlier].state_flags &
                 WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_OCCUPIED) != 0 &&
                command_id_equal(join->slots[earlier].command_id,
                                 slot->command_id)) {
                return false;
            }
        }
        if ((slot->state_flags &
             WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE) != 0) {
            if (!Worr_CommandRecordValidateV1(
                    &slot->native_record, join->max_duration_ms) ||
                !command_id_equal(slot->native_record.command_id,
                                  slot->command_id)) {
                return false;
            }
        } else if (!all_zero(&slot->native_record,
                             sizeof(slot->native_record))) {
            return false;
        }
        if ((slot->state_flags &
             WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY) != 0) {
            if (!Worr_CommandRecordValidateV1(
                    &slot->legacy_record, join->max_duration_ms) ||
                !command_id_equal(slot->legacy_record.command_id,
                                  slot->command_id)) {
                return false;
            }
        } else if (!all_zero(&slot->legacy_record,
                             sizeof(slot->legacy_record))) {
            return false;
        }
        if ((slot->state_flags &
             WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_COMPARED) != 0) {
            if ((slot->state_flags &
                 (WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE |
                  WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY)) !=
                    (WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE |
                     WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY) ||
                !compare_report_is_join_result(
                    &slot->comparison, &slot->native_record,
                    &slot->legacy_record)) {
                return false;
            }
            if (slot->comparison.expected_offset_direction !=
                WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_UNSET) {
                if ((join->comparator.state_flags &
                     WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED) ==
                        0 ||
                    slot->comparison.expected_offset_direction !=
                        join->comparator.sample_offset_direction ||
                    slot->comparison.expected_offset_us !=
                        join->comparator.sample_offset_us) {
                    return false;
                }
            }
            if ((slot->comparison.flags &
                 WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW) !=
                0) {
                ++offset_establishers;
                if (offset_establishers > 1)
                    return false;
            }
        } else if (!all_zero(&slot->comparison,
                             sizeof(slot->comparison))) {
            return false;
        }
        ++occupied;
    }
    return occupied == join->occupied_count;
}

static int join_find_index(const worr_native_command_shadow_join_v1 *join,
                           worr_command_id_v1 command_id)
{
    uint32_t index;
    for (index = 0; index < join->capacity; ++index) {
        if ((join->slots[index].state_flags &
             WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_OCCUPIED) != 0 &&
            command_id_equal(join->slots[index].command_id, command_id)) {
            return (int)index;
        }
    }
    return -1;
}

static int join_find_free_index(
    const worr_native_command_shadow_join_v1 *join)
{
    uint32_t index;
    for (index = 0; index < join->capacity; ++index) {
        if (join->slots[index].state_flags == 0)
            return (int)index;
    }
    return -1;
}

static worr_native_command_shadow_join_result_v1 join_compare_result(
    worr_native_command_shadow_compare_result_v1 result)
{
    switch (result) {
    case WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED:
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED;
    case WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MISMATCH:
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_COMMAND_MISMATCH;
    case WORR_NATIVE_COMMAND_SHADOW_COMPARE_SAMPLE_OFFSET_MISMATCH:
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_SAMPLE_OFFSET_MISMATCH;
    default:
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE;
    }
}

worr_native_command_shadow_join_result_v1
Worr_NativeCommandShadowJoinObserveV1(
    worr_native_command_shadow_join_v1 *join,
    worr_native_command_shadow_join_side_v1 side,
    const worr_command_record_v1 *record,
    uint64_t now_tick,
    worr_native_command_shadow_compare_report_v1 *report_out)
{
    worr_command_record_v1 record_copy;
    worr_native_command_shadow_join_slot_v1 updated_slot;
    worr_native_command_shadow_comparator_v1 updated_comparator;
    worr_native_command_shadow_compare_report_v1 report;
    worr_native_command_shadow_compare_result_v1 compare_result;
    worr_native_command_shadow_join_result_v1 result;
    uint32_t side_flag;
    int index;

    if (!Worr_NativeCommandShadowJoinValidateV1(join))
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE;
    if (report_out &&
        ranges_overlap(join, sizeof(*join),
                       report_out, sizeof(*report_out))) {
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT;
    }
    if ((side != WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE &&
         side != WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY) ||
        !record) {
        saturating_increment(&join->telemetry.observe_attempts);
        saturating_increment(&join->telemetry.invalid_arguments);
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT;
    }
    record_copy = *record;
    saturating_increment(&join->telemetry.observe_attempts);
    if (!Worr_CommandRecordValidateV1(
            &record_copy, join->max_duration_ms)) {
        saturating_increment(&join->telemetry.invalid_records);
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_RECORD;
    }
    if (now_tick < join->last_tick) {
        saturating_increment(&join->telemetry.clock_regressions);
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_CLOCK_REGRESSION;
    }
    side_flag = side == WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE
                    ? WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE
                    : WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY;
    index = join_find_index(join, record_copy.command_id);
    if (index < 0) {
        index = join_find_free_index(join);
        if (index < 0) {
            join->last_tick = now_tick;
            saturating_increment(&join->telemetry.capacity_stalls);
            return WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY_STALL;
        }
        memset(&updated_slot, 0, sizeof(updated_slot));
        updated_slot.command_id = record_copy.command_id;
        updated_slot.last_update_tick = now_tick;
        updated_slot.state_flags =
            WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_OCCUPIED | side_flag;
        if (side == WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE) {
            updated_slot.native_record = record_copy;
            result = WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE;
            saturating_increment(&join->telemetry.native_stored);
        } else {
            updated_slot.legacy_record = record_copy;
            result = WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_LEGACY;
            saturating_increment(&join->telemetry.legacy_stored);
        }
        join->slots[index] = updated_slot;
        ++join->occupied_count;
        join->last_tick = now_tick;
        return result;
    }

    if ((join->slots[index].state_flags & side_flag) != 0) {
        const worr_command_record_v1 *retained =
            side == WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE
                ? &join->slots[index].native_record
                : &join->slots[index].legacy_record;
        join->last_tick = now_tick;
        if (Worr_CommandRecordSemanticallyEqualV1(
                retained, &record_copy, join->max_duration_ms)) {
            if (side == WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE) {
                saturating_increment(&join->telemetry.native_duplicates);
                return WORR_NATIVE_COMMAND_SHADOW_JOIN_DUPLICATE_NATIVE;
            }
            saturating_increment(&join->telemetry.legacy_duplicates);
            return WORR_NATIVE_COMMAND_SHADOW_JOIN_DUPLICATE_LEGACY;
        }
        if (side == WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE) {
            saturating_increment(&join->telemetry.native_conflicts);
            return WORR_NATIVE_COMMAND_SHADOW_JOIN_CONFLICTING_NATIVE;
        }
        saturating_increment(&join->telemetry.legacy_conflicts);
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_CONFLICTING_LEGACY;
    }

    updated_slot = join->slots[index];
    updated_slot.last_update_tick = now_tick;
    updated_slot.state_flags |= side_flag;
    if (side == WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE) {
        updated_slot.native_record = record_copy;
        saturating_increment(&join->telemetry.native_stored);
    } else {
        updated_slot.legacy_record = record_copy;
        saturating_increment(&join->telemetry.legacy_stored);
    }
    updated_comparator = join->comparator;
    compare_result = Worr_NativeCommandShadowCompareV1(
        &updated_comparator, &updated_slot.native_record,
        &updated_slot.legacy_record, &report);
    result = join_compare_result(compare_result);
    if (result == WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE)
        return result;

    updated_slot.state_flags |=
        WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_COMPARED;
    updated_slot.comparison = report;
    join->slots[index] = updated_slot;
    join->comparator = updated_comparator;
    join->last_tick = now_tick;
    saturating_increment(&join->telemetry.comparisons_completed);
    if (result ==
        WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED) {
        saturating_increment(&join->telemetry.matches);
    } else if (result ==
               WORR_NATIVE_COMMAND_SHADOW_JOIN_COMMAND_MISMATCH) {
        saturating_increment(&join->telemetry.command_mismatches);
    } else {
        saturating_increment(&join->telemetry.sample_offset_mismatches);
    }
    if (report_out)
        *report_out = report;
    return result;
}

worr_native_command_shadow_join_result_v1
Worr_NativeCommandShadowJoinPruneV1(
    worr_native_command_shadow_join_v1 *join,
    uint64_t now_tick,
    uint32_t *pruned_count_out)
{
    uint32_t index;
    uint32_t pruned = 0;
    if (!Worr_NativeCommandShadowJoinValidateV1(join))
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE;
    if (pruned_count_out &&
        ranges_overlap(join, sizeof(*join),
                       pruned_count_out, sizeof(*pruned_count_out))) {
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT;
    }
    saturating_increment(&join->telemetry.prune_attempts);
    if (!pruned_count_out) {
        saturating_increment(&join->telemetry.invalid_arguments);
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT;
    }
    if (now_tick < join->last_tick) {
        saturating_increment(&join->telemetry.clock_regressions);
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_CLOCK_REGRESSION;
    }
    for (index = 0; index < join->capacity; ++index) {
        worr_native_command_shadow_join_slot_v1 *slot =
            &join->slots[index];
        if ((slot->state_flags &
             WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_OCCUPIED) != 0 &&
            now_tick - slot->last_update_tick >= join->expiry_ticks) {
            memset(slot, 0, sizeof(*slot));
            ++pruned;
        }
    }
    join->occupied_count =
        (uint16_t)(join->occupied_count - pruned);
    join->last_tick = now_tick;
    saturating_add(&join->telemetry.slots_pruned, pruned);
    *pruned_count_out = pruned;
    return WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE;
}

worr_native_command_shadow_join_result_v1
Worr_NativeCommandShadowJoinFindV1(
    const worr_native_command_shadow_join_v1 *join,
    worr_command_id_v1 command_id,
    worr_native_command_shadow_join_slot_v1 *slot_out)
{
    int index;
    if (!Worr_NativeCommandShadowJoinValidateV1(join))
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE;
    if (!Worr_CommandIdValidV1(command_id, false) || !slot_out ||
        ranges_overlap(join, sizeof(*join),
                       slot_out, sizeof(*slot_out))) {
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT;
    }
    index = join_find_index(join, command_id);
    if (index < 0)
        return WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND;
    *slot_out = join->slots[index];
    return WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND;
}
