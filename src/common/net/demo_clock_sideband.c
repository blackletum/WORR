/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/demo_clock_sideband.h"

#include <string.h>

#define WORR_DEMO_CLOCK_FNV_OFFSET UINT32_C(2166136261)
#define WORR_DEMO_CLOCK_FNV_PRIME UINT32_C(16777619)
#define WORR_DEMO_CLOCK_CHECKSUM_DOMAIN UINT32_C(0x44434c31)
#define WORR_DEMO_CLOCK_COMMIT_DOMAIN UINT32_C(0x44434331)

static uint32_t append_u32(uint32_t hash, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) {
        hash ^= value & UINT32_C(0xff);
        hash *= WORR_DEMO_CLOCK_FNV_PRIME;
        value >>= 8;
    }
    return hash;
}

static uint32_t rotate_left(uint32_t value, unsigned shift)
{
    return (value << shift) | (value >> (32u - shift));
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

static uint32_t anchor_checksum(int32_t server_frame,
                                uint64_t server_time_us)
{
    uint32_t hash = WORR_DEMO_CLOCK_FNV_OFFSET;
    hash = append_u32(hash, WORR_DEMO_CLOCK_CHECKSUM_DOMAIN);
    hash = append_u32(hash, WORR_DEMO_CLOCK_SIDEBAND_VERSION);
    hash = append_u32(hash, (uint32_t)server_frame);
    hash = append_u32(hash, (uint32_t)server_time_us);
    return append_u32(hash, (uint32_t)(server_time_us >> 32));
}

static uint32_t anchor_commit(const worr_demo_clock_anchor_v1 *anchor)
{
    const uint32_t time_low = (uint32_t)anchor->server_time_us;
    const uint32_t time_high = (uint32_t)(anchor->server_time_us >> 32);
    return WORR_DEMO_CLOCK_COMMIT_DOMAIN ^
           rotate_left(anchor->checksum, 7) ^
           rotate_left((uint32_t)anchor->server_frame, 13) ^
           rotate_left(time_low, 19) ^ rotate_left(time_high, 3);
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

static bool anchor_zero(const worr_demo_clock_anchor_v1 *anchor)
{
    return anchor->struct_size == 0 && anchor->schema_version == 0 &&
           anchor->server_frame == 0 && anchor->reserved0 == 0 &&
           anchor->server_time_us == 0 && anchor->checksum == 0 &&
           anchor->reserved1 == 0;
}

static void parser_idle(worr_demo_clock_sideband_parser_v1 *parser)
{
    memset(&parser->pending, 0, sizeof(parser->pending));
    parser->phase = WORR_DEMO_CLOCK_PHASE_IDLE;
}

static void parser_poison(worr_demo_clock_sideband_parser_v1 *parser)
{
    memset(&parser->pending, 0, sizeof(parser->pending));
    parser->phase = WORR_DEMO_CLOCK_PHASE_POISONED;
}

static int32_t expected_index(uint32_t phase)
{
    switch (phase) {
    case WORR_DEMO_CLOCK_PHASE_FRAME:
        return WORR_DEMO_CLOCK_SETTING_FRAME;
    case WORR_DEMO_CLOCK_PHASE_TIME_LOW:
        return WORR_DEMO_CLOCK_SETTING_TIME_LOW;
    case WORR_DEMO_CLOCK_PHASE_TIME_HIGH:
        return WORR_DEMO_CLOCK_SETTING_TIME_HIGH;
    case WORR_DEMO_CLOCK_PHASE_CHECKSUM:
        return WORR_DEMO_CLOCK_SETTING_CHECKSUM;
    case WORR_DEMO_CLOCK_PHASE_COMMIT:
        return WORR_DEMO_CLOCK_SETTING_COMMIT;
    default:
        return 0;
    }
}

bool Worr_DemoClockSettingRecognizedV1(int32_t index)
{
    return index >= WORR_DEMO_CLOCK_SETTING_BEGIN &&
           index <= WORR_DEMO_CLOCK_SETTING_COMMIT;
}

bool Worr_DemoClockAnchorInitV1(worr_demo_clock_anchor_v1 *anchor,
                                int32_t server_frame,
                                uint64_t server_time_us)
{
    worr_demo_clock_anchor_v1 output;
    if (!anchor || server_frame < 0)
        return false;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_DEMO_CLOCK_SIDEBAND_VERSION;
    output.server_frame = server_frame;
    output.server_time_us = server_time_us;
    output.checksum = anchor_checksum(server_frame, server_time_us);
    *anchor = output;
    return true;
}

bool Worr_DemoClockAnchorValidateV1(
    const worr_demo_clock_anchor_v1 *anchor)
{
    return anchor && anchor->struct_size == sizeof(*anchor) &&
           anchor->schema_version == WORR_DEMO_CLOCK_SIDEBAND_VERSION &&
           anchor->server_frame >= 0 && anchor->reserved0 == 0 &&
           anchor->checksum ==
               anchor_checksum(anchor->server_frame,
                               anchor->server_time_us) &&
           anchor->reserved1 == 0;
}

bool Worr_DemoClockAnchorEncodeV1(
    const worr_demo_clock_anchor_v1 *anchor,
    worr_demo_clock_setting_pair_v1 *pairs,
    uint32_t pair_capacity)
{
    worr_demo_clock_setting_pair_v1 output[
        WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT];
    if (!Worr_DemoClockAnchorValidateV1(anchor) || !pairs ||
        pair_capacity < WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT ||
        !range_disjoint(anchor, sizeof(*anchor), pairs, sizeof(output))) {
        return false;
    }
    output[0] = (worr_demo_clock_setting_pair_v1){
        WORR_DEMO_CLOCK_SETTING_BEGIN,
        (int32_t)WORR_DEMO_CLOCK_SIDEBAND_VERSION};
    output[1] = (worr_demo_clock_setting_pair_v1){
        WORR_DEMO_CLOCK_SETTING_FRAME, anchor->server_frame};
    output[2] = (worr_demo_clock_setting_pair_v1){
        WORR_DEMO_CLOCK_SETTING_TIME_LOW,
        signed_bits((uint32_t)anchor->server_time_us)};
    output[3] = (worr_demo_clock_setting_pair_v1){
        WORR_DEMO_CLOCK_SETTING_TIME_HIGH,
        signed_bits((uint32_t)(anchor->server_time_us >> 32))};
    output[4] = (worr_demo_clock_setting_pair_v1){
        WORR_DEMO_CLOCK_SETTING_CHECKSUM, signed_bits(anchor->checksum)};
    output[5] = (worr_demo_clock_setting_pair_v1){
        WORR_DEMO_CLOCK_SETTING_COMMIT,
        signed_bits(anchor_commit(anchor))};
    memcpy(pairs, output, sizeof(output));
    return true;
}

bool Worr_DemoClockSidebandParserInitV1(
    worr_demo_clock_sideband_parser_v1 *parser)
{
    worr_demo_clock_sideband_parser_v1 output;
    if (!parser)
        return false;
    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_DEMO_CLOCK_SIDEBAND_VERSION;
    *parser = output;
    return true;
}

bool Worr_DemoClockSidebandParserValidateV1(
    const worr_demo_clock_sideband_parser_v1 *parser)
{
    if (!parser || parser->struct_size != sizeof(*parser) ||
        parser->schema_version != WORR_DEMO_CLOCK_SIDEBAND_VERSION ||
        parser->phase > WORR_DEMO_CLOCK_PHASE_POISONED ||
        parser->packet_active > 1) {
        return false;
    }
    if (!parser->packet_active) {
        return parser->phase == WORR_DEMO_CLOCK_PHASE_IDLE &&
               anchor_zero(&parser->pending);
    }
    if (parser->phase == WORR_DEMO_CLOCK_PHASE_IDLE ||
        parser->phase == WORR_DEMO_CLOCK_PHASE_POISONED) {
        return anchor_zero(&parser->pending);
    }
    if (parser->pending.struct_size != sizeof(parser->pending) ||
        parser->pending.schema_version !=
            WORR_DEMO_CLOCK_SIDEBAND_VERSION ||
        parser->pending.reserved0 != 0 ||
        parser->pending.reserved1 != 0) {
        return false;
    }
    switch (parser->phase) {
    case WORR_DEMO_CLOCK_PHASE_FRAME:
        return parser->pending.server_frame == 0 &&
               parser->pending.server_time_us == 0 &&
               parser->pending.checksum == 0;
    case WORR_DEMO_CLOCK_PHASE_TIME_LOW:
    case WORR_DEMO_CLOCK_PHASE_TIME_HIGH:
    case WORR_DEMO_CLOCK_PHASE_CHECKSUM:
        return parser->pending.server_frame >= 0 &&
               parser->pending.checksum == 0;
    case WORR_DEMO_CLOCK_PHASE_COMMIT:
    case WORR_DEMO_CLOCK_PHASE_READY:
        return Worr_DemoClockAnchorValidateV1(&parser->pending);
    default:
        return false;
    }
}

worr_demo_clock_sideband_result_v1 Worr_DemoClockSidebandPacketBeginV1(
    worr_demo_clock_sideband_parser_v1 *parser)
{
    bool overlap;
    if (!parser)
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_DemoClockSidebandParserValidateV1(parser))
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_STATE;
    overlap = parser->packet_active != 0;
    parser_idle(parser);
    parser->packet_active = 1;
    return overlap ? WORR_DEMO_CLOCK_SIDEBAND_PACKET_BOUNDARY
                   : WORR_DEMO_CLOCK_SIDEBAND_PACKET_STARTED;
}

worr_demo_clock_sideband_result_v1 Worr_DemoClockSidebandPacketEndV1(
    worr_demo_clock_sideband_parser_v1 *parser)
{
    bool dangling;
    if (!parser)
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_DemoClockSidebandParserValidateV1(parser))
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_STATE;
    if (!parser->packet_active)
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_STATE;
    dangling = parser->phase != WORR_DEMO_CLOCK_PHASE_IDLE;
    parser_idle(parser);
    parser->packet_active = 0;
    return dangling ? WORR_DEMO_CLOCK_SIDEBAND_PACKET_BOUNDARY
                    : WORR_DEMO_CLOCK_SIDEBAND_PACKET_ENDED;
}

