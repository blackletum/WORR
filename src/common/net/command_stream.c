/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/command_stream.h"

#include <string.h>

static void saturating_increment(uint64_t *counter)
{
    if (*counter != UINT64_MAX)
        ++*counter;
}

/* Validate distinct half-open object ranges without wrapping uintptr_t. */
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

static int id_compare(worr_command_id_v1 a, worr_command_id_v1 b)
{
    if (a.epoch != b.epoch)
        return a.epoch < b.epoch ? -1 : 1;
    if (a.sequence != b.sequence)
        return a.sequence < b.sequence ? -1 : 1;
    return 0;
}

static int cursor_compare(worr_command_cursor_v1 a,
                          worr_command_cursor_v1 b)
{
    worr_command_id_v1 a_id = {a.epoch, a.contiguous_sequence};
    worr_command_id_v1 b_id = {b.epoch, b.contiguous_sequence};
    return id_compare(a_id, b_id);
}

static int id_cursor_compare(worr_command_id_v1 id,
                             worr_command_cursor_v1 cursor)
{
    worr_command_id_v1 cursor_id = {
        cursor.epoch, cursor.contiguous_sequence};
    return id_compare(id, cursor_id);
}

static bool id_equal(worr_command_id_v1 a, worr_command_id_v1 b)
{
    return a.epoch == b.epoch && a.sequence == b.sequence;
}

static uint32_t ring_index(const worr_command_stream_v1 *stream,
                           uint32_t offset)
{
    return (uint32_t)(((uint64_t)stream->head + offset) % stream->capacity);
}

static const worr_command_stream_slot_v1 *find_slot_const(
    const worr_command_stream_v1 *stream,
    worr_command_id_v1 command_id)
{
    uint32_t offset;
    for (offset = 0; offset < stream->count; ++offset) {
        const worr_command_stream_slot_v1 *slot =
            &stream->slots[ring_index(stream, offset)];
        if (id_equal(slot->record.command_id, command_id))
            return slot;
    }
    return NULL;
}

static worr_command_stream_slot_v1 *find_slot(
    worr_command_stream_v1 *stream,
    worr_command_id_v1 command_id)
{
    return (worr_command_stream_slot_v1 *)find_slot_const(stream, command_id);
}

static bool next_sample_time(uint64_t previous,
                             uint8_t duration_ms,
                             uint64_t *next)
{
    const uint64_t increment = (uint64_t)duration_ms * UINT64_C(1000);
    if (previous > UINT64_MAX - increment)
        return false;
    *next = previous + increment;
    return true;
}

bool Worr_CommandStreamInitV1(
    worr_command_stream_v1 *stream,
    worr_command_stream_slot_v1 *storage,
    uint32_t capacity,
    uint16_t max_duration_ms,
    worr_command_cursor_v1 consumed_baseline,
    uint64_t baseline_sample_time_us)
{
    worr_command_stream_v1 output;
    size_t storage_size;

    if (!stream || !storage || capacity == 0 ||
        !Worr_CommandDurationLimitValidV1(max_duration_ms) ||
        !Worr_CommandCursorValidV1(consumed_baseline) ||
        (consumed_baseline.contiguous_sequence == 0 &&
         baseline_sample_time_us != 0)) {
        return false;
    }
    if (!array_size_u32(capacity, sizeof(*storage), &storage_size))
        return false;
    if (!ranges_disjoint(stream, sizeof(*stream),
                         storage, storage_size)) {
        return false;
    }

    memset(&output, 0, sizeof(output));
    output.struct_size = sizeof(output);
    output.schema_version = WORR_COMMAND_STREAM_VERSION;
    output.slots = storage;
    output.capacity = capacity;
    output.max_duration_ms = max_duration_ms;
    output.received_cursor = consumed_baseline;
    output.consumed_cursor = consumed_baseline;
    output.last_received_sample_time_us = baseline_sample_time_us;

    memset(storage, 0, storage_size);
    *stream = output;
    return true;
}

