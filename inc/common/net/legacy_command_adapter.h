/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/command_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_LEGACY_COMMAND_SIDEBAND_VERSION 1u
#define WORR_LEGACY_COMMAND_SIDEBAND_PAIR_COUNT 9u
#define WORR_LEGACY_COMMAND_NO_STREAM_RESULT UINT32_MAX

/* Exact bounds of the supported legacy carriers after q2proto decode. */
#define WORR_LEGACY_COMMAND_MOVE_COUNT 3u
#define WORR_LEGACY_COMMAND_BATCH_MAX_FRAMES 4u
#define WORR_LEGACY_COMMAND_BATCH_MAX_PER_FRAME 31u
#define WORR_LEGACY_COMMAND_BATCH_MAX_COUNT                         \
    (WORR_LEGACY_COMMAND_BATCH_MAX_FRAMES *                        \
     WORR_LEGACY_COMMAND_BATCH_MAX_PER_FRAME)

/*
 * Negative setting indices are ignored by the unextended server setting
 * table.  A negotiated implementation may reserve this consecutive range
 * without changing q2proto's signed-int16 CLC_SETTING representation.
 */
enum {
    WORR_LEGACY_COMMAND_SETTING_BEGIN = -32000,
    WORR_LEGACY_COMMAND_SETTING_EPOCH_LOW = -31999,
    WORR_LEGACY_COMMAND_SETTING_EPOCH_HIGH = -31998,
    WORR_LEGACY_COMMAND_SETTING_SEQUENCE_LOW = -31997,
    WORR_LEGACY_COMMAND_SETTING_SEQUENCE_HIGH = -31996,
    WORR_LEGACY_COMMAND_SETTING_COUNT = -31995,
    WORR_LEGACY_COMMAND_SETTING_CHECKSUM_LOW = -31994,
    WORR_LEGACY_COMMAND_SETTING_CHECKSUM_HIGH = -31993,
    WORR_LEGACY_COMMAND_SETTING_COMMIT = -31992,
};

typedef struct worr_legacy_command_setting_pair_v1_s {
    int16_t index;
    int16_t value;
} worr_legacy_command_setting_pair_v1;

typedef struct worr_legacy_command_range_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_command_id_v1 first_command_id;
    uint16_t command_count;
    uint16_t reserved0;
    uint32_t header_checksum;
} worr_legacy_command_range_v1;

typedef enum worr_legacy_command_carrier_v1_e {
    WORR_LEGACY_COMMAND_CARRIER_MOVE = 1,
    WORR_LEGACY_COMMAND_CARRIER_BATCH_MOVE = 2,
} worr_legacy_command_carrier_v1;

typedef enum worr_legacy_command_sideband_result_v1_e {
    WORR_LEGACY_COMMAND_SIDEBAND_PACKET_STARTED = 0,
    WORR_LEGACY_COMMAND_SIDEBAND_PACKET_ENDED = 1,
    WORR_LEGACY_COMMAND_SIDEBAND_NOT_SIDEBAND = 2,
    WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED = 3,
    WORR_LEGACY_COMMAND_SIDEBAND_HEADER_COMMITTED = 4,
    WORR_LEGACY_COMMAND_SIDEBAND_MOVE_MATCHED = 5,
    WORR_LEGACY_COMMAND_SIDEBAND_RESET_PACKET_BOUNDARY = 6,
    WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE = 7,
    WORR_LEGACY_COMMAND_SIDEBAND_UNSUPPORTED_VERSION = 8,
    WORR_LEGACY_COMMAND_SIDEBAND_UNEXPECTED_FIELD = 9,
    WORR_LEGACY_COMMAND_SIDEBAND_CHECKSUM_MISMATCH = 10,
    WORR_LEGACY_COMMAND_SIDEBAND_COMMIT_MISMATCH = 11,
    WORR_LEGACY_COMMAND_SIDEBAND_MISSING_HEADER = 12,
    WORR_LEGACY_COMMAND_SIDEBAND_COUNT_MISMATCH = 13,
    WORR_LEGACY_COMMAND_SIDEBAND_INVALID_CARRIER = 14,
    WORR_LEGACY_COMMAND_SIDEBAND_INVALID_ARGUMENT = 15,
    WORR_LEGACY_COMMAND_SIDEBAND_INVALID_STATE = 16,
} worr_legacy_command_sideband_result_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_legacy_command_sideband_telemetry_v1_s {
    uint64_t packet_begins;
    uint64_t packet_ends;
    uint64_t packet_boundary_resets;
    uint64_t settings_seen;
    uint64_t non_sideband_settings;
    uint64_t sideband_begins;
    uint64_t fields_accepted;
    uint64_t headers_committed;
    uint64_t moves_seen;
    uint64_t moves_matched;
    uint64_t intervening_resets;
    uint64_t dangling_headers;
    uint64_t malformed_order;
    uint64_t unsupported_versions;
    uint64_t checksum_failures;
    uint64_t commit_failures;
    uint64_t missing_headers;
    uint64_t count_mismatches;
    uint64_t invalid_carriers;
    uint64_t invalid_arguments;
    uint64_t invalid_state;
} worr_legacy_command_sideband_telemetry_v1;

