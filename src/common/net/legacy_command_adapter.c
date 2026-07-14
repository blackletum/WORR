/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/legacy_command_adapter.h"

#include <limits.h>
#include <string.h>

typedef struct object_range_s {
    const void *pointer;
    size_t size;
    bool mutable_object;
} object_range;

static void saturating_increment(uint64_t *counter)
{
    if (*counter != UINT64_MAX)
        ++*counter;
}

static bool ranges_disjoint(const void *a,
                            size_t a_size,
                            const void *b,
                            size_t b_size)
{
    const uintptr_t a_begin = (uintptr_t)a;
    const uintptr_t b_begin = (uintptr_t)b;
    uintptr_t a_end;
    uintptr_t b_end;

    if (a_size > UINTPTR_MAX - a_begin ||
        b_size > UINTPTR_MAX - b_begin) {
        return false;
    }
    a_end = a_begin + (uintptr_t)a_size;
    b_end = b_begin + (uintptr_t)b_size;
    return a_end <= b_begin || b_end <= a_begin;
}

static bool object_ranges_valid(const object_range *objects,
                                size_t object_count)
{
    size_t i;
    size_t j;
    for (i = 0; i < object_count; ++i) {
        for (j = i + 1; j < object_count; ++j) {
            if ((objects[i].mutable_object || objects[j].mutable_object) &&
                !ranges_disjoint(objects[i].pointer, objects[i].size,
                                 objects[j].pointer, objects[j].size)) {
                return false;
            }
        }
    }
    return true;
}

static bool array_size_u32(uint32_t count,
                           size_t element_size,
                           size_t *size_out)
{
    if (!size_out ||
        (count != 0 && element_size > SIZE_MAX / (size_t)count)) {
        return false;
    }
    *size_out = element_size * (size_t)count;
    return true;
}

static uint16_t signed_word(int16_t value)
{
    if (value >= 0)
        return (uint16_t)value;
    return (uint16_t)((int32_t)value + INT32_C(65536));
}

static int16_t wire_word(uint16_t value)
{
    if (value <= (uint16_t)INT16_MAX)
        return (int16_t)value;
    return (int16_t)((int32_t)value - INT32_C(65536));
}

static uint32_t crc32c_byte(uint32_t crc, uint8_t value)
{
    uint32_t bit;
    crc ^= value;
    for (bit = 0; bit < 8; ++bit) {
        const uint32_t mask = UINT32_C(0) - (crc & UINT32_C(1));
        crc = (crc >> 1) ^ (UINT32_C(0x82f63b78) & mask);
    }
    return crc;
}

static uint32_t crc32c_u16(uint32_t crc, uint16_t value)
{
    crc = crc32c_byte(crc, (uint8_t)(value & UINT16_C(0xff)));
    return crc32c_byte(crc, (uint8_t)(value >> 8));
}

static uint32_t sideband_checksum(worr_command_id_v1 first,
                                  uint16_t count)
{
    uint32_t crc = UINT32_MAX;
    crc = crc32c_u16(crc, UINT16_C(0x4357)); /* WC */
    crc = crc32c_u16(crc, UINT16_C(0x3153)); /* S1 */
    crc = crc32c_u16(
        crc, (uint16_t)(int32_t)WORR_LEGACY_COMMAND_SETTING_BEGIN);
    crc = crc32c_u16(crc, WORR_LEGACY_COMMAND_SIDEBAND_VERSION);
    crc = crc32c_u16(
        crc, (uint16_t)(int32_t)WORR_LEGACY_COMMAND_SETTING_EPOCH_LOW);
    crc = crc32c_u16(crc, (uint16_t)first.epoch);
    crc = crc32c_u16(
        crc, (uint16_t)(int32_t)WORR_LEGACY_COMMAND_SETTING_EPOCH_HIGH);
    crc = crc32c_u16(crc, (uint16_t)(first.epoch >> 16));
    crc = crc32c_u16(
        crc, (uint16_t)(int32_t)WORR_LEGACY_COMMAND_SETTING_SEQUENCE_LOW);
    crc = crc32c_u16(crc, (uint16_t)first.sequence);
    crc = crc32c_u16(
        crc, (uint16_t)(int32_t)WORR_LEGACY_COMMAND_SETTING_SEQUENCE_HIGH);
    crc = crc32c_u16(crc, (uint16_t)(first.sequence >> 16));
    crc = crc32c_u16(
        crc, (uint16_t)(int32_t)WORR_LEGACY_COMMAND_SETTING_COUNT);
    crc = crc32c_u16(crc, count);
    return ~crc;
}