bool Worr_CommandStreamValidateV1(const worr_command_stream_v1 *stream)
{
    worr_command_id_v1 previous_id = {0, 0};
    worr_command_id_v1 next_consumed = {0, 0};
    uint64_t previous_sample = 0;
    bool have_previous = false;
    bool found_next_consumed = false;
    uint32_t offset;
    size_t storage_size;

    if (!stream || stream->struct_size != sizeof(*stream) ||
        stream->schema_version != WORR_COMMAND_STREAM_VERSION ||
        !stream->slots || stream->capacity == 0 ||
        stream->count > stream->capacity || stream->reserved0 != 0 ||
        !Worr_CommandDurationLimitValidV1(stream->max_duration_ms) ||
        !Worr_CommandCursorValidV1(stream->received_cursor) ||
        !Worr_CommandCursorValidV1(stream->consumed_cursor) ||
        cursor_compare(stream->consumed_cursor,
                       stream->received_cursor) > 0) {
        return false;
    }
    if (!array_size_u32(stream->capacity, sizeof(*stream->slots),
                        &storage_size)) {
        return false;
    }
    if (!ranges_disjoint(stream, sizeof(*stream),
                         stream->slots, storage_size)) {
        return false;
    }

    if (stream->count == 0) {
        return stream->head == 0 &&
               cursor_compare(stream->consumed_cursor,
                              stream->received_cursor) == 0 &&
               (stream->received_cursor.contiguous_sequence != 0 ||
                stream->last_received_sample_time_us == 0);
    }
    if (stream->head >= stream->capacity)
        return false;

    if (cursor_compare(stream->consumed_cursor,
                       stream->received_cursor) < 0 &&
        !Worr_CommandCursorNextIdV1(stream->consumed_cursor,
                                    &next_consumed)) {
        return false;
    }

    for (offset = 0; offset < stream->count; ++offset) {
        const worr_command_stream_slot_v1 *slot =
            &stream->slots[ring_index(stream, offset)];
        uint64_t semantic_hash;
        uint64_t content_hash;
        uint64_t expected_sample;
        int consumed_relation;

        if (slot->state != WORR_COMMAND_STREAM_SLOT_OCCUPIED &&
            slot->state != (WORR_COMMAND_STREAM_SLOT_OCCUPIED |
                            WORR_COMMAND_STREAM_SLOT_CONSUMED)) {
            return false;
        }
        if (slot->reserved0 != 0 ||
            !Worr_CommandRecordSemanticHashV1(
                &slot->record, stream->max_duration_ms, &semantic_hash) ||
            !Worr_CommandRecordContentHashV1(
                &slot->record, stream->max_duration_ms, &content_hash) ||
            slot->semantic_hash != semantic_hash ||
            slot->content_hash != content_hash) {
            return false;
        }

        if (have_previous) {
            worr_command_id_v1 expected_id;
            if (!Worr_CommandIdNextV1(previous_id, &expected_id) ||
                !id_equal(expected_id, slot->record.command_id) ||
                !next_sample_time(previous_sample,
                                  slot->record.command.duration_ms,
                                  &expected_sample) ||
                expected_sample != slot->record.sample_time_us) {
                return false;
            }
        }

        consumed_relation =
            id_cursor_compare(slot->record.command_id,
                              stream->consumed_cursor);
        if (consumed_relation <= 0) {
            if ((slot->state & WORR_COMMAND_STREAM_SLOT_CONSUMED) == 0)
                return false;
        } else {
            if ((slot->state & WORR_COMMAND_STREAM_SLOT_CONSUMED) != 0)
                return false;
            if (!found_next_consumed) {
                if (!id_equal(slot->record.command_id, next_consumed))
                    return false;
                found_next_consumed = true;
            }
        }

        previous_id = slot->record.command_id;
        previous_sample = slot->record.sample_time_us;
        have_previous = true;
    }

    if (previous_id.epoch != stream->received_cursor.epoch ||
        previous_id.sequence !=
            stream->received_cursor.contiguous_sequence ||
        previous_sample != stream->last_received_sample_time_us) {
        return false;
    }
    return cursor_compare(stream->consumed_cursor,
                          stream->received_cursor) == 0 ||
           found_next_consumed;
}