enum {
    WORR_LEGACY_COMMAND_PHASE_IDLE = 0,
    WORR_LEGACY_COMMAND_PHASE_EPOCH_LOW = 1,
    WORR_LEGACY_COMMAND_PHASE_EPOCH_HIGH = 2,
    WORR_LEGACY_COMMAND_PHASE_SEQUENCE_LOW = 3,
    WORR_LEGACY_COMMAND_PHASE_SEQUENCE_HIGH = 4,
    WORR_LEGACY_COMMAND_PHASE_COUNT = 5,
    WORR_LEGACY_COMMAND_PHASE_CHECKSUM_LOW = 6,
    WORR_LEGACY_COMMAND_PHASE_CHECKSUM_HIGH = 7,
    WORR_LEGACY_COMMAND_PHASE_COMMIT = 8,
    WORR_LEGACY_COMMAND_PHASE_MOVE = 9,
    WORR_LEGACY_COMMAND_PHASE_POISONED = 10,
};

typedef struct worr_legacy_command_sideband_parser_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t phase;
    uint32_t packet_active;
    worr_legacy_command_range_v1 pending_range;
    uint16_t checksum_low;
    uint16_t checksum_high;
    uint32_t reserved0;
    worr_legacy_command_sideband_telemetry_v1 telemetry;
} worr_legacy_command_sideband_parser_v1;

bool Worr_LegacyCommandRangeInitV1(
    worr_legacy_command_range_v1 *range,
    worr_command_id_v1 first_command_id,
    uint16_t command_count);
bool Worr_LegacyCommandRangeValidateV1(
    const worr_legacy_command_range_v1 *range);
bool Worr_LegacyCommandSidebandEncodeV1(
    const worr_legacy_command_range_v1 *range,
    worr_legacy_command_setting_pair_v1 *pairs,
    uint32_t pair_capacity);

bool Worr_LegacyCommandSidebandParserInitV1(
    worr_legacy_command_sideband_parser_v1 *parser);
bool Worr_LegacyCommandSidebandParserValidateV1(
    const worr_legacy_command_sideband_parser_v1 *parser);
worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandPacketBeginV1(
    worr_legacy_command_sideband_parser_v1 *parser);
worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandPacketEndV1(
    worr_legacy_command_sideband_parser_v1 *parser);
worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandObserveSettingV1(
    worr_legacy_command_sideband_parser_v1 *parser,
    int16_t index,
    int16_t value);
worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandObserveInterveningServiceV1(
    worr_legacy_command_sideband_parser_v1 *parser);
worr_legacy_command_sideband_result_v1
Worr_LegacyCommandSidebandConsumeMoveV1(
    worr_legacy_command_sideband_parser_v1 *parser,
    uint32_t carrier,
    uint32_t decoded_command_count,
    worr_legacy_command_range_v1 *range_out);

