/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_codec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure, default-off command-shadow support for FR-10-T04.
 *
 * This module owns no cvar, capability, connection, netchan callback, clock,
 * or authoritative command stream.  Callers explicitly retain every object
 * and byte buffer.  Native observations never authorize simulation.
 */
#define WORR_NATIVE_COMMAND_SHADOW_ABI_VERSION 1u
#define WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES                       \
    (WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +                             \
     WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES)
#define WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY 64u
#define WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY 64u
#define WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_BITS 6u
#define WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_MASK                  \
    ((UINT32_C(1) << WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_BITS) - \
     UINT32_C(1))
#define WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_GENERATION_MAX              \
    (UINT32_MAX >> WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_BITS)

enum {
    WORR_NATIVE_COMMAND_SHADOW_BUILDER_INITIALIZED = 1u << 0,
    WORR_NATIVE_COMMAND_SHADOW_BUILDER_SEQUENCE_EXHAUSTED = 1u << 1,
};

typedef struct worr_native_command_shadow_builder_telemetry_v1_s {
    uint64_t build_attempts;
    uint64_t built;
    uint64_t invalid_arguments;
    uint64_t invalid_commands;
    uint64_t wrong_epochs;
    uint64_t out_of_order;
    uint64_t sample_time_overflows;
    uint64_t sequence_exhaustions;
} worr_native_command_shadow_builder_telemetry_v1;

/*
 * A reset-local client producer.  sequence zero is the initial cursor and the
 * first accepted command must therefore be {command_epoch, 1}.  A command ID
 * may never roll this object into another epoch; an explicit reset is needed.
 */
typedef struct worr_native_command_shadow_builder_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint32_t command_epoch;
    uint32_t last_sequence;
    uint64_t sample_time_us;
    uint16_t max_duration_ms;
    uint16_t reserved0;
    uint32_t reserved1;
    worr_native_command_shadow_builder_telemetry_v1 telemetry;
} worr_native_command_shadow_builder_v1;

typedef enum worr_native_command_shadow_build_result_v1_e {
    WORR_NATIVE_COMMAND_SHADOW_BUILD_BUILT = 0,
    WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_ARGUMENT = 1,
    WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_STATE = 2,
    WORR_NATIVE_COMMAND_SHADOW_BUILD_INVALID_COMMAND = 3,
    WORR_NATIVE_COMMAND_SHADOW_BUILD_WRONG_EPOCH = 4,
    WORR_NATIVE_COMMAND_SHADOW_BUILD_OUT_OF_ORDER = 5,
    WORR_NATIVE_COMMAND_SHADOW_BUILD_SAMPLE_TIME_OVERFLOW = 6,
    WORR_NATIVE_COMMAND_SHADOW_BUILD_SEQUENCE_EXHAUSTED = 7,
} worr_native_command_shadow_build_result_v1;

/* Invalid initialization leaves the destination byte-identical. */
bool Worr_NativeCommandShadowBuilderInitV1(
    worr_native_command_shadow_builder_v1 *builder_out,
    uint32_t command_epoch,
    uint16_t max_duration_ms);

bool Worr_NativeCommandShadowBuilderValidateV1(
    const worr_native_command_shadow_builder_v1 *builder);

/*
 * Builds a canonical record with a cumulative command-end clock and an
 * explicitly absent render watermark.  The command is canonicalized through
 * the existing command ABI.  On failure the sequencing/sample clock and
 * record_out are untouched; saturating rejection telemetry may change.
 */
worr_native_command_shadow_build_result_v1
Worr_NativeCommandShadowBuilderBuildV1(
    worr_native_command_shadow_builder_v1 *builder,
    worr_command_id_v1 command_id,
    const worr_prediction_command_v1 *command,
    worr_command_record_v1 *record_out);

enum {
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_REGISTRY_INITIALIZED = 1u << 0,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_OCCUPIED = 1u << 0,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_SLOT_RETIRED = 1u << 1,
};

typedef struct worr_native_command_shadow_payload_telemetry_v1_s {
    uint64_t retain_attempts;
    uint64_t retained;
    uint64_t copy_attempts;
    uint64_t copied;
    uint64_t release_attempts;
    uint64_t released;
    uint64_t invalid_arguments;
    uint64_t invalid_records;
    uint64_t invalid_handles;
    uint64_t output_too_small;
    uint64_t capacity_stalls;
    uint64_t slots_retired;
} worr_native_command_shadow_payload_telemetry_v1;