worr_command_stream_result_v1 Worr_CommandStreamInsertV1(
    worr_command_stream_v1 *stream,
    const worr_command_record_v1 *record)
{
    worr_command_stream_slot_v1 new_slot;
    worr_command_id_v1 expected_id;
    const worr_command_stream_slot_v1 *duplicate_slot;
    uint64_t semantic_hash;
    uint64_t content_hash;
    uint64_t expected_sample;
    uint32_t target;
    int order;
    int received_order;
    bool reclaim;

    if (!stream)
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    if (!Worr_CommandStreamValidateV1(stream))
        return WORR_COMMAND_STREAM_INVALID_STATE;

    saturating_increment(&stream->telemetry.receive_attempts);
    if (!record) {
        saturating_increment(&stream->telemetry.invalid_arguments);
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    }
    if (!Worr_CommandRecordSemanticHashV1(
            record, stream->max_duration_ms, &semantic_hash) ||
        !Worr_CommandRecordContentHashV1(
            record, stream->max_duration_ms, &content_hash)) {
        saturating_increment(&stream->telemetry.invalid_records);
        return WORR_COMMAND_STREAM_INVALID_RECORD;
    }
    received_order =
        id_cursor_compare(record->command_id, stream->received_cursor);
    if (received_order <= 0) {
        duplicate_slot = find_slot_const(stream, record->command_id);
        if (!duplicate_slot) {
            if (received_order == 0 &&
                stream->received_cursor.epoch == UINT32_MAX &&
                stream->received_cursor.contiguous_sequence == UINT32_MAX) {
                saturating_increment(&stream->telemetry.epoch_exhaustions);
                return WORR_COMMAND_STREAM_EPOCH_EXHAUSTED;
            }
            saturating_increment(&stream->telemetry.stale);
            return WORR_COMMAND_STREAM_STALE;
        }
        if (duplicate_slot->semantic_hash != semantic_hash ||
            !Worr_CommandRecordSemanticallyEqualV1(
                &duplicate_slot->record, record,
                stream->max_duration_ms)) {
            saturating_increment(&stream->telemetry.conflicts);
            return WORR_COMMAND_STREAM_CONFLICT;
        }
        saturating_increment(&stream->telemetry.duplicates);
        return WORR_COMMAND_STREAM_DUPLICATE;
    }

    if (!Worr_CommandCursorNextIdV1(stream->received_cursor, &expected_id)) {
        saturating_increment(&stream->telemetry.epoch_exhaustions);
        return WORR_COMMAND_STREAM_EPOCH_EXHAUSTED;
    }
    order = id_compare(record->command_id, expected_id);
    if (order > 0) {
        if (record->command_id.epoch == expected_id.epoch) {
            saturating_increment(&stream->telemetry.future_gaps);
            return WORR_COMMAND_STREAM_FUTURE_GAP;
        }
        saturating_increment(&stream->telemetry.wrong_epoch);
        return WORR_COMMAND_STREAM_WRONG_EPOCH;
    }

    if (!next_sample_time(stream->last_received_sample_time_us,
                          record->command.duration_ms,
                          &expected_sample)) {
        saturating_increment(&stream->telemetry.sample_time_rejections);
        return WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW;
    }
    if (record->sample_time_us != expected_sample) {
        saturating_increment(&stream->telemetry.sample_time_rejections);
        return WORR_COMMAND_STREAM_SAMPLE_TIME_MISMATCH;
    }

    reclaim = stream->count == stream->capacity;
    if (reclaim &&
        (stream->slots[stream->head].state &
         WORR_COMMAND_STREAM_SLOT_CONSUMED) == 0) {
        saturating_increment(&stream->telemetry.capacity_stalls);
        return WORR_COMMAND_STREAM_CAPACITY;
    }
    target = reclaim ? stream->head : ring_index(stream, stream->count);

    memset(&new_slot, 0, sizeof(new_slot));
    new_slot.record = *record;
    new_slot.semantic_hash = semantic_hash;
    new_slot.content_hash = content_hash;
    new_slot.state = WORR_COMMAND_STREAM_SLOT_OCCUPIED;

    stream->slots[target] = new_slot;
    if (reclaim) {
        stream->head = (stream->head + 1u == stream->capacity)
                           ? 0u
                           : stream->head + 1u;
    } else {
        ++stream->count;
    }
    if (stream->received_cursor.epoch != record->command_id.epoch)
        saturating_increment(&stream->telemetry.epoch_wraps);
    stream->received_cursor.epoch = record->command_id.epoch;
    stream->received_cursor.contiguous_sequence =
        record->command_id.sequence;
    stream->last_received_sample_time_us = record->sample_time_us;
    saturating_increment(&stream->telemetry.inserted);
    return WORR_COMMAND_STREAM_INSERTED;
}

