/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/consumed_cursor_sideband.h"

#include <string.h>

#define WORR_CONSUMED_CURSOR_FNV_OFFSET UINT32_C(2166136261)
#define WORR_CONSUMED_CURSOR_FNV_PRIME UINT32_C(16777619)
#define WORR_CONSUMED_CURSOR_CHECKSUM_DOMAIN UINT32_C(0x43555231)
#define WORR_CONSUMED_CURSOR_COMMIT_DOMAIN UINT32_C(0x53434631)

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static uint32_t append_u32(uint32_t hash, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) {
        hash ^= value & UINT32_C(0xff);
        hash *= WORR_CONSUMED_CURSOR_FNV_PRIME;
        value >>= 8;
    }
    return hash;
}

static uint32_t rotate_left(uint32_t value, unsigned shift)
{
    return (value << shift) | (value >> (32u - shift));
}

static uint32_t cursor_checksum(worr_command_cursor_v1 cursor)
{
    uint32_t hash = WORR_CONSUMED_CURSOR_FNV_OFFSET;
    hash = append_u32(hash, WORR_CONSUMED_CURSOR_CHECKSUM_DOMAIN);
    hash = append_u32(hash, WORR_CONSUMED_CURSOR_SIDEBAND_VERSION);
    hash = append_u32(hash, cursor.epoch);
    return append_u32(hash, cursor.contiguous_sequence);
}

static uint32_t cursor_commit(
    const worr_consumed_cursor_sideband_v1 *sideband)
{
    return WORR_CONSUMED_CURSOR_COMMIT_DOMAIN ^
           rotate_left(sideband->header_checksum, 11) ^
           rotate_left(sideband->consumed_cursor.epoch, 3) ^
           rotate_left(sideband->consumed_cursor.contiguous_sequence, 19);
}

static int32_t signed_bits(uint32_t value)
{
    int32_t output;
    memcpy(&output, &value, sizeof(output));
    return output;
}

static uint32_t unsigned_bits(int32_t value)
{
    uint32_t output;
    memcpy(&output, &value, sizeof(output));
    return output;
}

static bool cursor_allowed(worr_command_cursor_v1 cursor)
{
    return cursor.epoch != 0 || cursor.contiguous_sequence == 0;
}

static bool range_disjoint(const void *a, size_t a_size,
                           const void *b, size_t b_size)
{
    uintptr_t a_begin;
    uintptr_t b_begin;
    uintptr_t a_end;
    uintptr_t b_end;
    if (!a || !b)
        return false;
    a_begin = (uintptr_t)a;
    b_begin = (uintptr_t)b;
    if (a_size > UINTPTR_MAX - a_begin || b_size > UINTPTR_MAX - b_begin)
        return false;
    a_end = a_begin + a_size;
    b_end = b_begin + b_size;
    return a_end <= b_begin || b_end <= a_begin;
}

static bool sideband_is_zero(
    const worr_consumed_cursor_sideband_v1 *sideband)
{
    return sideband->struct_size == 0 && sideband->schema_version == 0 &&
           sideband->consumed_cursor.epoch == 0 &&
           sideband->consumed_cursor.contiguous_sequence == 0 &&
           sideband->header_checksum == 0 && sideband->reserved0 == 0;
}

static void parser_clear_pending(
    worr_consumed_cursor_sideband_parser_v1 *parser)
{
    memset(&parser->pending, 0, sizeof(parser->pending));
    parser->observed_checksum = 0;
    parser->reserved0 = 0;
}

static void parser_idle(worr_consumed_cursor_sideband_parser_v1 *parser)
{
    parser_clear_pending(parser);
    parser->phase = WORR_CONSUMED_CURSOR_PHASE_IDLE;
}

static void parser_poison(worr_consumed_cursor_sideband_parser_v1 *parser)
{
    parser_clear_pending(parser);
    parser->phase = WORR_CONSUMED_CURSOR_PHASE_POISONED;
}

static bool phase_has_dangling_header(uint32_t phase)
{
    return phase >= WORR_CONSUMED_CURSOR_PHASE_EPOCH &&
           phase <= WORR_CONSUMED_CURSOR_PHASE_FRAME;
}

static bool setting_index_recognized(int32_t index)
{
    return index >= WORR_CONSUMED_CURSOR_SETTING_BEGIN &&
           index <= WORR_CONSUMED_CURSOR_SETTING_COMMIT;
}