/*
 * Handles encode a six-bit slot index and a nonzero generation.  A slot is
 * permanently retired when its representable generation is exhausted, so a
 * stale handle can never become valid again within one registry lifetime.
 */
typedef struct worr_native_command_shadow_payload_slot_v1_s {
    uint32_t handle;
    uint32_t generation;
    worr_command_id_v1 command_id;
    uint16_t encoded_bytes;
    uint16_t state_flags;
    uint32_t reserved0;
    uint8_t encoded[WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES];
    uint16_t reserved1;
} worr_native_command_shadow_payload_slot_v1;

typedef struct worr_native_command_shadow_payload_registry_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint16_t capacity;
    uint16_t occupied_count;
    uint16_t retired_count;
    uint16_t next_slot;
    uint16_t max_duration_ms;
    uint16_t reserved0;
    uint32_t reserved1;
    worr_native_command_shadow_payload_telemetry_v1 telemetry;
    worr_native_command_shadow_payload_slot_v1
        slots[WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY];
} worr_native_command_shadow_payload_registry_v1;

typedef enum worr_native_command_shadow_payload_result_v1_e {
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RETAINED = 0,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_COPIED = 1,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_RELEASED = 2,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY_STALL = 3,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_ARGUMENT = 4,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_STATE = 5,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_RECORD = 6,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INVALID_HANDLE = 7,
    WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_OUTPUT_TOO_SMALL = 8,
} worr_native_command_shadow_payload_result_v1;

bool Worr_NativeCommandShadowPayloadRegistryInitV1(
    worr_native_command_shadow_payload_registry_v1 *registry_out,
    uint16_t max_duration_ms);

bool Worr_NativeCommandShadowPayloadRegistryValidateV1(
    const worr_native_command_shadow_payload_registry_v1 *registry);

/* Encodes and atomically retains exactly one fixed command-codec payload. */
worr_native_command_shadow_payload_result_v1
Worr_NativeCommandShadowPayloadRetainV1(
    worr_native_command_shadow_payload_registry_v1 *registry,
    const worr_command_record_v1 *record,
    uint32_t *handle_out);

/*
 * Copies the exact retained codec bytes.  All requested outputs are untouched
 * on failure.  Output regions must not overlap one another or the registry.
 */
worr_native_command_shadow_payload_result_v1
Worr_NativeCommandShadowPayloadCopyV1(
    worr_native_command_shadow_payload_registry_v1 *registry,
    uint32_t handle,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out,
    worr_command_id_v1 *command_id_out);

/* Exact generation-bearing handles make release safe against stale callers. */
worr_native_command_shadow_payload_result_v1
Worr_NativeCommandShadowPayloadReleaseV1(
    worr_native_command_shadow_payload_registry_v1 *registry,
    uint32_t handle);

enum {
    WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_INITIALIZED = 1u << 0,
    WORR_NATIVE_COMMAND_SHADOW_COMPARATOR_OFFSET_ESTABLISHED = 1u << 1,
};

typedef enum worr_native_command_shadow_sample_offset_direction_v1_e {
    WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_UNSET = 0,
    WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_EQUAL = 1,
    WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_LEGACY_AHEAD = 2,
    WORR_NATIVE_COMMAND_SHADOW_SAMPLE_OFFSET_NATIVE_AHEAD = 3,
} worr_native_command_shadow_sample_offset_direction_v1;

enum {
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MATCH = 1u << 0,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_MODEL_MATCH = 1u << 1,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MATCH = 1u << 2,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_ESTABLISHED_NOW = 1u << 3,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_OFFSET_MATCH = 1u << 4,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_WATERMARK_UNVERIFIED = 1u << 5,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_FULL_RECORD_PARITY_NOT_CLAIMED =
        1u << 6,
};

typedef enum worr_native_command_shadow_compare_result_v1_e {
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_MATCHED_WATERMARK_UNVERIFIED = 0,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_ARGUMENT = 1,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_STATE = 2,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_INVALID_RECORD = 3,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_ID_MISMATCH = 4,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_COMMAND_MISMATCH = 5,
    WORR_NATIVE_COMMAND_SHADOW_COMPARE_SAMPLE_OFFSET_MISMATCH = 6,
} worr_native_command_shadow_compare_result_v1;