worr_command_stream_result_v1 Worr_CommandStreamInsertBatchV1(
    worr_command_stream_v1 *stream,
    const worr_command_record_v1 *records,
    uint32_t record_count,
    worr_command_stream_slot_v1 *scratch_storage,
    uint32_t scratch_capacity)
{
    worr_command_stream_v1 transaction;
    worr_command_stream_result_v1 result;
    bool inserted = false;
    bool duplicate = false;
    size_t live_size;
    size_t record_size;
    size_t scratch_size;
    uint32_t i;

    if (!stream || !records || record_count == 0 || !scratch_storage)
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    if (!Worr_CommandStreamValidateV1(stream))
        return WORR_COMMAND_STREAM_INVALID_STATE;
    if (scratch_capacity < stream->capacity) {
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    }

    if (!array_size_u32(stream->capacity, sizeof(*stream->slots),
                        &live_size) ||
        !array_size_u32(record_count, sizeof(*records), &record_size) ||
        !array_size_u32(scratch_capacity, sizeof(*scratch_storage),
                        &scratch_size)) {
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    }
    if (!ranges_disjoint(stream, sizeof(*stream), records, record_size) ||
        !ranges_disjoint(stream, sizeof(*stream), scratch_storage,
                         scratch_size) ||
        !ranges_disjoint(stream->slots, live_size, records, record_size) ||
        !ranges_disjoint(stream->slots, live_size, scratch_storage,
                         scratch_size) ||
        !ranges_disjoint(records, record_size, scratch_storage,
                         scratch_size)) {
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    }

    memcpy(scratch_storage, stream->slots, live_size);
    transaction = *stream;
    transaction.slots = scratch_storage;
    if (!Worr_CommandStreamValidateV1(&transaction))
        return WORR_COMMAND_STREAM_INVALID_STATE;

    for (i = 0; i < record_count; ++i) {
        result = Worr_CommandStreamInsertV1(&transaction, &records[i]);
        if (result == WORR_COMMAND_STREAM_INSERTED) {
            inserted = true;
        } else if (result == WORR_COMMAND_STREAM_DUPLICATE) {
            duplicate = true;
        } else if (result != WORR_COMMAND_STREAM_STALE) {
            return result;
        }
    }

    memcpy(stream->slots, scratch_storage, live_size);
    transaction.slots = stream->slots;
    *stream = transaction;
    if (inserted)
        return WORR_COMMAND_STREAM_INSERTED;
    if (duplicate)
        return WORR_COMMAND_STREAM_DUPLICATE;
    return WORR_COMMAND_STREAM_STALE;
}

worr_command_stream_result_v1 Worr_CommandStreamConsumeV1(
    worr_command_stream_v1 *stream,
    worr_command_id_v1 command_id,
    worr_command_record_v1 *record_out)
{
    worr_command_stream_slot_v1 *slot;
    worr_command_record_v1 output;
    worr_command_id_v1 expected_id;
    int order;

    if (!stream)
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    if (!Worr_CommandStreamValidateV1(stream))
        return WORR_COMMAND_STREAM_INVALID_STATE;

    saturating_increment(&stream->telemetry.consume_attempts);
    if (!Worr_CommandIdValidV1(command_id, false)) {
        saturating_increment(&stream->telemetry.invalid_arguments);
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    }

    if (id_cursor_compare(command_id, stream->consumed_cursor) <= 0) {
        saturating_increment(&stream->telemetry.already_consumed);
        return WORR_COMMAND_STREAM_ALREADY_CONSUMED;
    }
    if (!Worr_CommandCursorNextIdV1(stream->consumed_cursor,
                                    &expected_id)) {
        saturating_increment(&stream->telemetry.epoch_exhaustions);
        return WORR_COMMAND_STREAM_EPOCH_EXHAUSTED;
    }

    order = id_compare(command_id, expected_id);
    if (order != 0) {
        if (command_id.epoch != expected_id.epoch) {
            saturating_increment(&stream->telemetry.wrong_epoch);
            return WORR_COMMAND_STREAM_WRONG_EPOCH;
        }
        saturating_increment(&stream->telemetry.not_ready);
        return WORR_COMMAND_STREAM_NOT_READY;
    }
    if (id_cursor_compare(command_id, stream->received_cursor) > 0) {
        saturating_increment(&stream->telemetry.not_ready);
        return WORR_COMMAND_STREAM_NOT_READY;
    }

    slot = find_slot(stream, command_id);
    if (!slot ||
        (slot->state & WORR_COMMAND_STREAM_SLOT_CONSUMED) != 0) {
        saturating_increment(&stream->telemetry.invalid_state);
        return WORR_COMMAND_STREAM_INVALID_STATE;
    }
    output = slot->record;
    slot->state |= WORR_COMMAND_STREAM_SLOT_CONSUMED;
    stream->consumed_cursor.epoch = command_id.epoch;
    stream->consumed_cursor.contiguous_sequence = command_id.sequence;
    saturating_increment(&stream->telemetry.consumed);
    if (record_out)
        *record_out = output;
    return WORR_COMMAND_STREAM_CONSUMED;
}