worr_demo_clock_sideband_result_v1 Worr_DemoClockSidebandObserveSettingV1(
    worr_demo_clock_sideband_parser_v1 *parser,
    int32_t index,
    int32_t value)
{
    uint32_t word;
    if (!parser)
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_DemoClockSidebandParserValidateV1(parser) ||
        !parser->packet_active) {
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_STATE;
    }
    if (!Worr_DemoClockSettingRecognizedV1(index)) {
        if (parser->phase == WORR_DEMO_CLOCK_PHASE_IDLE)
            return WORR_DEMO_CLOCK_SIDEBAND_NOT_SIDEBAND;
        parser_poison(parser);
        return WORR_DEMO_CLOCK_SIDEBAND_INTERVENING_SERVICE;
    }
    if (parser->phase == WORR_DEMO_CLOCK_PHASE_POISONED) {
        return WORR_DEMO_CLOCK_SIDEBAND_UNEXPECTED_FIELD;
    }
    if (index == WORR_DEMO_CLOCK_SETTING_BEGIN) {
        if (parser->phase != WORR_DEMO_CLOCK_PHASE_IDLE) {
            parser_poison(parser);
            return WORR_DEMO_CLOCK_SIDEBAND_UNEXPECTED_FIELD;
        }
        if (value != (int32_t)WORR_DEMO_CLOCK_SIDEBAND_VERSION) {
            parser_poison(parser);
            return WORR_DEMO_CLOCK_SIDEBAND_UNSUPPORTED_VERSION;
        }
        parser->pending.struct_size = sizeof(parser->pending);
        parser->pending.schema_version =
            WORR_DEMO_CLOCK_SIDEBAND_VERSION;
        parser->phase = WORR_DEMO_CLOCK_PHASE_FRAME;
        return WORR_DEMO_CLOCK_SIDEBAND_FIELD_ACCEPTED;
    }
    if (index != expected_index(parser->phase)) {
        parser_poison(parser);
        return WORR_DEMO_CLOCK_SIDEBAND_UNEXPECTED_FIELD;
    }

    word = unsigned_bits(value);
    switch (parser->phase) {
    case WORR_DEMO_CLOCK_PHASE_FRAME:
        if (value < 0) {
            parser_poison(parser);
            return WORR_DEMO_CLOCK_SIDEBAND_UNEXPECTED_FIELD;
        }
        parser->pending.server_frame = value;
        parser->phase = WORR_DEMO_CLOCK_PHASE_TIME_LOW;
        break;
    case WORR_DEMO_CLOCK_PHASE_TIME_LOW:
        parser->pending.server_time_us = word;
        parser->phase = WORR_DEMO_CLOCK_PHASE_TIME_HIGH;
        break;
    case WORR_DEMO_CLOCK_PHASE_TIME_HIGH:
        parser->pending.server_time_us |= (uint64_t)word << 32;
        parser->phase = WORR_DEMO_CLOCK_PHASE_CHECKSUM;
        break;
    case WORR_DEMO_CLOCK_PHASE_CHECKSUM:
        parser->pending.checksum = word;
        if (word != anchor_checksum(parser->pending.server_frame,
                                    parser->pending.server_time_us)) {
            parser_poison(parser);
            return WORR_DEMO_CLOCK_SIDEBAND_CHECKSUM_MISMATCH;
        }
        parser->phase = WORR_DEMO_CLOCK_PHASE_COMMIT;
        break;
    case WORR_DEMO_CLOCK_PHASE_COMMIT:
        if (word != anchor_commit(&parser->pending) ||
            !Worr_DemoClockAnchorValidateV1(&parser->pending)) {
            parser_poison(parser);
            return WORR_DEMO_CLOCK_SIDEBAND_COMMIT_MISMATCH;
        }
        parser->phase = WORR_DEMO_CLOCK_PHASE_READY;
        return WORR_DEMO_CLOCK_SIDEBAND_ANCHOR_COMMITTED;
    default:
        parser_poison(parser);
        return WORR_DEMO_CLOCK_SIDEBAND_UNEXPECTED_FIELD;
    }
    return WORR_DEMO_CLOCK_SIDEBAND_FIELD_ACCEPTED;
}

