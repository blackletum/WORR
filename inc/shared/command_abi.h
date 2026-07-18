/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/prediction_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_COMMAND_ABI_VERSION 1u
#define WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS 250u
#define WORR_COMMAND_MAX_TICK_INTERVAL_US 1000000u
#define WORR_COMMAND_MAX_EXTRAPOLATION_INTERVALS 4u
#define WORR_COMMAND_MAX_RENDER_OFFSET_US 250000u

/* Epoch and sequence zero are reserved for absent IDs. */
typedef struct worr_command_id_v1_s {
    uint32_t epoch;
    uint32_t sequence;
} worr_command_id_v1;

/*
 * A cursor names the highest contiguous command in an epoch.  Sequence zero
 * is valid here and means that no command in the epoch has been received or
 * consumed.  It is never a real command ID.
 */
typedef struct worr_command_cursor_v1_s {
    uint32_t epoch;
    uint32_t contiguous_sequence;
} worr_command_cursor_v1;

typedef enum worr_command_render_provenance_v1_e {
    WORR_COMMAND_RENDER_PROVENANCE_NONE = 0,
    /* The client froze this watermark with this individual command. */
    WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND = 1,
    /* A legacy adapter projected one move-packet watermark onto backups. */
    WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED = 2,
    /* A trusted server-side producer supplied the immutable watermark. */
    WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED = 3,
} worr_command_render_provenance_v1;

enum {
    WORR_COMMAND_RENDER_INTERPOLATED = 1u << 0,
    WORR_COMMAND_RENDER_EXTRAPOLATED = 1u << 1,
};

/*
 * Snapshot-independent render provenance.  The source tick/time identifies
 * the validated authoritative timing sample, while rendered_server_time_us
 * identifies the world time presented when the input was sampled.  Stage 2
 * can correlate these values with canonical snapshots without creating a
 * second snapshot-ID schema or a mutual command/snapshot include.
 */
typedef struct worr_command_render_watermark_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t provenance;
    uint32_t flags;
    uint32_t source_server_tick;
    uint32_t tick_interval_us;
    uint64_t source_server_time_us;
    uint64_t rendered_server_time_us;
} worr_command_render_watermark_v1;

/*
 * Canonical command record.  `command` is the existing T02 prediction payload,
 * not a parallel input schema.  sample_time_us is cumulative command-end time
 * since the current explicit stream reset.  It may remain unchanged for a
 * valid zero-duration command.
 *
 * No packet sequence, packet acknowledgement, netchan state, or retry ordinal
 * is present.  Transport identity is deliberately outside command identity.
 */
typedef struct worr_command_record_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_command_id_v1 command_id;
    uint64_t sample_time_us;
    uint32_t movement_model_revision;
    uint32_t reserved0;
    worr_prediction_command_v1 command;
    worr_command_render_watermark_v1 render_watermark;
} worr_command_record_v1;

bool Worr_CommandDurationLimitValidV1(uint16_t max_duration_ms);
bool Worr_CommandIdValidV1(worr_command_id_v1 command_id,
                           bool allow_absent);
bool Worr_CommandIdNextV1(worr_command_id_v1 current,
                          worr_command_id_v1 *next);
bool Worr_CommandCursorValidV1(worr_command_cursor_v1 cursor);
bool Worr_CommandCursorNextIdV1(worr_command_cursor_v1 cursor,
                                worr_command_id_v1 *next);

/*
 * Return the number of command identities strictly between a consumed or
 * received cursor and a later command.  Arithmetic is constant-time across
 * sequence and epoch rollover.  The destination is untouched unless the
 * command is later than the cursor and the gap is at most maximum_gap.
 */
bool Worr_CommandCursorGapBeforeV1(
    worr_command_cursor_v1 cursor,
    worr_command_id_v1 later_command,
    uint32_t maximum_gap,
    uint32_t *gap_out);

bool Worr_CommandRenderWatermarkValidateV1(
    const worr_command_render_watermark_v1 *watermark);
bool Worr_CommandRecordCanonicalizeV1(worr_command_record_v1 *record,
                                      uint16_t max_duration_ms);
bool Worr_CommandRecordValidateV1(const worr_command_record_v1 *record,
                                  uint16_t max_duration_ms);

/*
 * Semantic hashing/equality are provenance-aware.  Exact-command and trusted
 * synthesized watermarks are immutable semantic input.  A legacy
 * packet-shared watermark's timing context is diagnostic only because a
 * backup may be retransmitted under a different packet header; its provenance
 * class remains semantic.  The first accepted record is therefore retained.
 */
bool Worr_CommandRecordSemanticHashV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    uint64_t *hash_out);
/*
 * Cross-process input identity deliberately excludes render-watermark
 * provenance.  The client retains a command before legacy packet placement
 * is known, while the server later annotates that same input with a
 * packet-shared watermark.  Command ID, cumulative sample time, movement
 * revision, and the canonical prediction command remain exact on both ends.
 */
bool Worr_CommandRecordInputHashV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    uint64_t *hash_out);
bool Worr_CommandRecordContentHashV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    uint64_t *hash_out);
bool Worr_CommandRecordSemanticallyEqualV1(
    const worr_command_record_v1 *a,
    const worr_command_record_v1 *b,
    uint16_t max_duration_ms);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_COMMAND_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_COMMAND_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_COMMAND_STATIC_ASSERT(sizeof(uint8_t) == 1,
                           "command ABI requires 8-bit bytes");
WORR_COMMAND_STATIC_ASSERT(sizeof(float) == 4,
                           "command ABI requires 32-bit floats");
WORR_COMMAND_STATIC_ASSERT(FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
                               FLT_MAX_EXP == 128,
                           "command ABI requires IEEE-754 binary32 floats");
WORR_COMMAND_STATIC_ASSERT(sizeof(worr_command_id_v1) == 8,
                           "command ID v1 layout changed");
WORR_COMMAND_STATIC_ASSERT(sizeof(worr_command_cursor_v1) == 8,
                           "command cursor v1 layout changed");
WORR_COMMAND_STATIC_ASSERT(sizeof(worr_command_render_watermark_v1) == 40,
                           "command render watermark v1 layout changed");
WORR_COMMAND_STATIC_ASSERT(
    offsetof(worr_command_render_watermark_v1, source_server_time_us) == 24,
    "command render watermark source-time offset changed");
WORR_COMMAND_STATIC_ASSERT(
    offsetof(worr_command_render_watermark_v1, rendered_server_time_us) == 32,
    "command render watermark rendered-time offset changed");
WORR_COMMAND_STATIC_ASSERT(sizeof(worr_command_record_v1) == 104,
                           "command record v1 layout changed");
WORR_COMMAND_STATIC_ASSERT(offsetof(worr_command_record_v1, command_id) == 8,
                           "command record ID offset changed");
WORR_COMMAND_STATIC_ASSERT(
    offsetof(worr_command_record_v1, sample_time_us) == 16,
    "command record sample-time offset changed");
WORR_COMMAND_STATIC_ASSERT(offsetof(worr_command_record_v1, command) == 32,
                           "command record prediction payload offset changed");
WORR_COMMAND_STATIC_ASSERT(
    offsetof(worr_command_record_v1, render_watermark) == 64,
    "command record render-watermark offset changed");

#undef WORR_COMMAND_STATIC_ASSERT