static uint16_t sideband_commit(const worr_legacy_command_range_v1 *range)
{
    uint16_t value = UINT16_C(0xa55a);
    const uint16_t checksum_low = (uint16_t)range->header_checksum;
    const uint16_t checksum_high =
        (uint16_t)(range->header_checksum >> 16);
    value ^= (uint16_t)range->first_command_id.epoch;
    value ^= (uint16_t)(range->first_command_id.epoch >> 16);
    value ^= (uint16_t)range->first_command_id.sequence;
    value ^= (uint16_t)(range->first_command_id.sequence >> 16);
    value ^= range->command_count;
    value ^= checksum_low;
    value ^= (uint16_t)((checksum_high << 1) |
                        (checksum_high >> 15));
    value ^= WORR_LEGACY_COMMAND_SIDEBAND_VERSION;
    return value;
}

static int id_compare(worr_command_id_v1 a, worr_command_id_v1 b)
{
    if (a.epoch != b.epoch)
        return a.epoch < b.epoch ? -1 : 1;
    if (a.sequence != b.sequence)
        return a.sequence < b.sequence ? -1 : 1;
    return 0;
}

static bool id_equal(worr_command_id_v1 a, worr_command_id_v1 b)
{
    return a.epoch == b.epoch && a.sequence == b.sequence;
}

static worr_command_id_v1 cursor_id(worr_command_cursor_v1 cursor)
{
    const worr_command_id_v1 output = {
        cursor.epoch, cursor.contiguous_sequence};
    return output;
}

static bool range_last_id(worr_command_id_v1 first,
                          uint16_t count,
                          worr_command_id_v1 *last)
{
    worr_command_id_v1 current = first;
    uint16_t i;
    if (!last || count == 0 ||
        !Worr_CommandIdValidV1(first, false)) {
        return false;
    }
    for (i = 1; i < count; ++i) {
        if (!Worr_CommandIdNextV1(current, &current))
            return false;
    }
    *last = current;
    return true;
}

bool Worr_LegacyCommandRangeInitV1(
    worr_legacy_command_range_v1 *range,
    worr_command_id_v1 first_command_id,
    uint16_t command_count)
{
    worr_legacy_command_range_v1 output;
    worr_command_id_v1 last;
    if (!range ||
        command_count == 0 ||
        command_count > WORR_LEGACY_COMMAND_BATCH_MAX_COUNT ||
        !range_last_id(first_command_id, command_count, &last)) {
        return false;
    }
    (void)last;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_LEGACY_COMMAND_SIDEBAND_VERSION;
    output.first_command_id = first_command_id;
    output.command_count = command_count;
    output.header_checksum =
        sideband_checksum(first_command_id, command_count);
    *range = output;
    return true;
}

bool Worr_LegacyCommandRangeValidateV1(
    const worr_legacy_command_range_v1 *range)
{
    worr_command_id_v1 last;
    return range && range->struct_size == sizeof(*range) &&
           range->schema_version ==
               WORR_LEGACY_COMMAND_SIDEBAND_VERSION &&
           range->reserved0 == 0 && range->command_count != 0 &&
           range->command_count <=
               WORR_LEGACY_COMMAND_BATCH_MAX_COUNT &&
           range_last_id(range->first_command_id,
                         range->command_count, &last) &&
           range->header_checksum ==
               sideband_checksum(range->first_command_id,
                                 range->command_count);
}

bool Worr_LegacyCommandSidebandEncodeV1(
    const worr_legacy_command_range_v1 *range,
    worr_legacy_command_setting_pair_v1 *pairs,
    uint32_t pair_capacity)
{
    worr_legacy_command_setting_pair_v1 output[
        WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT];
    const size_t output_size = sizeof(output);
    if (!Worr_LegacyCommandRangeValidateV1(range) || !pairs ||
        pair_capacity < WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT ||
        !ranges_disjoint(range, sizeof(*range), pairs, output_size)) {
        return false;
    }

    output[0] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_BEGIN,
        (int16_t)WORR_LEGACY_COMMAND_SIDEBAND_VERSION};
    output[1] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_EPOCH_LOW,
        wire_word((uint16_t)range->first_command_id.epoch)};
    output[2] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_EPOCH_HIGH,
        wire_word((uint16_t)(range->first_command_id.epoch >> 16))};
    output[3] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_SEQUENCE_LOW,
        wire_word((uint16_t)range->first_command_id.sequence)};
    output[4] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_SEQUENCE_HIGH,
        wire_word((uint16_t)(range->first_command_id.sequence >> 16))};
    output[5] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_COUNT,
        wire_word(range->command_count)};
    output[6] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_CHECKSUM_LOW,
        wire_word((uint16_t)range->header_checksum)};
    output[7] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_CHECKSUM_HIGH,
        wire_word((uint16_t)(range->header_checksum >> 16))};
    output[8] = (worr_legacy_command_setting_pair_v1){
        WORR_LEGACY_COMMAND_SETTING_COMMIT,
        wire_word(sideband_commit(range))};
    memcpy(pairs, output, output_size);
    return true;
}