typedef struct worr_native_command_shadow_compare_telemetry_v1_s {
    uint64_t compare_attempts;
    uint64_t matched;
    uint64_t invalid_records;
    uint64_t id_mismatches;
    uint64_t command_mismatches;
    uint64_t offsets_established;
    uint64_t offset_mismatches;
    uint64_t watermarks_unverified;
} worr_native_command_shadow_compare_telemetry_v1;

typedef struct worr_native_command_shadow_comparator_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint16_t max_duration_ms;
    uint8_t sample_offset_direction;
    uint8_t reserved0;
    uint32_t reserved1;
    uint64_t sample_offset_us;
    worr_native_command_shadow_compare_telemetry_v1 telemetry;
} worr_native_command_shadow_comparator_v1;

typedef struct worr_native_command_shadow_compare_report_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t result;
    uint32_t flags;
    uint8_t observed_offset_direction;
    uint8_t expected_offset_direction;
    uint8_t reserved0[2];
    worr_command_id_v1 native_command_id;
    worr_command_id_v1 legacy_command_id;
    uint64_t native_sample_time_us;
    uint64_t legacy_sample_time_us;
    uint64_t observed_offset_us;
    uint64_t expected_offset_us;
    uint32_t native_model_revision;
    uint32_t legacy_model_revision;
    uint64_t reserved1;
} worr_native_command_shadow_compare_report_v1;

bool Worr_NativeCommandShadowComparatorInitV1(
    worr_native_command_shadow_comparator_v1 *comparator_out,
    uint16_t max_duration_ms);

bool Worr_NativeCommandShadowComparatorValidateV1(
    const worr_native_command_shadow_comparator_v1 *comparator);

/*
 * Both inputs first pass the accepted canonical-record validator, which admits
 * only WORR_PREDICTION_MODEL_REVISION in V1.  An unsupported or differing model
 * is therefore INVALID_RECORD, not a model-mismatch comparison.  The remaining
 * comparison covers canonical ID, prediction-command semantic fields, and a
 * connection-constant sample-time offset.  Render watermarks are deliberately
 * unverified and every successful report says so.
 */
worr_native_command_shadow_compare_result_v1
Worr_NativeCommandShadowCompareV1(
    worr_native_command_shadow_comparator_v1 *comparator,
    const worr_command_record_v1 *native_record,
    const worr_command_record_v1 *legacy_record,
    worr_native_command_shadow_compare_report_v1 *report_out);

enum {
    WORR_NATIVE_COMMAND_SHADOW_JOIN_INITIALIZED = 1u << 0,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_OCCUPIED = 1u << 0,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_NATIVE = 1u << 1,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_LEGACY = 1u << 2,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_SLOT_COMPARED = 1u << 3,
};

typedef enum worr_native_command_shadow_join_side_v1_e {
    WORR_NATIVE_COMMAND_SHADOW_JOIN_NATIVE = 1,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_LEGACY = 2,
} worr_native_command_shadow_join_side_v1;

typedef enum worr_native_command_shadow_join_result_v1_e {
    WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_NATIVE = 0,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_STORED_LEGACY = 1,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_DUPLICATE_NATIVE = 2,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_DUPLICATE_LEGACY = 3,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_MATCHED_WATERMARK_UNVERIFIED = 4,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_COMMAND_MISMATCH = 5,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_SAMPLE_OFFSET_MISMATCH = 6,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_CONFLICTING_NATIVE = 7,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_CONFLICTING_LEGACY = 8,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_PRUNE_COMPLETE = 9,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_FOUND = 10,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_NOT_FOUND = 11,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY_STALL = 12,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_ARGUMENT = 13,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_STATE = 14,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_INVALID_RECORD = 15,
    WORR_NATIVE_COMMAND_SHADOW_JOIN_CLOCK_REGRESSION = 16,
} worr_native_command_shadow_join_result_v1;

typedef struct worr_native_command_shadow_join_telemetry_v1_s {
    uint64_t observe_attempts;
    uint64_t native_stored;
    uint64_t legacy_stored;
    uint64_t native_duplicates;
    uint64_t legacy_duplicates;
    uint64_t native_conflicts;
    uint64_t legacy_conflicts;
    uint64_t comparisons_completed;
    uint64_t matches;
    uint64_t command_mismatches;
    uint64_t sample_offset_mismatches;
    uint64_t capacity_stalls;
    uint64_t invalid_arguments;
    uint64_t invalid_records;
    uint64_t clock_regressions;
    uint64_t prune_attempts;
    uint64_t slots_pruned;
} worr_native_command_shadow_join_telemetry_v1;