typedef enum worr_legacy_command_adapter_result_v1_e {
    WORR_LEGACY_COMMAND_ADAPTER_APPLIED = 0,
    WORR_LEGACY_COMMAND_ADAPTER_IDEMPOTENT = 1,
    WORR_LEGACY_COMMAND_ADAPTER_INVALID_ARGUMENT = 2,
    WORR_LEGACY_COMMAND_ADAPTER_INVALID_STREAM = 3,
    WORR_LEGACY_COMMAND_ADAPTER_INVALID_RANGE = 4,
    WORR_LEGACY_COMMAND_ADAPTER_COUNT_MISMATCH = 5,
    WORR_LEGACY_COMMAND_ADAPTER_INVALID_COMMAND = 6,
    WORR_LEGACY_COMMAND_ADAPTER_INVALID_WATERMARK = 7,
    WORR_LEGACY_COMMAND_ADAPTER_ALIAS_VIOLATION = 8,
    WORR_LEGACY_COMMAND_ADAPTER_INSUFFICIENT_SCRATCH = 9,
    WORR_LEGACY_COMMAND_ADAPTER_FUTURE_GAP = 10,
    WORR_LEGACY_COMMAND_ADAPTER_WRONG_EPOCH = 11,
    WORR_LEGACY_COMMAND_ADAPTER_CONFLICT = 12,
    WORR_LEGACY_COMMAND_ADAPTER_CAPACITY = 13,
    WORR_LEGACY_COMMAND_ADAPTER_SAMPLE_TIME_OVERFLOW = 14,
    WORR_LEGACY_COMMAND_ADAPTER_EPOCH_EXHAUSTED = 15,
    WORR_LEGACY_COMMAND_ADAPTER_STREAM_REJECTED = 16,
} worr_legacy_command_adapter_result_v1;

typedef struct worr_legacy_command_adapter_report_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t result;
    uint32_t stream_result;
    uint32_t input_count;
    uint32_t inserted_count;
    uint32_t duplicate_count;
    uint32_t stale_count;
    worr_command_id_v1 first_command_id;
    worr_command_id_v1 last_command_id;
    worr_command_cursor_v1 received_cursor;
    uint64_t last_received_sample_time_us;
} worr_legacy_command_adapter_report_v1;

/*
 * Adapt an already decoded, oldest-to-newest legacy command range.  The input
 * payload is the canonical T02 prediction command; this API deliberately does
 * not define a second usercmd representation.  The packet-shared render
 * watermark is retained as diagnostic provenance only by command ABI rules.
 *
 * The operation validates and stages the complete range before committing the
 * command stream.  `record_scratch` needs command_count records and
 * `stream_scratch` needs stream->capacity slots.  All mutable and immutable
 * object ranges must be non-overlapping.  On every adapter failure the stream
 * envelope and live storage are byte-identical to their entry state.
 */
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
    worr_legacy_command_adapter_report_v1 *report_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LEGACY_COMMAND_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_LEGACY_COMMAND_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_LEGACY_COMMAND_STATIC_ASSERT(
    WORR_LEGACY_COMMAND_BATCH_MAX_COUNT == 124u,
    "legacy batch carrier bound changed");
WORR_LEGACY_COMMAND_STATIC_ASSERT(
    sizeof(worr_legacy_command_setting_pair_v1) == 4,
    "legacy command setting-pair layout changed");
WORR_LEGACY_COMMAND_STATIC_ASSERT(
    sizeof(worr_legacy_command_range_v1) == 24,
    "legacy command range layout changed");
WORR_LEGACY_COMMAND_STATIC_ASSERT(
    offsetof(worr_legacy_command_range_v1, first_command_id) == 8,
    "legacy command range ID offset changed");
WORR_LEGACY_COMMAND_STATIC_ASSERT(
    offsetof(worr_legacy_command_range_v1, header_checksum) == 20,
    "legacy command range checksum offset changed");
WORR_LEGACY_COMMAND_STATIC_ASSERT(
    sizeof(worr_legacy_command_sideband_telemetry_v1) == 168,
    "legacy command sideband telemetry layout changed");
WORR_LEGACY_COMMAND_STATIC_ASSERT(
    sizeof(worr_legacy_command_sideband_parser_v1) == 216,
    "legacy command sideband parser layout changed");
WORR_LEGACY_COMMAND_STATIC_ASSERT(
    sizeof(worr_legacy_command_adapter_report_v1) == 64,
    "legacy command adapter report layout changed");

#undef WORR_LEGACY_COMMAND_STATIC_ASSERT