static bool range_is_zero(const worr_legacy_command_range_v1 *range)
{
    return range->struct_size == 0 && range->schema_version == 0 &&
           range->first_command_id.epoch == 0 &&
           range->first_command_id.sequence == 0 &&
           range->command_count == 0 && range->reserved0 == 0 &&
           range->header_checksum == 0;
}

static void parser_clear_pending(
    worr_legacy_command_sideband_parser_v1 *parser)
{
    memset(&parser->pending_range, 0, sizeof(parser->pending_range));
    parser->checksum_low = 0;
    parser->checksum_high = 0;
    parser->reserved0 = 0;
}

static void parser_idle(worr_legacy_command_sideband_parser_v1 *parser)
{
    parser_clear_pending(parser);
    parser->phase = WORR_LEGACY_COMMAND_PHASE_IDLE;
}

static void parser_poison(worr_legacy_command_sideband_parser_v1 *parser)
{
    parser_clear_pending(parser);
    parser->phase = WORR_LEGACY_COMMAND_PHASE_POISONED;
}

static bool phase_has_dangling_header(uint32_t phase)
{
    return phase != WORR_LEGACY_COMMAND_PHASE_IDLE &&
           phase != WORR_LEGACY_COMMAND_PHASE_POISONED;
}

bool Worr_LegacyCommandSidebandParserInitV1(
    worr_legacy_command_sideband_parser_v1 *parser)
{
    worr_legacy_command_sideband_parser_v1 output;
    if (!parser)
        return false;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_LEGACY_COMMAND_SIDEBAND_VERSION;
    *parser = output;
    return true;
}

bool Worr_LegacyCommandSidebandParserValidateV1(
    const worr_legacy_command_sideband_parser_v1 *parser)
{
    if (!parser || parser->struct_size != sizeof(*parser) ||
        parser->schema_version !=
            WORR_LEGACY_COMMAND_SIDEBAND_VERSION ||
        parser->phase > WORR_LEGACY_COMMAND_PHASE_POISONED ||
        parser->packet_active > 1 || parser->reserved0 != 0) {
        return false;
    }
    if (!parser->packet_active) {
        return parser->phase == WORR_LEGACY_COMMAND_PHASE_IDLE &&
               range_is_zero(&parser->pending_range) &&
               parser->checksum_low == 0 && parser->checksum_high == 0;
    }
    if (parser->phase == WORR_LEGACY_COMMAND_PHASE_IDLE ||
        parser->phase == WORR_LEGACY_COMMAND_PHASE_POISONED) {
        return range_is_zero(&parser->pending_range) &&
               parser->checksum_low == 0 && parser->checksum_high == 0;
    }
    if (parser->pending_range.struct_size !=
            sizeof(parser->pending_range) ||
        parser->pending_range.schema_version !=
            WORR_LEGACY_COMMAND_SIDEBAND_VERSION ||
        parser->pending_range.reserved0 != 0) {
        return false;
    }
    if (parser->phase == WORR_LEGACY_COMMAND_PHASE_COMMIT ||
        parser->phase == WORR_LEGACY_COMMAND_PHASE_MOVE) {
        if (!Worr_LegacyCommandRangeValidateV1(
                &parser->pending_range) ||
            parser->pending_range.header_checksum !=
                ((uint32_t)parser->checksum_high << 16 |
                 parser->checksum_low)) {
            return false;
        }
    }
    return true;
}

worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandPacketBeginV1(
    worr_legacy_command_sideband_parser_v1 *parser)
{
    bool reset;
    if (!parser)
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_LegacyCommandSidebandParserValidateV1(parser))
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;

    reset = parser->packet_active &&
            parser->phase != WORR_LEGACY_COMMAND_PHASE_IDLE;
    if (reset) {
        saturating_increment(&parser->telemetry.packet_boundary_resets);
        if (phase_has_dangling_header(parser->phase))
            saturating_increment(&parser->telemetry.dangling_headers);
    }
    parser_idle(parser);
    parser->packet_active = 1;
    saturating_increment(&parser->telemetry.packet_begins);
    return reset ? WORR_LEGACY_COMMAND_SIDEBAND_RESET_PACKET_BOUNDARY
                 : WORR_LEGACY_COMMAND_SIDEBAND_PACKET_STARTED;
}

worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandPacketEndV1(
    worr_legacy_command_sideband_parser_v1 *parser)
{
    bool reset;
    if (!parser)
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_LegacyCommandSidebandParserValidateV1(parser))
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;
    if (!parser->packet_active) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;
    }

    reset = parser->phase != WORR_LEGACY_COMMAND_PHASE_IDLE;
    if (reset) {
        saturating_increment(&parser->telemetry.packet_boundary_resets);
        if (phase_has_dangling_header(parser->phase))
            saturating_increment(&parser->telemetry.dangling_headers);
    }
    parser_idle(parser);
    parser->packet_active = 0;
    saturating_increment(&parser->telemetry.packet_ends);
    return reset ? WORR_LEGACY_COMMAND_SIDEBAND_RESET_PACKET_BOUNDARY
                 : WORR_LEGACY_COMMAND_SIDEBAND_PACKET_ENDED;
}

static bool sideband_index_recognized(int16_t index)
{
    return index >= WORR_LEGACY_COMMAND_SETTING_BEGIN &&
           index <= WORR_LEGACY_COMMAND_SETTING_COMMIT;
}

static int16_t expected_index(uint32_t phase)
{
    switch (phase) {
    case WORR_LEGACY_COMMAND_PHASE_EPOCH_LOW:
        return WORR_LEGACY_COMMAND_SETTING_EPOCH_LOW;
    case WORR_LEGACY_COMMAND_PHASE_EPOCH_HIGH:
        return WORR_LEGACY_COMMAND_SETTING_EPOCH_HIGH;
    case WORR_LEGACY_COMMAND_PHASE_SEQUENCE_LOW:
        return WORR_LEGACY_COMMAND_SETTING_SEQUENCE_LOW;
    case WORR_LEGACY_COMMAND_PHASE_SEQUENCE_HIGH:
        return WORR_LEGACY_COMMAND_SETTING_SEQUENCE_HIGH;
    case WORR_LEGACY_COMMAND_PHASE_COUNT:
        return WORR_LEGACY_COMMAND_SETTING_COUNT;
    case WORR_LEGACY_COMMAND_PHASE_CHECKSUM_LOW:
        return WORR_LEGACY_COMMAND_SETTING_CHECKSUM_LOW;
    case WORR_LEGACY_COMMAND_PHASE_CHECKSUM_HIGH:
        return WORR_LEGACY_COMMAND_SETTING_CHECKSUM_HIGH;
    case WORR_LEGACY_COMMAND_PHASE_COMMIT:
        return WORR_LEGACY_COMMAND_SETTING_COMMIT;
    default:
        return 0;
    }
}

worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandObserveSettingV1(
    worr_legacy_command_sideband_parser_v1 *parser,
    int16_t index,
    int16_t value)
{
    uint16_t word;
    uint32_t checksum;
    if (!parser)
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_LegacyCommandSidebandParserValidateV1(parser))
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;
    if (!parser->packet_active) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;
    }
    saturating_increment(&parser->telemetry.settings_seen);

    if (!sideband_index_recognized(index)) {
        saturating_increment(&parser->telemetry.non_sideband_settings);
        if (parser->phase != WORR_LEGACY_COMMAND_PHASE_IDLE) {
            parser_idle(parser);
            saturating_increment(&parser->telemetry.intervening_resets);
            return WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE;
        }
        return WORR_LEGACY_COMMAND_SIDEBAND_NOT_SIDEBAND;
    }

    if (parser->phase == WORR_LEGACY_COMMAND_PHASE_POISONED) {
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD;
    }

    if (index == WORR_LEGACY_COMMAND_SETTING_BEGIN) {
        if (parser->phase != WORR_LEGACY_COMMAND_PHASE_IDLE) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.malformed_order);
            return WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD;
        }
        if (value != (int16_t)WORR_LEGACY_COMMAND_SIDEBAND_VERSION) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.unsupported_versions);
            return WORR_LEGACY_COMMAND_SIDEBAND_UNSUPPORTED_VERSION;
        }
        parser->pending_range.struct_size = sizeof(parser->pending_range);
        parser->pending_range.schema_version =
            WORR_LEGACY_COMMAND_SIDEBAND_VERSION;
        parser->phase = WORR_LEGACY_COMMAND_PHASE_EPOCH_LOW;
        saturating_increment(&parser->telemetry.sideband_begins);
        saturating_increment(&parser->telemetry.fields_accepted);
        return WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED;
    }

    if (index != expected_index(parser->phase)) {
        parser_poison(parser);
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD;
    }

    word = signed_word(value);
    switch (parser->phase) {
    case WORR_LEGACY_COMMAND_PHASE_EPOCH_LOW:
        parser->pending_range.first_command_id.epoch = word;
        parser->phase = WORR_LEGACY_COMMAND_PHASE_EPOCH_HIGH;
        break;
    case WORR_LEGACY_COMMAND_PHASE_EPOCH_HIGH:
        parser->pending_range.first_command_id.epoch |=
            (uint32_t)word << 16;
        if (parser->pending_range.first_command_id.epoch == 0) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.malformed_order);
            return WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD;
        }
        parser->phase = WORR_LEGACY_COMMAND_PHASE_SEQUENCE_LOW;
        break;
    case WORR_LEGACY_COMMAND_PHASE_SEQUENCE_LOW:
        parser->pending_range.first_command_id.sequence = word;
        parser->phase = WORR_LEGACY_COMMAND_PHASE_SEQUENCE_HIGH;
        break;
    case WORR_LEGACY_COMMAND_PHASE_SEQUENCE_HIGH:
        parser->pending_range.first_command_id.sequence |=
            (uint32_t)word << 16;
        if (parser->pending_range.first_command_id.sequence == 0) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.malformed_order);
            return WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD;
        }
        parser->phase = WORR_LEGACY_COMMAND_PHASE_COUNT;
        break;
    case WORR_LEGACY_COMMAND_PHASE_COUNT:
        if (word == 0 ||
            word > WORR_LEGACY_COMMAND_BATCH_MAX_COUNT) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.malformed_order);
            return WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD;
        }
        parser->pending_range.command_count = word;
        parser->phase = WORR_LEGACY_COMMAND_PHASE_CHECKSUM_LOW;
        break;
    case WORR_LEGACY_COMMAND_PHASE_CHECKSUM_LOW:
        parser->checksum_low = word;
        parser->phase = WORR_LEGACY_COMMAND_PHASE_CHECKSUM_HIGH;
        break;
    case WORR_LEGACY_COMMAND_PHASE_CHECKSUM_HIGH:
        parser->checksum_high = word;
        checksum = (uint32_t)parser->checksum_high << 16 |
                   parser->checksum_low;
        parser->pending_range.header_checksum = checksum;
        if (checksum != sideband_checksum(
                            parser->pending_range.first_command_id,
                            parser->pending_range.command_count)) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.checksum_failures);
            return WORR_LEGACY_COMMAND_SIDEBAND_CHECKSUM_MISMATCH;
        }
        parser->phase = WORR_LEGACY_COMMAND_PHASE_COMMIT;
        break;
    case WORR_LEGACY_COMMAND_PHASE_COMMIT:
        if (word != sideband_commit(&parser->pending_range) ||
            !Worr_LegacyCommandRangeValidateV1(
                &parser->pending_range)) {
            parser_poison(parser);
            saturating_increment(&parser->telemetry.commit_failures);
            return WORR_LEGACY_COMMAND_SIDEBAND_COMMIT_MISMATCH;
        }
        parser->phase = WORR_LEGACY_COMMAND_PHASE_MOVE;
        saturating_increment(&parser->telemetry.fields_accepted);
        saturating_increment(&parser->telemetry.headers_committed);
        return WORR_LEGACY_COMMAND_SIDEBAND_HEADER_COMMITTED;
    default:
        parser_poison(parser);
        saturating_increment(&parser->telemetry.malformed_order);
        return WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD;
    }

    saturating_increment(&parser->telemetry.fields_accepted);
    return WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED;
}

worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandObserveInterveningServiceV1(
    worr_legacy_command_sideband_parser_v1 *parser)
{
    if (!parser)
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_LegacyCommandSidebandParserValidateV1(parser))
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;
    if (!parser->packet_active) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;
    }
    if (parser->phase == WORR_LEGACY_COMMAND_PHASE_IDLE)
        return WORR_LEGACY_COMMAND_SIDEBAND_NOT_SIDEBAND;
    parser_idle(parser);
    saturating_increment(&parser->telemetry.intervening_resets);
    return WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE;
}

worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandConsumeMoveV1(
    worr_legacy_command_sideband_parser_v1 *parser,
    uint32_t carrier,
    uint32_t decoded_command_count,
    worr_legacy_command_range_v1 *range_out)
{
    worr_legacy_command_range_v1 output;
    bool carrier_count_valid;
    if (!parser)
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_LegacyCommandSidebandParserValidateV1(parser))
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;
    if (!range_out ||
        !ranges_disjoint(parser, sizeof(*parser),
                         range_out, sizeof(*range_out))) {
        saturating_increment(&parser->telemetry.invalid_arguments);
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_ARGUMENT;
    }
    if (!parser->packet_active) {
        saturating_increment(&parser->telemetry.invalid_state);
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE;
    }
    saturating_increment(&parser->telemetry.moves_seen);

    if (carrier != WORR_LEGACY_COMMAND_CARRIER_MOVE &&
        carrier != WORR_LEGACY_COMMAND_CARRIER_BATCH_MOVE) {
        parser_idle(parser);
        saturating_increment(&parser->telemetry.invalid_carriers);
        return WORR_LEGACY_COMMAND_SIDEBAND_INVALID_CARRIER;
    }
    carrier_count_valid =
        (carrier == WORR_LEGACY_COMMAND_CARRIER_MOVE &&
         decoded_command_count == WORR_LEGACY_COMMAND_MOVE_COUNT) ||
        (carrier == WORR_LEGACY_COMMAND_CARRIER_BATCH_MOVE &&
         decoded_command_count != 0 &&
         decoded_command_count <=
             WORR_LEGACY_COMMAND_BATCH_MAX_COUNT);
    if (!carrier_count_valid) {
        parser_idle(parser);
        saturating_increment(&parser->telemetry.count_mismatches);
        return WORR_LEGACY_COMMAND_SIDEBAND_COUNT_MISMATCH;
    }
    if (parser->phase != WORR_LEGACY_COMMAND_PHASE_MOVE) {
        parser_idle(parser);
        saturating_increment(&parser->telemetry.missing_headers);
        return WORR_LEGACY_COMMAND_SIDEBAND_MISSING_HEADER;
    }
    if (decoded_command_count !=
        parser->pending_range.command_count) {
        parser_idle(parser);
        saturating_increment(&parser->telemetry.count_mismatches);
        return WORR_LEGACY_COMMAND_SIDEBAND_COUNT_MISMATCH;
    }

    output = parser->pending_range;
    parser_idle(parser);
    saturating_increment(&parser->telemetry.moves_matched);
    *range_out = output;
    return WORR_LEGACY_COMMAND_SIDEBAND_MOVE_MATCHED;
}

static worr_legacy_command_adapter_result_v1 adapter_finish(
    worr_legacy_command_adapter_report_v1 *report_out,
    worr_legacy_command_adapter_report_v1 *report,
    const worr_command_stream_v1 *stream,
    worr_legacy_command_adapter_result_v1 result,
    uint32_t stream_result)
{
    report->result = (uint32_t)result;
    report->stream_result = stream_result;
    report->received_cursor = stream->received_cursor;
    report->last_received_sample_time_us =
        stream->last_received_sample_time_us;
    *report_out = *report;
    return result;
}

static worr_legacy_command_adapter_result_v1 map_stream_result(
    worr_command_stream_result_v1 result)
{
    switch (result) {
    case WORR_COMMAND_STREAM_CAPACITY:
        return WORR_LEGACY_COMMAND_ADAPTER_CAPACITY;
    case WORR_COMMAND_STREAM_CONFLICT:
        return WORR_LEGACY_COMMAND_ADAPTER_CONFLICT;
    case WORR_COMMAND_STREAM_FUTURE_GAP:
        return WORR_LEGACY_COMMAND_ADAPTER_FUTURE_GAP;
    case WORR_COMMAND_STREAM_WRONG_EPOCH:
        return WORR_LEGACY_COMMAND_ADAPTER_WRONG_EPOCH;
    case WORR_COMMAND_STREAM_SAMPLE_TIME_MISMATCH:
    case WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW:
        return WORR_LEGACY_COMMAND_ADAPTER_SAMPLE_TIME_OVERFLOW;
    case WORR_COMMAND_STREAM_EPOCH_EXHAUSTED:
        return WORR_LEGACY_COMMAND_ADAPTER_EPOCH_EXHAUSTED;
    case WORR_COMMAND_STREAM_INVALID_RECORD:
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_COMMAND;
    case WORR_COMMAND_STREAM_INVALID_ARGUMENT:
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_ARGUMENT;
    case WORR_COMMAND_STREAM_INVALID_STATE:
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_STREAM;
    default:
        return WORR_LEGACY_COMMAND_ADAPTER_STREAM_REJECTED;
    }
}

