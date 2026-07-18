/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/command_abi.h"

#include <string.h>

#include "common/net/command_canonical.h"

static uint64_t append_u8(uint64_t hash, uint8_t value)
{
    hash ^= value;
    return hash * UINT64_C(1099511628211);
}

static uint64_t append_u32(uint64_t hash, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        hash = append_u8(hash, (uint8_t)(value >> (i * 8u)));
    return hash;
}

static uint64_t append_u64(uint64_t hash, uint64_t value)
{
    unsigned i;
    for (i = 0; i < 8; ++i)
        hash = append_u8(hash, (uint8_t)(value >> (i * 8u)));
    return hash;
}

static uint64_t begin_hash(uint32_t domain)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = append_u32(hash, UINT32_C(0x52524f57)); /* WORR, little endian */
    hash = append_u32(hash, WORR_COMMAND_ABI_VERSION);
    return append_u32(hash, domain);
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    /* Match the prediction hash contract by collapsing signed zero. */
    if ((bits & UINT32_C(0x7fffffff)) == 0)
        return 0;
    return bits;
}

static bool float_equal(float a, float b)
{
    return float_bits(a) == float_bits(b);
}

static bool command_equal(const worr_prediction_command_v1 *a,
                          const worr_prediction_command_v1 *b)
{
    unsigned i;
    if (a->struct_size != b->struct_size ||
        a->schema_version != b->schema_version ||
        a->duration_ms != b->duration_ms || a->buttons != b->buttons ||
        a->reserved0 != b->reserved0 ||
        !float_equal(a->forward_move, b->forward_move) ||
        !float_equal(a->side_move, b->side_move)) {
        return false;
    }
    for (i = 0; i < 3; ++i) {
        if (!float_equal(a->view_angles[i], b->view_angles[i]))
            return false;
    }
    return true;
}

static bool watermark_equal(
    const worr_command_render_watermark_v1 *a,
    const worr_command_render_watermark_v1 *b)
{
    return a->struct_size == b->struct_size &&
           a->schema_version == b->schema_version &&
           a->provenance == b->provenance && a->flags == b->flags &&
           a->source_server_tick == b->source_server_tick &&
           a->tick_interval_us == b->tick_interval_us &&
           a->source_server_time_us == b->source_server_time_us &&
           a->rendered_server_time_us == b->rendered_server_time_us;
}

static bool command_canonicalize(worr_prediction_command_v1 *command,
                                 uint16_t max_duration_ms)
{
    worr_prediction_command_v1 output;
    unsigned i;

    if (command->struct_size != sizeof(*command) ||
        command->schema_version != WORR_PREDICTION_ABI_VERSION ||
        command->reserved0 != 0 ||
        command->duration_ms > max_duration_ms) {
        return false;
    }

    output = *command;
    for (i = 0; i < 3; ++i) {
        if (!NetUsercmd_CanonicalizeAngle(command->view_angles[i],
                                          &output.view_angles[i])) {
            return false;
        }
    }
    if (!NetUsercmd_CanonicalizeMove(command->forward_move,
                                     &output.forward_move) ||
        !NetUsercmd_CanonicalizeMove(command->side_move,
                                     &output.side_move)) {
        return false;
    }
    *command = output;
    return true;
}

static uint64_t append_command(uint64_t hash,
                               const worr_prediction_command_v1 *command)
{
    unsigned i;
    hash = append_u32(hash, command->duration_ms);
    hash = append_u32(hash, command->buttons);
    for (i = 0; i < 3; ++i)
        hash = append_u32(hash, float_bits(command->view_angles[i]));
    hash = append_u32(hash, float_bits(command->forward_move));
    return append_u32(hash, float_bits(command->side_move));
}

static uint64_t append_watermark(
    uint64_t hash,
    const worr_command_render_watermark_v1 *watermark,
    bool semantic_projection)
{
    hash = append_u32(hash, watermark->provenance);
    if (semantic_projection &&
        watermark->provenance ==
            WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED) {
        return hash;
    }
    hash = append_u32(hash, watermark->flags);
    hash = append_u32(hash, watermark->source_server_tick);
    hash = append_u32(hash, watermark->tick_interval_us);
    hash = append_u64(hash, watermark->source_server_time_us);
    return append_u64(hash, watermark->rendered_server_time_us);
}

static uint64_t append_record_core(uint64_t hash,
                                   const worr_command_record_v1 *record)
{
    hash = append_u32(hash, record->command_id.epoch);
    hash = append_u32(hash, record->command_id.sequence);
    hash = append_u64(hash, record->sample_time_us);
    hash = append_u32(hash, record->movement_model_revision);
    return append_command(hash, &record->command);
}