typedef struct worr_native_command_shadow_join_slot_v1_s {
    worr_command_id_v1 command_id;
    uint64_t last_update_tick;
    uint32_t state_flags;
    uint32_t reserved0;
    worr_command_record_v1 native_record;
    worr_command_record_v1 legacy_record;
    worr_native_command_shadow_compare_report_v1 comparison;
} worr_native_command_shadow_join_slot_v1;

typedef struct worr_native_command_shadow_join_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    uint16_t capacity;
    uint16_t occupied_count;
    uint16_t max_duration_ms;
    uint16_t reserved0;
    uint64_t expiry_ticks;
    uint64_t last_tick;
    worr_native_command_shadow_comparator_v1 comparator;
    worr_native_command_shadow_join_telemetry_v1 telemetry;
    worr_native_command_shadow_join_slot_v1
        slots[WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY];
} worr_native_command_shadow_join_v1;

bool Worr_NativeCommandShadowJoinInitV1(
    worr_native_command_shadow_join_v1 *join_out,
    uint16_t max_duration_ms,
    uint64_t expiry_ticks);

bool Worr_NativeCommandShadowJoinValidateV1(
    const worr_native_command_shadow_join_v1 *join);

/*
 * Inserts one validated half keyed only by record.command_id.  Semantically
 * equal repeated halves are idempotent, including the command ABI's signed-zero
 * equivalence; different semantic content for an occupied side is a
 * conflicting duplicate and never replaces the first observation.  A report
 * is written only when the insertion completes a pair.
 */
worr_native_command_shadow_join_result_v1
Worr_NativeCommandShadowJoinObserveV1(
    worr_native_command_shadow_join_v1 *join,
    worr_native_command_shadow_join_side_v1 side,
    const worr_command_record_v1 *record,
    uint64_t now_tick,
    worr_native_command_shadow_compare_report_v1 *report_out);

/* Slots expire when now_tick - last_update_tick is at least expiry_ticks. */
worr_native_command_shadow_join_result_v1
Worr_NativeCommandShadowJoinPruneV1(
    worr_native_command_shadow_join_v1 *join,
    uint64_t now_tick,
    uint32_t *pruned_count_out);

/* A successful lookup copies a stable slot image and never mutates the join. */
worr_native_command_shadow_join_result_v1
Worr_NativeCommandShadowJoinFindV1(
    const worr_native_command_shadow_join_v1 *join,
    worr_command_id_v1 command_id,
    worr_native_command_shadow_join_slot_v1 *slot_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(condition, message) \
    static_assert(condition, message)
#else
#define WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(condition, message) \
    _Static_assert(condition, message)
#endif

WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES == 110,
    "native command shadow command-codec size changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    (UINT32_C(1) << WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_INDEX_BITS) ==
        WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY,
    "native command shadow handle index width changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_builder_telemetry_v1) == 64,
    "native command shadow builder telemetry layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_builder_v1) == 96,
    "native command shadow builder layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_payload_telemetry_v1) == 96,
    "native command shadow payload telemetry layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_payload_slot_v1) == 136,
    "native command shadow payload slot layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_payload_registry_v1) == 8824,
    "native command shadow payload registry layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_compare_telemetry_v1) == 64,
    "native command shadow comparator telemetry layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_comparator_v1) == 88,
    "native command shadow comparator layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_compare_report_v1) == 80,
    "native command shadow compare report layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_join_telemetry_v1) == 136,
    "native command shadow join telemetry layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_join_slot_v1) == 312,
    "native command shadow join slot layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    sizeof(worr_native_command_shadow_join_v1) == 20224,
    "native command shadow join layout changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    offsetof(worr_native_command_shadow_builder_v1, telemetry) == 32,
    "native command shadow builder telemetry offset changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    offsetof(worr_native_command_shadow_payload_registry_v1, slots) == 120,
    "native command shadow payload slot offset changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    offsetof(worr_native_command_shadow_comparator_v1, telemetry) == 24,
    "native command shadow comparator telemetry offset changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    offsetof(worr_native_command_shadow_join_slot_v1, comparison) == 232,
    "native command shadow join comparison offset changed");
WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT(
    offsetof(worr_native_command_shadow_join_v1, slots) == 256,
    "native command shadow join slot-array offset changed");

#undef WORR_NATIVE_COMMAND_SHADOW_STATIC_ASSERT
