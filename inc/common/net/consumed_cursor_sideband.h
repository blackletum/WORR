/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/command_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_CONSUMED_CURSOR_SIDEBAND_VERSION 1u
#define WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT 5u

/*
 * Server-to-client setting indices.  The carrier uses the signed 32-bit
 * SVC_SETTING index/value representation; values are bit containers, not
 * signed arithmetic.  The range is intentionally separate from capability
 * confirmation (-31799..-31797) and the CLC command sideband.
 */
enum {
    WORR_CONSUMED_CURSOR_SETTING_BEGIN = -31790,
    WORR_CONSUMED_CURSOR_SETTING_EPOCH = -31789,
    WORR_CONSUMED_CURSOR_SETTING_SEQUENCE = -31788,
    WORR_CONSUMED_CURSOR_SETTING_CHECKSUM = -31787,
    WORR_CONSUMED_CURSOR_SETTING_COMMIT = -31786,
};

typedef struct worr_consumed_cursor_setting_pair_v1_s {
    int32_t index;
    int32_t value;
} worr_consumed_cursor_setting_pair_v1;

/*
 * Pointer-free record delivered atomically with the following snapshot
 * frame.  {0, 0} is the sole absent cursor and is allowed only so a server can
 * publish frames before its canonical command stream has been initialized.
 * {nonzero epoch, 0} is an initialized stream with no consumed command.
 */
typedef struct worr_consumed_cursor_sideband_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_command_cursor_v1 consumed_cursor;
    uint32_t header_checksum;
    uint32_t reserved0;
} worr_consumed_cursor_sideband_v1;

typedef enum worr_consumed_cursor_sideband_result_v1_e {
    WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_STARTED = 0,
    WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED = 1,
    WORR_CONSUMED_CURSOR_SIDEBAND_NOT_SIDEBAND = 2,
    WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED = 3,
    WORR_CONSUMED_CURSOR_SIDEBAND_HEADER_COMMITTED = 4,
    WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED = 5,
    WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY = 6,
    WORR_CONSUMED_CURSOR_SIDEBAND_RESET_INTERVENING_SERVICE = 7,
    WORR_CONSUMED_CURSOR_SIDEBAND_UNSUPPORTED_VERSION = 8,
    WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD = 9,
    WORR_CONSUMED_CURSOR_SIDEBAND_CHECKSUM_MISMATCH = 10,
    WORR_CONSUMED_CURSOR_SIDEBAND_COMMIT_MISMATCH = 11,
    WORR_CONSUMED_CURSOR_SIDEBAND_MISSING_HEADER = 12,
    WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT = 13,
    WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE = 14,
} worr_consumed_cursor_sideband_result_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_consumed_cursor_sideband_telemetry_v1_s {
    uint64_t packet_begins;
    uint64_t packet_ends;
    uint64_t packet_boundary_failures;
    uint64_t settings_seen;
    uint64_t non_sideband_settings;
    uint64_t sideband_begins;
    uint64_t fields_accepted;
    uint64_t headers_committed;
    uint64_t frames_seen;
    uint64_t frames_matched;
    uint64_t intervening_failures;
    uint64_t dangling_headers;
    uint64_t malformed_order;
    uint64_t unsupported_versions;
    uint64_t checksum_failures;
    uint64_t commit_failures;
    uint64_t missing_headers;
    uint64_t invalid_arguments;
    uint64_t invalid_state;
} worr_consumed_cursor_sideband_telemetry_v1;

enum {
    WORR_CONSUMED_CURSOR_PHASE_IDLE = 0,
    WORR_CONSUMED_CURSOR_PHASE_EPOCH = 1,
    WORR_CONSUMED_CURSOR_PHASE_SEQUENCE = 2,
    WORR_CONSUMED_CURSOR_PHASE_CHECKSUM = 3,
    WORR_CONSUMED_CURSOR_PHASE_COMMIT = 4,
    WORR_CONSUMED_CURSOR_PHASE_FRAME = 5,
    WORR_CONSUMED_CURSOR_PHASE_POISONED = 6,
};