static int32_t expected_index(uint32_t phase)
{
    switch (phase) {
    case WORR_CONSUMED_CURSOR_PHASE_EPOCH:
        return WORR_CONSUMED_CURSOR_SETTING_EPOCH;
    case WORR_CONSUMED_CURSOR_PHASE_SEQUENCE:
        return WORR_CONSUMED_CURSOR_SETTING_SEQUENCE;
    case WORR_CONSUMED_CURSOR_PHASE_CHECKSUM:
        return WORR_CONSUMED_CURSOR_SETTING_CHECKSUM;
    case WORR_CONSUMED_CURSOR_PHASE_COMMIT:
        return WORR_CONSUMED_CURSOR_SETTING_COMMIT;
    default:
        return 0;
    }
}

bool Worr_ConsumedCursorSidebandInitV1(
    worr_consumed_cursor_sideband_v1 *sideband,
    worr_command_cursor_v1 consumed_cursor)
{
    worr_consumed_cursor_sideband_v1 output;
    if (!sideband || !cursor_allowed(consumed_cursor))
        return false;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_CONSUMED_CURSOR_SIDEBAND_VERSION;
    output.consumed_cursor = consumed_cursor;
    output.header_checksum = cursor_checksum(consumed_cursor);
    *sideband = output;
    return true;
}

bool Worr_ConsumedCursorSidebandValidateV1(
    const worr_consumed_cursor_sideband_v1 *sideband)
{
    return sideband && sideband->struct_size == sizeof(*sideband) &&
           sideband->schema_version ==
               WORR_CONSUMED_CURSOR_SIDEBAND_VERSION &&
           cursor_allowed(sideband->consumed_cursor) &&
           sideband->header_checksum ==
               cursor_checksum(sideband->consumed_cursor) &&
           sideband->reserved0 == 0;
}

bool Worr_ConsumedCursorSidebandEncodeV1(
    const worr_consumed_cursor_sideband_v1 *sideband,
    worr_consumed_cursor_setting_pair_v1 *pairs,
    uint32_t pair_capacity)
{
    worr_consumed_cursor_setting_pair_v1 output[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    if (!Worr_ConsumedCursorSidebandValidateV1(sideband) || !pairs ||
        pair_capacity < WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT ||
        !range_disjoint(sideband, sizeof(*sideband), pairs, sizeof(output))) {
        return false;
    }
    output[0] = (worr_consumed_cursor_setting_pair_v1){
        WORR_CONSUMED_CURSOR_SETTING_BEGIN,
        (int32_t)WORR_CONSUMED_CURSOR_SIDEBAND_VERSION};
    output[1] = (worr_consumed_cursor_setting_pair_v1){
        WORR_CONSUMED_CURSOR_SETTING_EPOCH,
        signed_bits(sideband->consumed_cursor.epoch)};
    output[2] = (worr_consumed_cursor_setting_pair_v1){
        WORR_CONSUMED_CURSOR_SETTING_SEQUENCE,
        signed_bits(sideband->consumed_cursor.contiguous_sequence)};
    output[3] = (worr_consumed_cursor_setting_pair_v1){
        WORR_CONSUMED_CURSOR_SETTING_CHECKSUM,
        signed_bits(sideband->header_checksum)};
    output[4] = (worr_consumed_cursor_setting_pair_v1){
        WORR_CONSUMED_CURSOR_SETTING_COMMIT,
        signed_bits(cursor_commit(sideband))};
    memcpy(pairs, output, sizeof(output));
    return true;
}

bool Worr_ConsumedCursorSidebandParserInitV1(
    worr_consumed_cursor_sideband_parser_v1 *parser)
{
    worr_consumed_cursor_sideband_parser_v1 output;
    if (!parser)
        return false;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_CONSUMED_CURSOR_SIDEBAND_VERSION;
    *parser = output;
    return true;
}

bool Worr_ConsumedCursorSidebandParserValidateV1(
    const worr_consumed_cursor_sideband_parser_v1 *parser)
{
    if (!parser || parser->struct_size != sizeof(*parser) ||
        parser->schema_version != WORR_CONSUMED_CURSOR_SIDEBAND_VERSION ||
        parser->phase > WORR_CONSUMED_CURSOR_PHASE_POISONED ||
        parser->packet_active > 1 || parser->reserved0 != 0) {
        return false;
    }
    if (!parser->packet_active) {
        return parser->phase == WORR_CONSUMED_CURSOR_PHASE_IDLE &&
               sideband_is_zero(&parser->pending) &&
               parser->observed_checksum == 0;
    }
    if (parser->phase == WORR_CONSUMED_CURSOR_PHASE_IDLE ||
        parser->phase == WORR_CONSUMED_CURSOR_PHASE_POISONED) {
        return sideband_is_zero(&parser->pending) &&
               parser->observed_checksum == 0;
    }
    if (parser->pending.struct_size != sizeof(parser->pending) ||
        parser->pending.schema_version !=
            WORR_CONSUMED_CURSOR_SIDEBAND_VERSION ||
        parser->pending.reserved0 != 0) {
        return false;
    }
    switch (parser->phase) {
    case WORR_CONSUMED_CURSOR_PHASE_EPOCH:
        return parser->pending.consumed_cursor.epoch == 0 &&
               parser->pending.consumed_cursor.contiguous_sequence == 0 &&
               parser->pending.header_checksum == 0 &&
               parser->observed_checksum == 0;
    case WORR_CONSUMED_CURSOR_PHASE_SEQUENCE:
        return parser->pending.consumed_cursor.contiguous_sequence == 0 &&
               parser->pending.header_checksum == 0 &&
               parser->observed_checksum == 0;
    case WORR_CONSUMED_CURSOR_PHASE_CHECKSUM:
        return cursor_allowed(parser->pending.consumed_cursor) &&
               parser->pending.header_checksum == 0 &&
               parser->observed_checksum == 0;
    case WORR_CONSUMED_CURSOR_PHASE_COMMIT:
    case WORR_CONSUMED_CURSOR_PHASE_FRAME:
        return Worr_ConsumedCursorSidebandValidateV1(&parser->pending) &&
               parser->observed_checksum ==
                   parser->pending.header_checksum;
    default:
        return false;
    }
}

worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandPacketBeginV1(
    worr_consumed_cursor_sideband_parser_v1 *parser)
{
    bool reset;
    if (!parser)
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_ConsumedCursorSidebandParserValidateV1(parser))
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    reset = parser->packet_active != 0;
    if (reset) {
        saturating_increment(&parser->telemetry.packet_boundary_failures);
        if (phase_has_dangling_header(parser->phase))
            saturating_increment(&parser->telemetry.dangling_headers);
    }
    parser_idle(parser);
    parser->packet_active = 1;
    saturating_increment(&parser->telemetry.packet_begins);
    return reset ? WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY
                 : WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_STARTED;
}

worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandPacketEndV1(
    worr_consumed_cursor_sideband_parser_v1 *parser)
{
    bool reset;
    if (!parser)
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_ConsumedCursorSidebandParserValidateV1(parser))
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    if (!parser->packet_active) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    }
    reset = parser->phase != WORR_CONSUMED_CURSOR_PHASE_IDLE;
    if (reset) {
        saturating_increment(&parser->telemetry.packet_boundary_failures);
        if (phase_has_dangling_header(parser->phase))
            saturating_increment(&parser->telemetry.dangling_headers);
    }
    parser_idle(parser);
    parser->packet_active = 0;
    saturating_increment(&parser->telemetry.packet_ends);
    return reset ? WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY
                 : WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED;
}

worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandObserveSettingV1(
    worr_consumed_cursor_sideband_parser_v1 *parser,
    int32_t index,
    int32_t value)
{
    uint32_t word;
    if (!parser)
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_ConsumedCursorSidebandParserValidateV1(parser))
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    if (!parser->packet_active) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    }
    saturating_increment(&parser->telemetry.settings_seen);
    if (!setting_index_recognized(index)) {
        saturating_increment(&parser->telemetry.non_sideband_settings);
        if (parser->phase != WORR_CONSUMED_CURSOR_PHASE_IDLE) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.intervening_failures);
            return WORR_CONSUMED_CURSOR_SIDEBAND_RESET_INTERVENING_SERVICE;
        }
        return WORR_CONSUMED_CURSOR_SIDEBAND_NOT_SIDEBAND;
    }
    if (parser->phase == WORR_CONSUMED_CURSOR_PHASE_POISONED) {
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD;
    }
    if (index == WORR_CONSUMED_CURSOR_SETTING_BEGIN) {
        if (parser->phase != WORR_CONSUMED_CURSOR_PHASE_IDLE) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.malformed_order);
            return WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD;
        }
        if (value != (int32_t)WORR_CONSUMED_CURSOR_SIDEBAND_VERSION) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.unsupported_versions);
            return WORR_CONSUMED_CURSOR_SIDEBAND_UNSUPPORTED_VERSION;
        }
        parser->pending.struct_size = sizeof(parser->pending);
        parser->pending.schema_version =
            WORR_CONSUMED_CURSOR_SIDEBAND_VERSION;
        parser->phase = WORR_CONSUMED_CURSOR_PHASE_EPOCH;
        saturating_increment(&parser->telemetry.sideband_begins);
        saturating_increment(&parser->telemetry.fields_accepted);
        return WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED;
    }
    if (index != expected_index(parser->phase)) {
        parser_poison(parser);
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD;
    }
    word = unsigned_bits(value);
    switch (parser->phase) {
    case WORR_CONSUMED_CURSOR_PHASE_EPOCH:
        parser->pending.consumed_cursor.epoch = word;
        parser->phase = WORR_CONSUMED_CURSOR_PHASE_SEQUENCE;
        break;
    case WORR_CONSUMED_CURSOR_PHASE_SEQUENCE:
        parser->pending.consumed_cursor.contiguous_sequence = word;
        if (!cursor_allowed(parser->pending.consumed_cursor)) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.malformed_order);
            return WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD;
        }
        parser->phase = WORR_CONSUMED_CURSOR_PHASE_CHECKSUM;
        break;
    case WORR_CONSUMED_CURSOR_PHASE_CHECKSUM:
        parser->observed_checksum = word;
        parser->pending.header_checksum = word;
        if (word != cursor_checksum(parser->pending.consumed_cursor)) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.checksum_failures);
            return WORR_CONSUMED_CURSOR_SIDEBAND_CHECKSUM_MISMATCH;
        }
        parser->phase = WORR_CONSUMED_CURSOR_PHASE_COMMIT;
        break;
    case WORR_CONSUMED_CURSOR_PHASE_COMMIT:
        if (word != cursor_commit(&parser->pending) ||
            !Worr_ConsumedCursorSidebandValidateV1(&parser->pending)) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.commit_failures);
            return WORR_CONSUMED_CURSOR_SIDEBAND_COMMIT_MISMATCH;
        }
        parser->phase = WORR_CONSUMED_CURSOR_PHASE_FRAME;
        saturating_increment(&parser->telemetry.fields_accepted);
        saturating_increment(&parser->telemetry.headers_committed);
        return WORR_CONSUMED_CURSOR_SIDEBAND_HEADER_COMMITTED;
    default:
        parser_poison(parser);
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD;
    }
    saturating_increment(&parser->telemetry.fields_accepted);
    return WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED;
}

worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandObserveInterveningServiceV1(
    worr_consumed_cursor_sideband_parser_v1 *parser)
{
    if (!parser)
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_ConsumedCursorSidebandParserValidateV1(parser))
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    if (!parser->packet_active) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    }
    if (parser->phase == WORR_CONSUMED_CURSOR_PHASE_IDLE)
        return WORR_CONSUMED_CURSOR_SIDEBAND_NOT_SIDEBAND;
    parser_poison(parser);
    saturating_increment(&parser->telemetry.intervening_failures);
    return WORR_CONSUMED_CURSOR_SIDEBAND_RESET_INTERVENING_SERVICE;
}

worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandConsumeFrameV1(
    worr_consumed_cursor_sideband_parser_v1 *parser,
    worr_consumed_cursor_sideband_v1 *sideband_out)
{
    worr_consumed_cursor_sideband_v1 output;
    if (!parser)
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_ConsumedCursorSidebandParserValidateV1(parser))
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    if (!sideband_out ||
        !range_disjoint(parser, sizeof(*parser),
                        sideband_out, sizeof(*sideband_out))) {
        saturating_increment(&parser->telemetry.invalid_arguments);
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT;
    }
    if (!parser->packet_active) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE;
    }
    saturating_increment(&parser->telemetry.frames_seen);
    if (parser->phase != WORR_CONSUMED_CURSOR_PHASE_FRAME) {
        saturating_increment(&parser->telemetry.missing_headers);
        /* A negotiated frame without its tuple is not recoverable later in
         * the packet.  Poison even the idle state so a caller cannot ignore
         * the missing-header result and silently resume modern framing. */
        parser_poison(parser);
        return WORR_CONSUMED_CURSOR_SIDEBAND_MISSING_HEADER;
    }
    output = parser->pending;
    parser_idle(parser);
    saturating_increment(&parser->telemetry.frames_matched);
    *sideband_out = output;
    return WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED;
}