worr_demo_clock_sideband_result_v1
Worr_DemoClockSidebandObserveInterveningServiceV1(
    worr_demo_clock_sideband_parser_v1 *parser)
{
    if (!parser)
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_ARGUMENT;
    if (!Worr_DemoClockSidebandParserValidateV1(parser) ||
        !parser->packet_active) {
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_STATE;
    }
    if (parser->phase == WORR_DEMO_CLOCK_PHASE_IDLE)
        return WORR_DEMO_CLOCK_SIDEBAND_NOT_SIDEBAND;
    parser_poison(parser);
    return WORR_DEMO_CLOCK_SIDEBAND_INTERVENING_SERVICE;
}

worr_demo_clock_sideband_result_v1 Worr_DemoClockSidebandConsumeFrameV1(
    worr_demo_clock_sideband_parser_v1 *parser,
    int32_t expected_server_frame,
    worr_demo_clock_anchor_v1 *anchor_out)
{
    worr_demo_clock_anchor_v1 output;
    if (!parser || !anchor_out || expected_server_frame < 0 ||
        !range_disjoint(parser, sizeof(*parser),
                        anchor_out, sizeof(*anchor_out))) {
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_ARGUMENT;
    }
    if (!Worr_DemoClockSidebandParserValidateV1(parser) ||
        !parser->packet_active) {
        return WORR_DEMO_CLOCK_SIDEBAND_INVALID_STATE;
    }
    if (parser->phase != WORR_DEMO_CLOCK_PHASE_READY) {
        parser_poison(parser);
        return WORR_DEMO_CLOCK_SIDEBAND_MISSING_ANCHOR;
    }
    if (parser->pending.server_frame != expected_server_frame) {
        parser_poison(parser);
        return WORR_DEMO_CLOCK_SIDEBAND_FRAME_MISMATCH;
    }
    output = parser->pending;
    parser_idle(parser);
    *anchor_out = output;
    return WORR_DEMO_CLOCK_SIDEBAND_FRAME_MATCHED;
}
