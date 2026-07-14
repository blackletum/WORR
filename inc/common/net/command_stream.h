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

#define WORR_COMMAND_STREAM_VERSION 1u

enum {
    WORR_COMMAND_STREAM_SLOT_OCCUPIED = 1u << 0,
    WORR_COMMAND_STREAM_SLOT_CONSUMED = 1u << 1,
};

typedef struct worr_command_stream_slot_v1_s {
    worr_command_record_v1 record;
    uint64_t semantic_hash;
    uint64_t content_hash;
    uint32_t state;
    uint32_t reserved0;
} worr_command_stream_slot_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_command_stream_telemetry_v1_s {
    uint64_t receive_attempts;
    uint64_t inserted;
    uint64_t duplicates;
    uint64_t conflicts;
    uint64_t stale;
    uint64_t future_gaps;
    uint64_t wrong_epoch;
    uint64_t invalid_records;
    uint64_t capacity_stalls;
    uint64_t sample_time_rejections;
    uint64_t consume_attempts;
    uint64_t consumed;
    uint64_t already_consumed;
    uint64_t not_ready;
    uint64_t reset_attempts;
    uint64_t resets;
    uint64_t reset_rejections;
    uint64_t epoch_wraps;
    uint64_t epoch_exhaustions;
    uint64_t invalid_arguments;
    uint64_t invalid_state;
} worr_command_stream_telemetry_v1;

/*
 * Runtime-only, caller-owned envelope.  It is not serialized or hashed.  The
 * envelope and its slot array must be distinct, non-overlapping objects whose
 * lifetimes cover every operation on the stream.
 */
typedef struct worr_command_stream_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_command_stream_slot_v1 *slots;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    uint16_t max_duration_ms;
    uint16_t reserved0;
    worr_command_cursor_v1 received_cursor;
    worr_command_cursor_v1 consumed_cursor;
    uint64_t last_received_sample_time_us;
    worr_command_stream_telemetry_v1 telemetry;
} worr_command_stream_v1;

typedef enum worr_command_stream_result_v1_e {
    WORR_COMMAND_STREAM_INSERTED = 0,
    WORR_COMMAND_STREAM_DUPLICATE = 1,
    WORR_COMMAND_STREAM_CONSUMED = 2,
    WORR_COMMAND_STREAM_ALREADY_CONSUMED = 3,
    WORR_COMMAND_STREAM_RESET = 4,
    WORR_COMMAND_STREAM_STALE = 5,
    WORR_COMMAND_STREAM_FUTURE_GAP = 6,
    WORR_COMMAND_STREAM_WRONG_EPOCH = 7,
    WORR_COMMAND_STREAM_NOT_READY = 8,
    WORR_COMMAND_STREAM_CAPACITY = 9,
    WORR_COMMAND_STREAM_CONFLICT = 10,
    WORR_COMMAND_STREAM_SAMPLE_TIME_MISMATCH = 11,
    WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW = 12,
    WORR_COMMAND_STREAM_EPOCH_EXHAUSTED = 13,
    WORR_COMMAND_STREAM_INVALID_RECORD = 14,
    WORR_COMMAND_STREAM_INVALID_ARGUMENT = 15,
    WORR_COMMAND_STREAM_INVALID_STATE = 16,
    WORR_COMMAND_STREAM_FAST_FORWARDED = 17,
} worr_command_stream_result_v1;

/*
 * Initialize from a fully consumed baseline.  Empty streams normally use
 * {epoch, 0} and sample time zero; a nonzero baseline supports migration,
 * restore, and deterministic wrap testing without fabricating resident slots.
 * Invalid initialization is transactional for both envelope and storage,
 * including invalid capacities.  Full insertion reclaims only the retained
 * ring head after contiguous consumption; an unconsumed command or interior
 * slot is never evicted.
 */
bool Worr_CommandStreamInitV1(
    worr_command_stream_v1 *stream,
    worr_command_stream_slot_v1 *storage,
    uint32_t capacity,
    uint16_t max_duration_ms,
    worr_command_cursor_v1 consumed_baseline,
    uint64_t baseline_sample_time_us);