bool Worr_CommandDurationLimitValidV1(uint16_t max_duration_ms)
{
    return max_duration_ms <= WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS;
}

bool Worr_CommandIdValidV1(worr_command_id_v1 command_id,
                           bool allow_absent)
{
    if (command_id.epoch == 0 || command_id.sequence == 0) {
        return allow_absent && command_id.epoch == 0 &&
               command_id.sequence == 0;
    }
    return true;
}

bool Worr_CommandIdNextV1(worr_command_id_v1 current,
                          worr_command_id_v1 *next)
{
    worr_command_id_v1 output;
    if (!next || !Worr_CommandIdValidV1(current, false))
        return false;
    output = current;
    if (output.sequence != UINT32_MAX) {
        ++output.sequence;
    } else {
        if (output.epoch == UINT32_MAX)
            return false;
        ++output.epoch;
        output.sequence = 1;
    }
    *next = output;
    return true;
}

bool Worr_CommandCursorValidV1(worr_command_cursor_v1 cursor)
{
    return cursor.epoch != 0;
}

bool Worr_CommandCursorNextIdV1(worr_command_cursor_v1 cursor,
                                worr_command_id_v1 *next)
{
    worr_command_id_v1 output;
    if (!next || !Worr_CommandCursorValidV1(cursor))
        return false;
    if (cursor.contiguous_sequence == UINT32_MAX) {
        if (cursor.epoch == UINT32_MAX)
            return false;
        output.epoch = cursor.epoch + 1;
        output.sequence = 1;
    } else {
        output.epoch = cursor.epoch;
        output.sequence = cursor.contiguous_sequence + 1;
    }
    *next = output;
    return true;
}

bool Worr_CommandCursorGapBeforeV1(
    worr_command_cursor_v1 cursor,
    worr_command_id_v1 later_command,
    uint32_t maximum_gap,
    uint32_t *gap_out)
{
    uint64_t gap;

    if (!gap_out || !Worr_CommandCursorValidV1(cursor) ||
        !Worr_CommandIdValidV1(later_command, false)) {
        return false;
    }

    if (later_command.epoch == cursor.epoch) {
        if (later_command.sequence <= cursor.contiguous_sequence)
            return false;
        gap = (uint64_t)later_command.sequence -
              cursor.contiguous_sequence - 1u;
    } else {
        uint64_t epoch_delta;
        if (later_command.epoch < cursor.epoch)
            return false;

        epoch_delta = (uint64_t)later_command.epoch - cursor.epoch;
        /*
         * Each complete epoch contains UINT32_MAX command identities.
         * The largest possible result is UINT32_MAX squared minus one,
         * which is representable in uint64_t.
         */
        gap = (uint64_t)UINT32_MAX - cursor.contiguous_sequence;
        gap += (epoch_delta - 1u) * (uint64_t)UINT32_MAX;
        gap += (uint64_t)later_command.sequence - 1u;
    }

    if (gap > maximum_gap)
        return false;
    *gap_out = (uint32_t)gap;
    return true;
}

bool Worr_CommandRenderWatermarkValidateV1(
    const worr_command_render_watermark_v1 *watermark)
{
    const uint32_t timing_flags = WORR_COMMAND_RENDER_INTERPOLATED |
                                  WORR_COMMAND_RENDER_EXTRAPOLATED;
    uint64_t render_delta;
    uint64_t extrapolation_limit;

    if (!watermark || watermark->struct_size != sizeof(*watermark) ||
        watermark->schema_version != WORR_COMMAND_ABI_VERSION ||
        watermark->provenance >
            WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED ||
        (watermark->flags & ~timing_flags) != 0 ||
        (watermark->flags & timing_flags) == timing_flags) {
        return false;
    }

    if (watermark->provenance == WORR_COMMAND_RENDER_PROVENANCE_NONE) {
        return watermark->flags == 0 && watermark->source_server_tick == 0 &&
               watermark->tick_interval_us == 0 &&
               watermark->source_server_time_us == 0 &&
               watermark->rendered_server_time_us == 0;
    }

    if (watermark->tick_interval_us == 0 ||
        watermark->tick_interval_us > WORR_COMMAND_MAX_TICK_INTERVAL_US) {
        return false;
    }

    if ((watermark->flags & WORR_COMMAND_RENDER_INTERPOLATED) != 0) {
        if (watermark->rendered_server_time_us >=
            watermark->source_server_time_us) {
            return false;
        }
        render_delta = watermark->source_server_time_us -
                       watermark->rendered_server_time_us;
        return render_delta != 0 &&
               render_delta <= WORR_COMMAND_MAX_RENDER_OFFSET_US;
    }
    if ((watermark->flags & WORR_COMMAND_RENDER_EXTRAPOLATED) != 0) {
        if (watermark->rendered_server_time_us <=
            watermark->source_server_time_us) {
            return false;
        }
        render_delta = watermark->rendered_server_time_us -
                       watermark->source_server_time_us;
        extrapolation_limit =
            (uint64_t)watermark->tick_interval_us *
            WORR_COMMAND_MAX_EXTRAPOLATION_INTERVALS;
        if (extrapolation_limit > WORR_COMMAND_MAX_RENDER_OFFSET_US)
            extrapolation_limit = WORR_COMMAND_MAX_RENDER_OFFSET_US;
        return render_delta <= extrapolation_limit;
    }
    return watermark->rendered_server_time_us ==
           watermark->source_server_time_us;
}