worr_command_stream_result_v1 Worr_CommandStreamResetV1(
    worr_command_stream_v1 *stream,
    uint32_t new_epoch)
{
    if (!stream)
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    if (!Worr_CommandStreamValidateV1(stream))
        return WORR_COMMAND_STREAM_INVALID_STATE;

    saturating_increment(&stream->telemetry.reset_attempts);
    if (new_epoch == 0) {
        saturating_increment(&stream->telemetry.invalid_arguments);
        saturating_increment(&stream->telemetry.reset_rejections);
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    }
    if (new_epoch <= stream->received_cursor.epoch) {
        saturating_increment(&stream->telemetry.reset_rejections);
        if (stream->received_cursor.epoch == UINT32_MAX) {
            saturating_increment(&stream->telemetry.epoch_exhaustions);
            return WORR_COMMAND_STREAM_EPOCH_EXHAUSTED;
        }
        saturating_increment(&stream->telemetry.wrong_epoch);
        return WORR_COMMAND_STREAM_WRONG_EPOCH;
    }

    memset(stream->slots, 0, sizeof(*stream->slots) * stream->capacity);
    stream->head = 0;
    stream->count = 0;
    stream->received_cursor.epoch = new_epoch;
    stream->received_cursor.contiguous_sequence = 0;
    stream->consumed_cursor = stream->received_cursor;
    stream->last_received_sample_time_us = 0;
    saturating_increment(&stream->telemetry.resets);
    return WORR_COMMAND_STREAM_RESET;
}

worr_command_stream_result_v1 Worr_CommandStreamFastForwardV1(
    worr_command_stream_v1 *stream,
    uint32_t skipped_commands,
    uint16_t duration_ms)
{
    worr_command_cursor_v1 new_baseline;
    uint64_t sequence;
    uint64_t duration_us;
    uint64_t skipped_time_us;
    uint64_t baseline_sample_time_us;
    bool wrapped = false;

    if (!stream)
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    if (!Worr_CommandStreamValidateV1(stream))
        return WORR_COMMAND_STREAM_INVALID_STATE;
    if (skipped_commands == 0 || duration_ms > stream->max_duration_ms)
        return WORR_COMMAND_STREAM_INVALID_ARGUMENT;
    if (cursor_compare(stream->consumed_cursor,
                       stream->received_cursor) != 0) {
        return WORR_COMMAND_STREAM_NOT_READY;
    }

    new_baseline = stream->received_cursor;
    sequence = (uint64_t)new_baseline.contiguous_sequence +
               skipped_commands;
    if (sequence > UINT32_MAX) {
        if (new_baseline.epoch == UINT32_MAX)
            return WORR_COMMAND_STREAM_EPOCH_EXHAUSTED;
        ++new_baseline.epoch;
        sequence -= UINT32_MAX;
        wrapped = true;
    }
    new_baseline.contiguous_sequence = (uint32_t)sequence;

    duration_us = (uint64_t)duration_ms * UINT64_C(1000);
    if (duration_us != 0 &&
        skipped_commands > UINT64_MAX / duration_us) {
        return WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW;
    }
    skipped_time_us = duration_us * skipped_commands;
    if (stream->last_received_sample_time_us >
        UINT64_MAX - skipped_time_us) {
        return WORR_COMMAND_STREAM_SAMPLE_TIME_OVERFLOW;
    }
    baseline_sample_time_us =
        stream->last_received_sample_time_us + skipped_time_us;

    memset(stream->slots, 0, sizeof(*stream->slots) * stream->capacity);
    stream->head = 0;
    stream->count = 0;
    stream->received_cursor = new_baseline;
    stream->consumed_cursor = new_baseline;
    stream->last_received_sample_time_us = baseline_sample_time_us;
    if (wrapped)
        saturating_increment(&stream->telemetry.epoch_wraps);
    return WORR_COMMAND_STREAM_FAST_FORWARDED;
}

bool Worr_CommandStreamCopyRecordV1(
    const worr_command_stream_v1 *stream,
    worr_command_id_v1 command_id,
    worr_command_record_v1 *record_out)
{
    const worr_command_stream_slot_v1 *slot;
    if (!record_out || !Worr_CommandIdValidV1(command_id, false) ||
        !Worr_CommandStreamValidateV1(stream)) {
        return false;
    }
    slot = find_slot_const(stream, command_id);
    if (!slot)
        return false;
    *record_out = slot->record;
    return true;
}

bool Worr_CommandStreamPeekNextV1(
    const worr_command_stream_v1 *stream,
    worr_command_record_v1 *record_out)
{
    worr_command_id_v1 next_id;
    const worr_command_stream_slot_v1 *slot;
    if (!record_out || !Worr_CommandStreamValidateV1(stream) ||
        !Worr_CommandCursorNextIdV1(stream->consumed_cursor, &next_id) ||
        id_cursor_compare(next_id, stream->received_cursor) > 0) {
        return false;
    }
    slot = find_slot_const(stream, next_id);
    if (!slot)
        return false;
    *record_out = slot->record;
    return true;
}
