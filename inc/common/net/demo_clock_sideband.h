/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_DEMO_CLOCK_SIDEBAND_VERSION 1u
#define WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT 6u

/*
 * Client-generated, demo-seek-only SVC_SETTING range.  It is deliberately
 * disjoint from capability confirmation (-31799..-31797), consumed-command
 * cursor transport (-31790..-31786), and command transport (-32000..-31992).
 * Live servers are never authorized to use this range.
 */
enum {
    WORR_DEMO_CLOCK_SETTING_BEGIN = -31780,
    WORR_DEMO_CLOCK_SETTING_FRAME = -31779,
    WORR_DEMO_CLOCK_SETTING_TIME_LOW = -31778,
    WORR_DEMO_CLOCK_SETTING_TIME_HIGH = -31777,
    WORR_DEMO_CLOCK_SETTING_CHECKSUM = -31776,
    WORR_DEMO_CLOCK_SETTING_COMMIT = -31775,
};

typedef struct worr_demo_clock_setting_pair_v1_s {
    int32_t index;
    int32_t value;
} worr_demo_clock_setting_pair_v1;

typedef struct worr_demo_clock_anchor_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    int32_t server_frame;
    uint32_t reserved0;
    uint64_t server_time_us;
    uint32_t checksum;
    uint32_t reserved1;
} worr_demo_clock_anchor_v1;

typedef enum worr_demo_clock_sideband_result_v1_e {
    WORR_DEMO_CLOCK_SIDEBAND_PACKET_STARTED = 0,
    WORR_DEMO_CLOCK_SIDEBAND_PACKET_ENDED = 1,
    WORR_DEMO_CLOCK_SIDEBAND_NOT_SIDEBAND = 2,
    WORR_DEMO_CLOCK_SIDEBAND_FIELD_ACCEPTED = 3,
    WORR_DEMO_CLOCK_SIDEBAND_ANCHOR_COMMITTED = 4,
    WORR_DEMO_CLOCK_SIDEBAND_FRAME_MATCHED = 5,
    WORR_DEMO_CLOCK_SIDEBAND_PACKET_BOUNDARY = 6,
    WORR_DEMO_CLOCK_SIDEBAND_INTERVENING_SERVICE = 7,
    WORR_DEMO_CLOCK_SIDEBAND_UNSUPPORTED_VERSION = 8,
    WORR_DEMO_CLOCK_SIDEBAND_UNEXPECTED_FIELD = 9,
    WORR_DEMO_CLOCK_SIDEBAND_CHECKSUM_MISMATCH = 10,
    WORR_DEMO_CLOCK_SIDEBAND_COMMIT_MISMATCH = 11,
    WORR_DEMO_CLOCK_SIDEBAND_MISSING_ANCHOR = 12,
    WORR_DEMO_CLOCK_SIDEBAND_FRAME_MISMATCH = 13,
    WORR_DEMO_CLOCK_SIDEBAND_INVALID_ARGUMENT = 14,
    WORR_DEMO_CLOCK_SIDEBAND_INVALID_STATE = 15,
} worr_demo_clock_sideband_result_v1;

enum {
    WORR_DEMO_CLOCK_PHASE_IDLE = 0,
    WORR_DEMO_CLOCK_PHASE_FRAME = 1,
    WORR_DEMO_CLOCK_PHASE_TIME_LOW = 2,
    WORR_DEMO_CLOCK_PHASE_TIME_HIGH = 3,
    WORR_DEMO_CLOCK_PHASE_CHECKSUM = 4,
    WORR_DEMO_CLOCK_PHASE_COMMIT = 5,
    WORR_DEMO_CLOCK_PHASE_READY = 6,
    WORR_DEMO_CLOCK_PHASE_POISONED = 7,
};

typedef struct worr_demo_clock_sideband_parser_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t phase;
    uint32_t packet_active;
    worr_demo_clock_anchor_v1 pending;
} worr_demo_clock_sideband_parser_v1;

bool Worr_DemoClockSettingRecognizedV1(int32_t index);
bool Worr_DemoClockAnchorInitV1(worr_demo_clock_anchor_v1 *anchor,
                                int32_t server_frame,
                                uint64_t server_time_us);
bool Worr_DemoClockAnchorValidateV1(
    const worr_demo_clock_anchor_v1 *anchor);
bool Worr_DemoClockAnchorEncodeV1(
    const worr_demo_clock_anchor_v1 *anchor,
    worr_demo_clock_setting_pair_v1 *pairs,
    uint32_t pair_capacity);

bool Worr_DemoClockSidebandParserInitV1(
    worr_demo_clock_sideband_parser_v1 *parser);
bool Worr_DemoClockSidebandParserValidateV1(
    const worr_demo_clock_sideband_parser_v1 *parser);
worr_demo_clock_sideband_result_v1 Worr_DemoClockSidebandPacketBeginV1(
    worr_demo_clock_sideband_parser_v1 *parser);
worr_demo_clock_sideband_result_v1 Worr_DemoClockSidebandPacketEndV1(
    worr_demo_clock_sideband_parser_v1 *parser);
worr_demo_clock_sideband_result_v1 Worr_DemoClockSidebandObserveSettingV1(
    worr_demo_clock_sideband_parser_v1 *parser,
    int32_t index,
    int32_t value);
worr_demo_clock_sideband_result_v1
Worr_DemoClockSidebandObserveInterveningServiceV1(
    worr_demo_clock_sideband_parser_v1 *parser);
worr_demo_clock_sideband_result_v1 Worr_DemoClockSidebandConsumeFrameV1(
    worr_demo_clock_sideband_parser_v1 *parser,
    int32_t expected_server_frame,
    worr_demo_clock_anchor_v1 *anchor_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_DEMO_CLOCK_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_DEMO_CLOCK_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_DEMO_CLOCK_STATIC_ASSERT(
    sizeof(worr_demo_clock_setting_pair_v1) == 8,
    "demo clock setting pair v1 layout changed");
WORR_DEMO_CLOCK_STATIC_ASSERT(
    sizeof(worr_demo_clock_anchor_v1) == 32,
    "demo clock anchor v1 layout changed");
WORR_DEMO_CLOCK_STATIC_ASSERT(
    offsetof(worr_demo_clock_anchor_v1, server_time_us) == 16,
    "demo clock anchor time offset changed");
WORR_DEMO_CLOCK_STATIC_ASSERT(
    sizeof(worr_demo_clock_sideband_parser_v1) == 48,
    "demo clock parser v1 layout changed");
WORR_DEMO_CLOCK_STATIC_ASSERT(
    offsetof(worr_demo_clock_sideband_parser_v1, pending) == 16,
    "demo clock parser pending offset changed");

#undef WORR_DEMO_CLOCK_STATIC_ASSERT