bool Worr_CommandRecordCanonicalizeV1(worr_command_record_v1 *record,
                                      uint16_t max_duration_ms)
{
    worr_command_record_v1 output;
    if (!record || !Worr_CommandDurationLimitValidV1(max_duration_ms))
        return false;
    output = *record;
    if (output.struct_size != sizeof(output) ||
        output.schema_version != WORR_COMMAND_ABI_VERSION ||
        !Worr_CommandIdValidV1(output.command_id, false) ||
        output.movement_model_revision != WORR_PREDICTION_MODEL_REVISION ||
        output.reserved0 != 0 ||
        !Worr_CommandRenderWatermarkValidateV1(&output.render_watermark) ||
        !command_canonicalize(&output.command, max_duration_ms)) {
        return false;
    }
    *record = output;
    return true;
}

bool Worr_CommandRecordValidateV1(const worr_command_record_v1 *record,
                                  uint16_t max_duration_ms)
{
    worr_command_record_v1 canonical;
    if (!record)
        return false;
    canonical = *record;
    return Worr_CommandRecordCanonicalizeV1(&canonical, max_duration_ms) &&
           command_equal(&canonical.command, &record->command);
}

bool Worr_CommandRecordSemanticHashV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    uint64_t *hash_out)
{
    uint64_t hash;
    if (!hash_out ||
        !Worr_CommandRecordValidateV1(record, max_duration_ms)) {
        return false;
    }
    hash = begin_hash(UINT32_C(0x4d455343)); /* CSEM */
    hash = append_record_core(hash, record);
    hash = append_watermark(hash, &record->render_watermark, true);
    *hash_out = hash;
    return true;
}

bool Worr_CommandRecordInputHashV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    uint64_t *hash_out)
{
    uint64_t hash;
    if (!hash_out ||
        !Worr_CommandRecordValidateV1(record, max_duration_ms)) {
        return false;
    }
    hash = begin_hash(UINT32_C(0x54504e49)); /* INPT */
    *hash_out = append_record_core(hash, record);
    return true;
}

bool Worr_CommandRecordContentHashV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    uint64_t *hash_out)
{
    uint64_t hash;
    if (!hash_out ||
        !Worr_CommandRecordValidateV1(record, max_duration_ms)) {
        return false;
    }
    hash = begin_hash(UINT32_C(0x544e4f43)); /* CONT */
    hash = append_record_core(hash, record);
    hash = append_watermark(hash, &record->render_watermark, false);
    *hash_out = hash;
    return true;
}

bool Worr_CommandRecordSemanticallyEqualV1(
    const worr_command_record_v1 *a,
    const worr_command_record_v1 *b,
    uint16_t max_duration_ms)
{
    if (!Worr_CommandRecordValidateV1(a, max_duration_ms) ||
        !Worr_CommandRecordValidateV1(b, max_duration_ms) ||
        a->command_id.epoch != b->command_id.epoch ||
        a->command_id.sequence != b->command_id.sequence ||
        a->sample_time_us != b->sample_time_us ||
        a->movement_model_revision != b->movement_model_revision ||
        !command_equal(&a->command, &b->command) ||
        a->render_watermark.provenance != b->render_watermark.provenance) {
        return false;
    }
    if (a->render_watermark.provenance ==
        WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED) {
        return true;
    }
    return watermark_equal(&a->render_watermark, &b->render_watermark);
}