/* Fixed-size runtime parser.  It owns no storage and contains no pointers. */
typedef struct worr_consumed_cursor_sideband_parser_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t phase;
    uint32_t packet_active;
    worr_consumed_cursor_sideband_v1 pending;
    uint32_t observed_checksum;
    uint32_t reserved0;
    worr_consumed_cursor_sideband_telemetry_v1 telemetry;
} worr_consumed_cursor_sideband_parser_v1;

bool Worr_ConsumedCursorSidebandInitV1(
    worr_consumed_cursor_sideband_v1 *sideband,
    worr_command_cursor_v1 consumed_cursor);
bool Worr_ConsumedCursorSidebandValidateV1(
    const worr_consumed_cursor_sideband_v1 *sideband);
bool Worr_ConsumedCursorSidebandEncodeV1(
    const worr_consumed_cursor_sideband_v1 *sideband,
    worr_consumed_cursor_setting_pair_v1 *pairs,
    uint32_t pair_capacity);

bool Worr_ConsumedCursorSidebandParserInitV1(
    worr_consumed_cursor_sideband_parser_v1 *parser);
bool Worr_ConsumedCursorSidebandParserValidateV1(
    const worr_consumed_cursor_sideband_parser_v1 *parser);
worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandPacketBeginV1(
    worr_consumed_cursor_sideband_parser_v1 *parser);
worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandPacketEndV1(
    worr_consumed_cursor_sideband_parser_v1 *parser);
worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandObserveSettingV1(
    worr_consumed_cursor_sideband_parser_v1 *parser,
    int32_t index,
    int32_t value);
worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandObserveInterveningServiceV1(
    worr_consumed_cursor_sideband_parser_v1 *parser);

/*
 * Call only when the next decoded service is a snapshot frame.  Success
 * consumes the committed tuple and copies it to sideband_out.  On failure the
 * output is untouched; partial or poisoned headers remain fail-closed until
 * PacketEnd resets the parser.
 */
worr_consumed_cursor_sideband_result_v1
Worr_ConsumedCursorSidebandConsumeFrameV1(
    worr_consumed_cursor_sideband_parser_v1 *parser,
    worr_consumed_cursor_sideband_v1 *sideband_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_CONSUMED_CURSOR_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_CONSUMED_CURSOR_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_CONSUMED_CURSOR_STATIC_ASSERT(
    sizeof(worr_consumed_cursor_setting_pair_v1) == 8,
    "consumed cursor setting pair v1 layout changed");
WORR_CONSUMED_CURSOR_STATIC_ASSERT(
    sizeof(worr_consumed_cursor_sideband_v1) == 24,
    "consumed cursor sideband v1 layout changed");
WORR_CONSUMED_CURSOR_STATIC_ASSERT(
    offsetof(worr_consumed_cursor_sideband_v1, consumed_cursor) == 8,
    "consumed cursor sideband cursor offset changed");
WORR_CONSUMED_CURSOR_STATIC_ASSERT(
    offsetof(worr_consumed_cursor_sideband_v1, header_checksum) == 16,
    "consumed cursor sideband checksum offset changed");
WORR_CONSUMED_CURSOR_STATIC_ASSERT(
    sizeof(worr_consumed_cursor_sideband_telemetry_v1) == 152,
    "consumed cursor telemetry v1 layout changed");
WORR_CONSUMED_CURSOR_STATIC_ASSERT(
    sizeof(worr_consumed_cursor_sideband_parser_v1) == 200,
    "consumed cursor parser v1 layout changed");
WORR_CONSUMED_CURSOR_STATIC_ASSERT(
    offsetof(worr_consumed_cursor_sideband_parser_v1, telemetry) == 48,
    "consumed cursor parser telemetry offset changed");

#undef WORR_CONSUMED_CURSOR_STATIC_ASSERT