worr_legacy_command_adapter_result_v1 Worr_LegacyCommandAdapterApplyV1(
    worr_command_stream_v1 *stream,
    const worr_legacy_command_range_v1 *range,
    const worr_prediction_command_v1 *commands,
    uint32_t command_count,
    uint32_t movement_model_revision,
    const worr_command_render_watermark_v1 *packet_watermark,
    worr_command_record_v1 *record_scratch,
    uint32_t record_scratch_capacity,
    worr_command_stream_slot_v1 *stream_scratch,
    uint32_t stream_scratch_capacity,
    worr_legacy_command_adapter_report_v1 *report_out)
{
    worr_legacy_command_adapter_report_v1 report;
    worr_command_id_v1 current_id;
    worr_command_id_v1 last_id;
    worr_command_id_v1 expected_new = {0, 0};
    uint64_t next_sample;
    uint32_t new_count = 0;
    uint32_t i;
    size_t command_size;
    size_t record_scratch_size;
    size_t stream_scratch_size;
    size_t live_size;
    object_range objects[8];
    worr_command_stream_result_v1 stream_result;

    if (!stream || !range || !commands || !packet_watermark ||
        !record_scratch || !stream_scratch || !report_out ||
        command_count == 0) {
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_ARGUMENT;
    }
    if (!Worr_CommandStreamValidateV1(stream))
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_STREAM;
    if (!Worr_LegacyCommandRangeValidateV1(range))
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_RANGE;
    if (command_count != range->command_count)
        return WORR_LEGACY_COMMAND_ADAPTER_COUNT_MISMATCH;
    if (!Worr_CommandRenderWatermarkValidateV1(packet_watermark) ||
        packet_watermark->provenance !=
            WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED) {
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_WATERMARK;
    }
    if (record_scratch_capacity < command_count ||
        stream_scratch_capacity < stream->capacity) {
        return WORR_LEGACY_COMMAND_ADAPTER_INSUFFICIENT_SCRATCH;
    }
    if (!array_size_u32(command_count, sizeof(*commands),
                        &command_size) ||
        !array_size_u32(record_scratch_capacity,
                        sizeof(*record_scratch),
                        &record_scratch_size) ||
        !array_size_u32(stream_scratch_capacity,
                        sizeof(*stream_scratch),
                        &stream_scratch_size) ||
        !array_size_u32(stream->capacity, sizeof(*stream->slots),
                        &live_size)) {
        return WORR_LEGACY_COMMAND_ADAPTER_INVALID_ARGUMENT;
    }
    objects[0] = (object_range){stream, sizeof(*stream), true};
    objects[1] = (object_range){stream->slots, live_size, true};
    objects[2] = (object_range){range, sizeof(*range), false};
    objects[3] = (object_range){commands, command_size, false};
    objects[4] =
        (object_range){packet_watermark, sizeof(*packet_watermark), false};
    objects[5] =
        (object_range){record_scratch, record_scratch_size, true};
    objects[6] =
        (object_range){stream_scratch, stream_scratch_size, true};
    objects[7] = (object_range){report_out, sizeof(*report_out), true};
    if (!object_ranges_valid(objects, sizeof(objects) / sizeof(objects[0])))
        return WORR_LEGACY_COMMAND_ADAPTER_ALIAS_VIOLATION;

    memset(&report, 0, sizeof(report));
    report.struct_size = sizeof(report);
    report.schema_version = WORR_LEGACY_COMMAND_SIDEBAND_VERSION;
    report.stream_result = WORR_LEGACY_COMMAND_NO_STREAM_RESULT;
    report.input_count = command_count;
    report.first_command_id = range->first_command_id;
    if (!range_last_id(range->first_command_id,
                       range->command_count, &last_id)) {
        return adapter_finish(
            report_out, &report, stream,
            WORR_LEGACY_COMMAND_ADAPTER_INVALID_RANGE,
            WORR_LEGACY_COMMAND_NO_STREAM_RESULT);
    }
    report.last_command_id = last_id;

    current_id = range->first_command_id;
    next_sample = stream->last_received_sample_time_us;
    for (i = 0; i < command_count; ++i) {
        worr_command_record_v1 candidate;
        worr_command_record_v1 retained;
        const int received_order =
            id_compare(current_id, cursor_id(stream->received_cursor));

        memset(&candidate, 0, sizeof(candidate));
        candidate.struct_size = sizeof(candidate);
        candidate.schema_version = WORR_COMMAND_ABI_VERSION;
        candidate.command_id = current_id;
        candidate.movement_model_revision = movement_model_revision;
        candidate.command = commands[i];
        candidate.render_watermark = *packet_watermark;
        if (!Worr_CommandRecordCanonicalizeV1(
                &candidate, stream->max_duration_ms)) {
            return adapter_finish(
                report_out, &report, stream,
                WORR_LEGACY_COMMAND_ADAPTER_INVALID_COMMAND,
                WORR_LEGACY_COMMAND_NO_STREAM_RESULT);
        }

        if (received_order <= 0) {
            if (Worr_CommandStreamCopyRecordV1(
                    stream, current_id, &retained)) {
                /*
                 * A server-synthesized record is the authoritative, final
                 * disposition for a command that never arrived before its
                 * execution deadline.  A later legacy backup carrying the
                 * real command is therefore stale, not a semantic conflict:
                 * the server must neither execute it nor disconnect the
                 * client because it differs from the substitute.
                 */
                if (retained.render_watermark.provenance ==
                    WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED) {
                    ++report.stale_count;
                    goto next_command;
                }
                candidate.sample_time_us = retained.sample_time_us;
                if (!Worr_CommandRecordSemanticallyEqualV1(
                        &candidate, &retained,
                        stream->max_duration_ms)) {
                    return adapter_finish(
                        report_out, &report, stream,
                        WORR_LEGACY_COMMAND_ADAPTER_CONFLICT,
                        WORR_COMMAND_STREAM_CONFLICT);
                }
                ++report.duplicate_count;
            } else {
                ++report.stale_count;
            }
        } else {
            uint64_t increment;
            if (expected_new.epoch == 0 &&
                !Worr_CommandCursorNextIdV1(
                    stream->received_cursor, &expected_new)) {
                return adapter_finish(
                    report_out, &report, stream,
                    WORR_LEGACY_COMMAND_ADAPTER_EPOCH_EXHAUSTED,
                    WORR_COMMAND_STREAM_EPOCH_EXHAUSTED);
            }
            if (!id_equal(current_id, expected_new)) {
                const worr_legacy_command_adapter_result_v1 gap_result =
                    current_id.epoch == expected_new.epoch
                        ? WORR_LEGACY_COMMAND_ADAPTER_FUTURE_GAP
                        : WORR_LEGACY_COMMAND_ADAPTER_WRONG_EPOCH;
                const uint32_t gap_stream_result =
                    current_id.epoch == expected_new.epoch
                        ? WORR_COMMAND_STREAM_FUTURE_GAP
                        : WORR_COMMAND_STREAM_WRONG_EPOCH;
                return adapter_finish(report_out, &report, stream,
                                      gap_result, gap_stream_result);
            }
            increment =
                (uint64_t)candidate.command.duration_ms * UINT64_C(1000);
            if (next_sample > UINT64_MAX - increment) {
                return adapter_finish(
                    report_out, &report, stream,
                    WORR_LEGACY_COMMAND_ADAPTER_SAMPLE_TIME_OVERFLOW,
                    WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW);
            }
            next_sample += increment;
            candidate.sample_time_us = next_sample;
            record_scratch[new_count++] = candidate;
            if (i + 1u < command_count &&
                !Worr_CommandIdNextV1(expected_new, &expected_new)) {
                return adapter_finish(
                    report_out, &report, stream,
                    WORR_LEGACY_COMMAND_ADAPTER_EPOCH_EXHAUSTED,
                    WORR_COMMAND_STREAM_EPOCH_EXHAUSTED);
            }
        }

next_command:
        if (i + 1u < command_count &&
            !Worr_CommandIdNextV1(current_id, &current_id)) {
            return adapter_finish(
                report_out, &report, stream,
                WORR_LEGACY_COMMAND_ADAPTER_EPOCH_EXHAUSTED,
                WORR_COMMAND_STREAM_EPOCH_EXHAUSTED);
        }
    }

    if (new_count == 0) {
        stream_result = report.duplicate_count != 0
                            ? WORR_COMMAND_STREAM_DUPLICATE
                            : WORR_COMMAND_STREAM_STALE;
        return adapter_finish(
            report_out, &report, stream,
            WORR_LEGACY_COMMAND_ADAPTER_IDEMPOTENT,
            (uint32_t)stream_result);
    }

    stream_result = Worr_CommandStreamInsertBatchV1(
        stream, record_scratch, new_count,
        stream_scratch, stream_scratch_capacity);
    if (stream_result != WORR_COMMAND_STREAM_INSERTED) {
        return adapter_finish(report_out, &report, stream,
                              map_stream_result(stream_result),
                              (uint32_t)stream_result);
    }
    report.inserted_count = new_count;
    return adapter_finish(report_out, &report, stream,
                          WORR_LEGACY_COMMAND_ADAPTER_APPLIED,
                          (uint32_t)stream_result);
}