bool Worr_CommandStreamValidateV1(const worr_command_stream_v1 *stream);

worr_command_stream_result_v1 Worr_CommandStreamInsertV1(
    worr_command_stream_v1 *stream,
    const worr_command_record_v1 *record);

/*
 * Atomically insert an ordered record range.  Every record is evaluated on a
 * private clone of the stream; INSERTED, DUPLICATE, and STALE members are
 * accepted.  The original envelope, storage, and telemetry are committed only
 * when the complete range succeeds.  On failure they remain byte-identical.
 *
 * `scratch_storage` must hold at least stream->capacity slots.  The stream
 * envelope, its live storage, records, and scratch storage must be pairwise
 * distinct and non-overlapping.  Scratch contents are unspecified on return.
 * The aggregate success result is INSERTED when any member was inserted,
 * DUPLICATE when none were inserted but at least one retained duplicate was
 * observed, and STALE when every member had already left the retained ring.
 */
worr_command_stream_result_v1 Worr_CommandStreamInsertBatchV1(
    worr_command_stream_v1 *stream,
    const worr_command_record_v1 *records,
    uint32_t record_count,
    worr_command_stream_slot_v1 *scratch_storage,
    uint32_t scratch_capacity);

/*
 * Only the exact successor of consumed_cursor can be consumed.  record_out is
 * optional and is untouched on failure.  Received and consumed cursors remain
 * independent so networking receipt never masquerades as simulation progress.
 */
worr_command_stream_result_v1 Worr_CommandStreamConsumeV1(
    worr_command_stream_v1 *stream,
    worr_command_id_v1 command_id,
    worr_command_record_v1 *record_out);

/* Explicit discontinuity.  new_epoch must strictly increase and be nonzero. */
worr_command_stream_result_v1 Worr_CommandStreamResetV1(
    worr_command_stream_v1 *stream,
    uint32_t new_epoch);

/*
 * Advance across an independently authenticated, bounded run of commands
 * without materializing one slot per missing command.  Every received command
 * must already be consumed.  skipped_commands must be nonzero; duration_ms
 * may be zero but may not exceed the stream's negotiated maximum.  The stream
 * derives the destination cursor and sample time itself, including natural
 * command-sequence epoch rollover.  Success clears retained slots and advances
 * both cursors in O(capacity).  Every rejection leaves the complete envelope,
 * storage, and telemetry byte-identical.
 *
 * This is not an epoch reset: reset telemetry retains its existing meaning.
 * Callers that apply transport policy should keep separate fast-forward
 * counters at that policy boundary.
 */
worr_command_stream_result_v1 Worr_CommandStreamFastForwardV1(
    worr_command_stream_v1 *stream,
    uint32_t skipped_commands,
    uint16_t duration_ms);

bool Worr_CommandStreamCopyRecordV1(
    const worr_command_stream_v1 *stream,
    worr_command_id_v1 command_id,
    worr_command_record_v1 *record_out);
bool Worr_CommandStreamPeekNextV1(
    const worr_command_stream_v1 *stream,
    worr_command_record_v1 *record_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_COMMAND_STREAM_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_COMMAND_STREAM_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_COMMAND_STREAM_STATIC_ASSERT(sizeof(worr_command_stream_slot_v1) == 128,
                                  "command stream slot v1 layout changed");
WORR_COMMAND_STREAM_STATIC_ASSERT(
    offsetof(worr_command_stream_slot_v1, semantic_hash) == 104,
    "command stream semantic-hash offset changed");
WORR_COMMAND_STREAM_STATIC_ASSERT(
    offsetof(worr_command_stream_slot_v1, state) == 120,
    "command stream slot-state offset changed");
WORR_COMMAND_STREAM_STATIC_ASSERT(
    sizeof(worr_command_stream_telemetry_v1) == 168,
    "command stream telemetry v1 layout changed");

#undef WORR_COMMAND_STREAM_STATIC_ASSERT
