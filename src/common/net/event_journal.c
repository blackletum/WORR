/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/event_journal.h"

#include <string.h>

static worr_event_slot_ref_v1 invalid_slot_ref(void)
{
    worr_event_slot_ref_v1 ref = {WORR_EVENT_SLOT_INVALID, 0};
    return ref;
}

static void set_slot_out(worr_event_slot_ref_v1 *slot_out,
                         worr_event_slot_ref_v1 value)
{
    if (slot_out)
        *slot_out = value;
}

static bool journal_valid(const worr_event_journal_v1 *journal)
{
    return journal && journal->struct_size == sizeof(*journal) &&
           journal->schema_version == WORR_EVENT_JOURNAL_VERSION &&
           journal->slots && journal->capacity != 0 &&
           journal->max_entities != 0 && journal->stream_epoch != 0 &&
           journal->occupied <= journal->capacity &&
           journal->receipt.struct_size == sizeof(journal->receipt) &&
           journal->receipt.schema_version == WORR_EVENT_ABI_VERSION &&
           journal->receipt.stream_epoch == journal->stream_epoch;
}

static bool slot_used(const worr_event_journal_slot_v1 *slot)
{
    return slot->state != 0;
}

static bool slot_terminal(const worr_event_journal_slot_v1 *slot)
{
    return (slot->state & (WORR_EVENT_SLOT_PRESENTED |
                           WORR_EVENT_SLOT_EXPIRED |
                           WORR_EVENT_SLOT_CANCELED)) != 0;
}

static bool slot_reclaimable(const worr_event_journal_slot_v1 *slot)
{
    if ((slot->state & (WORR_EVENT_SLOT_EXPIRED |
                        WORR_EVENT_SLOT_CANCELED)) != 0) {
        return true;
    }
    return (slot->state & WORR_EVENT_SLOT_PRESENTED) != 0 &&
           (slot->state & WORR_EVENT_SLOT_RECEIVED) != 0 &&
           slot->record.delivery_class !=
               WORR_EVENT_DELIVERY_PERSISTENT_STATE;
}

static bool ref_equal(worr_event_entity_ref_v1 a,
                      worr_event_entity_ref_v1 b)
{
    return a.index == b.index && a.generation == b.generation;
}

static bool prediction_key_equal(worr_event_prediction_key_v1 a,
                                 worr_event_prediction_key_v1 b)
{
    return a.command_epoch == b.command_epoch &&
           a.command_sequence == b.command_sequence &&
           a.emitter_ordinal == b.emitter_ordinal && a.lane == b.lane;
}

static bool id_equal(worr_event_id_v1 a, worr_event_id_v1 b)
{
    return a.stream_epoch == b.stream_epoch && a.sequence == b.sequence;
}

static bool records_semantically_equal(const worr_event_record_v1 *a,
                                       const worr_event_record_v1 *b)
{
    return a->model_revision == b->model_revision &&
           (a->flags & ~(uint32_t)WORR_EVENT_FLAG_HAS_AUTHORITY_ID) ==
               (b->flags & ~(uint32_t)WORR_EVENT_FLAG_HAS_AUTHORITY_ID) &&
           a->source_tick == b->source_tick &&
           a->source_ordinal == b->source_ordinal &&
           a->source_time_us == b->source_time_us &&
           ref_equal(a->source_entity, b->source_entity) &&
           ref_equal(a->subject_entity, b->subject_entity) &&
           a->event_type == b->event_type &&
           a->delivery_class == b->delivery_class &&
           a->prediction_class == b->prediction_class &&
           prediction_key_equal(a->prediction_key, b->prediction_key) &&
           a->expiry_tick == b->expiry_tick &&
           a->payload_kind == b->payload_kind &&
           a->payload_size == b->payload_size &&
           memcmp(a->payload, b->payload, WORR_EVENT_PAYLOAD_CAPACITY) == 0;
}

static bool records_equal(const worr_event_record_v1 *a,
                          const worr_event_record_v1 *b)
{
    return a->flags == b->flags && id_equal(a->event_id, b->event_id) &&
           records_semantically_equal(a, b);
}

static bool persistent_key_equal(const worr_event_record_v1 *a,
                                 const worr_event_record_v1 *b)
{
    return a->delivery_class == WORR_EVENT_DELIVERY_PERSISTENT_STATE &&
           b->delivery_class == WORR_EVENT_DELIVERY_PERSISTENT_STATE &&
           ref_equal(a->source_entity, b->source_entity) &&
           ref_equal(a->subject_entity, b->subject_entity) &&
           a->event_type == b->event_type &&
           a->payload_kind == b->payload_kind;
}

static bool cosmetic_coalesce_key_equal(const worr_event_record_v1 *a,
                                        const worr_event_record_v1 *b)
{
    return a->delivery_class == WORR_EVENT_DELIVERY_COSMETIC &&
           b->delivery_class == WORR_EVENT_DELIVERY_COSMETIC &&
           ref_equal(a->source_entity, b->source_entity) &&
           ref_equal(a->subject_entity, b->subject_entity) &&
           a->event_type == b->event_type &&
           a->payload_kind == b->payload_kind &&
           a->prediction_key.lane == b->prediction_key.lane;
}

static worr_event_slot_ref_v1 slot_ref(const worr_event_journal_v1 *journal,
                                       uint32_t index)
{
    worr_event_slot_ref_v1 ref;
    (void)journal;
    ref.index = index;
    ref.generation = journal->slots[index].generation;
    return ref;
}

static void assign_slot(worr_event_journal_v1 *journal,
                        uint32_t index,
                        const worr_event_record_v1 *record,
                        uint32_t state)
{
    worr_event_journal_slot_v1 *slot = &journal->slots[index];
    uint32_t generation = slot->generation + 1;
    if (generation == 0)
        generation = 1;
    if (!slot_used(slot))
        ++journal->occupied;
    slot->record = *record;
    slot->generation = generation;
    slot->state = state;
    ++journal->next_resident_order;
    if (journal->next_resident_order == 0)
        ++journal->next_resident_order;
    slot->resident_order = journal->next_resident_order;
}

static uint32_t find_free_or_terminal(const worr_event_journal_v1 *journal)
{
    uint32_t index;
    uint32_t oldest = WORR_EVENT_SLOT_INVALID;
    uint64_t oldest_order = UINT64_MAX;

    for (index = 0; index < journal->capacity; ++index) {
        const worr_event_journal_slot_v1 *slot = &journal->slots[index];
        if (!slot_used(slot))
            return index;
        if (slot_reclaimable(slot) && slot->resident_order < oldest_order) {
            oldest = index;
            oldest_order = slot->resident_order;
        }
    }
    return oldest;
}

static uint32_t find_cosmetic_coalesce(
    const worr_event_journal_v1 *journal,
    const worr_event_record_v1 *record)
{
    uint32_t index;
    uint32_t oldest = WORR_EVENT_SLOT_INVALID;
    uint64_t oldest_order = UINT64_MAX;
    for (index = 0; index < journal->capacity; ++index) {
        const worr_event_journal_slot_v1 *slot = &journal->slots[index];
        if (slot_used(slot) && !slot_terminal(slot) &&
            cosmetic_coalesce_key_equal(&slot->record, record) &&
            slot->resident_order < oldest_order) {
            oldest = index;
            oldest_order = slot->resident_order;
        }
    }
    return oldest;
}

static uint32_t find_persistent_key(const worr_event_journal_v1 *journal,
                                    const worr_event_record_v1 *record)
{
    uint32_t index;
    for (index = 0; index < journal->capacity; ++index) {
        const worr_event_journal_slot_v1 *slot = &journal->slots[index];
        if (slot_used(slot) &&
            persistent_key_equal(&slot->record, record)) {
            return index;
        }
    }
    return WORR_EVENT_SLOT_INVALID;
}

static uint32_t find_prediction_key(const worr_event_journal_v1 *journal,
                                    const worr_event_record_v1 *record)
{
    uint32_t index;
    if (record->prediction_class ==
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY) {
        return WORR_EVENT_SLOT_INVALID;
    }
    for (index = 0; index < journal->capacity; ++index) {
        const worr_event_journal_slot_v1 *slot = &journal->slots[index];
        if (slot_used(slot) &&
            slot->record.prediction_class !=
                WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY &&
            prediction_key_equal(slot->record.prediction_key,
                                 record->prediction_key)) {
            return index;
        }
    }
    return WORR_EVENT_SLOT_INVALID;
}

bool Worr_EventJournalInitV1(worr_event_journal_v1 *journal,
                             worr_event_journal_slot_v1 *storage,
                             uint32_t capacity,
                             uint32_t max_entities,
                             uint32_t stream_epoch)
{
    if (!journal || !storage || capacity == 0 || max_entities == 0 ||
        stream_epoch == 0 ||
        (size_t)capacity > SIZE_MAX / sizeof(*storage))
        return false;
    memset(journal, 0, sizeof(*journal));
    memset(storage, 0, sizeof(*storage) * capacity);
    journal->struct_size = sizeof(*journal);
    journal->schema_version = WORR_EVENT_JOURNAL_VERSION;
    journal->slots = storage;
    journal->capacity = capacity;
    journal->max_entities = max_entities;
    journal->stream_epoch = stream_epoch;
    return Worr_EventReceiptInitV1(&journal->receipt, stream_epoch);
}

bool Worr_EventJournalAdvanceEpochV1(worr_event_journal_v1 *journal,
                                     uint32_t stream_epoch)
{
    uint32_t index;
    if (!journal_valid(journal) || stream_epoch <= journal->stream_epoch)
        return false;
    for (index = 0; index < journal->capacity; ++index) {
        memset(&journal->slots[index].record, 0,
               sizeof(journal->slots[index].record));
        journal->slots[index].resident_order = 0;
        journal->slots[index].state = 0;
    }
    journal->occupied = 0;
    journal->stream_epoch = stream_epoch;
    journal->next_resident_order = 0;
    return Worr_EventReceiptInitV1(&journal->receipt, stream_epoch);
}

worr_event_journal_result_v1 Worr_EventJournalInsertAuthoritativeV1(
    worr_event_journal_v1 *journal,
    const worr_event_record_v1 *record,
    worr_event_slot_ref_v1 *slot_out)
{
    worr_event_receipt_ack_v1 receipt_after;
    worr_event_receipt_result_v1 receipt_result;
    uint32_t index;
    uint32_t replacement;

    set_slot_out(slot_out, invalid_slot_ref());
    if (!journal_valid(journal) || !record)
        return WORR_EVENT_JOURNAL_INVALID_ARGUMENT;
    if (!Worr_EventRecordValidateV1(record, journal->max_entities) ||
        (record->flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0) {
        return WORR_EVENT_JOURNAL_INVALID_RECORD;
    }
    if (record->event_id.stream_epoch != journal->stream_epoch)
        return WORR_EVENT_JOURNAL_WRONG_EPOCH;

    for (index = 0; index < journal->capacity; ++index) {
        worr_event_journal_slot_v1 *slot = &journal->slots[index];
        if (!slot_used(slot) ||
            (slot->record.flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0 ||
            !id_equal(slot->record.event_id, record->event_id)) {
            continue;
        }
        set_slot_out(slot_out, slot_ref(journal, index));
        return records_equal(&slot->record, record)
                   ? WORR_EVENT_JOURNAL_DUPLICATE
                   : WORR_EVENT_JOURNAL_CONFLICT;
    }

    if (Worr_EventReceiptContainsV1(&journal->receipt, record->event_id))
        return WORR_EVENT_JOURNAL_DUPLICATE;
    receipt_after = journal->receipt;
    receipt_result = Worr_EventReceiptMarkV1(&receipt_after, record->event_id);
    if (receipt_result == WORR_EVENT_RECEIPT_OUTSIDE_WINDOW)
        return WORR_EVENT_JOURNAL_ACK_WINDOW;
    if (receipt_result != WORR_EVENT_RECEIPT_ACCEPTED)
        return WORR_EVENT_JOURNAL_INVALID_RECORD;

    replacement = find_prediction_key(journal, record);
    if (replacement != WORR_EVENT_SLOT_INVALID) {
        worr_event_journal_slot_v1 *slot = &journal->slots[replacement];
        if ((slot->state & WORR_EVENT_SLOT_RECEIVED) != 0) {
            set_slot_out(slot_out, slot_ref(journal, replacement));
            return WORR_EVENT_JOURNAL_CONFLICT;
        }
        if (!records_semantically_equal(&slot->record, record)) {
            set_slot_out(slot_out, slot_ref(journal, replacement));
            return WORR_EVENT_JOURNAL_CONFLICT;
        }
        slot->record = *record;
        slot->state |= WORR_EVENT_SLOT_RECEIVED | WORR_EVENT_SLOT_MATCHED |
                       WORR_EVENT_SLOT_PREDICTED;
        journal->receipt = receipt_after;
        set_slot_out(slot_out, slot_ref(journal, replacement));
        return WORR_EVENT_JOURNAL_MATCHED;
    }

    if (record->delivery_class == WORR_EVENT_DELIVERY_PERSISTENT_STATE) {
        replacement = find_persistent_key(journal, record);
        if (replacement != WORR_EVENT_SLOT_INVALID) {
            worr_event_journal_slot_v1 *slot = &journal->slots[replacement];
            if ((slot->record.flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) != 0 &&
                slot->record.event_id.stream_epoch ==
                    record->event_id.stream_epoch &&
                slot->record.event_id.sequence > record->event_id.sequence) {
                journal->receipt = receipt_after;
                set_slot_out(slot_out, slot_ref(journal, replacement));
                return WORR_EVENT_JOURNAL_DROPPED_STALE;
            }
            assign_slot(journal, replacement, record,
                        WORR_EVENT_SLOT_RECEIVED);
            journal->receipt = receipt_after;
            set_slot_out(slot_out, slot_ref(journal, replacement));
            return WORR_EVENT_JOURNAL_SUPERSEDED;
        }
    }

    replacement = find_free_or_terminal(journal);
    if (replacement == WORR_EVENT_SLOT_INVALID &&
        record->delivery_class == WORR_EVENT_DELIVERY_COSMETIC) {
        replacement = find_cosmetic_coalesce(journal, record);
        if (replacement != WORR_EVENT_SLOT_INVALID) {
            assign_slot(journal, replacement, record,
                        WORR_EVENT_SLOT_RECEIVED);
            journal->receipt = receipt_after;
            set_slot_out(slot_out, slot_ref(journal, replacement));
            return WORR_EVENT_JOURNAL_COALESCED;
        }
    }
    if (replacement == WORR_EVENT_SLOT_INVALID) {
        return record->delivery_class >=
                       WORR_EVENT_DELIVERY_RELIABLE_ORDERED
                   ? WORR_EVENT_JOURNAL_CAPACITY_FATAL
                   : WORR_EVENT_JOURNAL_DROPPED_OVERFLOW;
    }

    assign_slot(journal, replacement, record, WORR_EVENT_SLOT_RECEIVED);
    journal->receipt = receipt_after;
    set_slot_out(slot_out, slot_ref(journal, replacement));
    return WORR_EVENT_JOURNAL_INSERTED;
}

worr_event_journal_result_v1 Worr_EventJournalInsertPredictedV1(
    worr_event_journal_v1 *journal,
    const worr_event_record_v1 *record,
    worr_event_slot_ref_v1 *slot_out)
{
    uint32_t replacement;

    set_slot_out(slot_out, invalid_slot_ref());
    if (!journal_valid(journal) || !record)
        return WORR_EVENT_JOURNAL_INVALID_ARGUMENT;
    if (!Worr_EventRecordValidateV1(record, journal->max_entities) ||
        (record->flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) != 0 ||
        record->prediction_class ==
            WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY) {
        return WORR_EVENT_JOURNAL_INVALID_RECORD;
    }

    replacement = find_prediction_key(journal, record);
    if (replacement != WORR_EVENT_SLOT_INVALID) {
        worr_event_journal_slot_v1 *slot = &journal->slots[replacement];
        set_slot_out(slot_out, slot_ref(journal, replacement));
        if (!records_semantically_equal(&slot->record, record))
            return WORR_EVENT_JOURNAL_CONFLICT;
        if ((slot->state & WORR_EVENT_SLOT_RECEIVED) != 0) {
            slot->state |= WORR_EVENT_SLOT_PREDICTED |
                           WORR_EVENT_SLOT_MATCHED;
            return WORR_EVENT_JOURNAL_MATCHED;
        }
        return WORR_EVENT_JOURNAL_DUPLICATE;
    }

    if (record->delivery_class == WORR_EVENT_DELIVERY_PERSISTENT_STATE) {
        replacement = find_persistent_key(journal, record);
        if (replacement != WORR_EVENT_SLOT_INVALID) {
            if ((journal->slots[replacement].state &
                 WORR_EVENT_SLOT_RECEIVED) != 0) {
                set_slot_out(slot_out, slot_ref(journal, replacement));
                return WORR_EVENT_JOURNAL_CONFLICT;
            }
            assign_slot(journal, replacement, record,
                        WORR_EVENT_SLOT_PREDICTED);
            set_slot_out(slot_out, slot_ref(journal, replacement));
            return WORR_EVENT_JOURNAL_SUPERSEDED;
        }
    }

    replacement = find_free_or_terminal(journal);
    if (replacement == WORR_EVENT_SLOT_INVALID &&
        record->delivery_class == WORR_EVENT_DELIVERY_COSMETIC) {
        replacement = find_cosmetic_coalesce(journal, record);
        if (replacement != WORR_EVENT_SLOT_INVALID) {
            assign_slot(journal, replacement, record,
                        WORR_EVENT_SLOT_PREDICTED);
            set_slot_out(slot_out, slot_ref(journal, replacement));
            return WORR_EVENT_JOURNAL_COALESCED;
        }
    }
    if (replacement == WORR_EVENT_SLOT_INVALID) {
        return record->delivery_class >=
                       WORR_EVENT_DELIVERY_RELIABLE_ORDERED
                   ? WORR_EVENT_JOURNAL_CAPACITY_FATAL
                   : WORR_EVENT_JOURNAL_DROPPED_OVERFLOW;
    }

    assign_slot(journal, replacement, record, WORR_EVENT_SLOT_PREDICTED);
    set_slot_out(slot_out, slot_ref(journal, replacement));
    return WORR_EVENT_JOURNAL_INSERTED;
}

const worr_event_journal_slot_v1 *Worr_EventJournalResolveV1(
    const worr_event_journal_v1 *journal, worr_event_slot_ref_v1 slot)
{
    const worr_event_journal_slot_v1 *resolved;
    if (!journal_valid(journal) || slot.index >= journal->capacity ||
        slot.generation == 0) {
        return NULL;
    }
    resolved = &journal->slots[slot.index];
    if (!slot_used(resolved) || resolved->generation != slot.generation)
        return NULL;
    return resolved;
}

static worr_event_journal_slot_v1 *resolve_mutable(
    worr_event_journal_v1 *journal, worr_event_slot_ref_v1 slot)
{
    return (worr_event_journal_slot_v1 *)Worr_EventJournalResolveV1(
        journal, slot);
}

worr_event_journal_result_v1 Worr_EventJournalMarkPresentedV1(
    worr_event_journal_v1 *journal, worr_event_slot_ref_v1 slot_ref_value)
{
    worr_event_journal_slot_v1 *slot =
        resolve_mutable(journal, slot_ref_value);
    if (!slot)
        return WORR_EVENT_JOURNAL_NOT_FOUND;
    if ((slot->state & WORR_EVENT_SLOT_PRESENTED) != 0)
        return WORR_EVENT_JOURNAL_ALREADY_PRESENTED;
    if ((slot->state & (WORR_EVENT_SLOT_EXPIRED |
                        WORR_EVENT_SLOT_CANCELED)) != 0) {
        return WORR_EVENT_JOURNAL_TERMINAL;
    }
    if (((slot->state & WORR_EVENT_SLOT_RECEIVED) == 0 &&
         slot->record.prediction_class !=
             WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE) ||
        ((slot->state & WORR_EVENT_SLOT_RECEIVED) != 0 &&
         slot->record.delivery_class ==
             WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
         slot->record.event_id.sequence >
             journal->receipt.highest_contiguous)) {
        return WORR_EVENT_JOURNAL_NOT_READY;
    }
    slot->state |= WORR_EVENT_SLOT_PRESENTED;
    return WORR_EVENT_JOURNAL_INSERTED;
}

worr_event_journal_result_v1 Worr_EventJournalCancelV1(
    worr_event_journal_v1 *journal, worr_event_slot_ref_v1 slot_ref_value)
{
    worr_event_journal_slot_v1 *slot =
        resolve_mutable(journal, slot_ref_value);
    if (!slot)
        return WORR_EVENT_JOURNAL_NOT_FOUND;
    if (slot_terminal(slot))
        return WORR_EVENT_JOURNAL_TERMINAL;
    slot->state |= WORR_EVENT_SLOT_CANCELED;
    return WORR_EVENT_JOURNAL_INSERTED;
}

static bool tick_reached(uint32_t now_tick, uint32_t deadline)
{
    return now_tick - deadline <= INT32_MAX;
}

uint32_t Worr_EventJournalExpireV1(worr_event_journal_v1 *journal,
                                   uint32_t now_tick)
{
    uint32_t index;
    uint32_t expired = 0;
    if (!journal_valid(journal))
        return 0;
    for (index = 0; index < journal->capacity; ++index) {
        worr_event_journal_slot_v1 *slot = &journal->slots[index];
        if (!slot_used(slot) ||
            (slot->state & (WORR_EVENT_SLOT_EXPIRED |
                            WORR_EVENT_SLOT_CANCELED)) != 0 ||
            slot->record.delivery_class > WORR_EVENT_DELIVERY_TRANSIENT ||
            !tick_reached(now_tick, slot->record.expiry_tick)) {
            continue;
        }
        slot->state |= WORR_EVENT_SLOT_EXPIRED;
        ++expired;
    }
    return expired;
}

bool Worr_EventJournalNeedsPresentationV1(
    const worr_event_journal_v1 *journal,
    worr_event_slot_ref_v1 slot_ref_value,
    uint32_t now_tick)
{
    const worr_event_journal_slot_v1 *slot =
        Worr_EventJournalResolveV1(journal, slot_ref_value);
    if (!slot || slot_terminal(slot))
        return false;
    if (slot->record.delivery_class <= WORR_EVENT_DELIVERY_TRANSIENT &&
        tick_reached(now_tick, slot->record.expiry_tick)) {
        return false;
    }
    if ((slot->state & WORR_EVENT_SLOT_RECEIVED) != 0 &&
        slot->record.delivery_class ==
            WORR_EVENT_DELIVERY_RELIABLE_ORDERED &&
        slot->record.event_id.sequence >
            journal->receipt.highest_contiguous) {
        return false;
    }
    return (slot->state & WORR_EVENT_SLOT_RECEIVED) != 0 ||
           slot->record.prediction_class ==
               WORR_EVENT_PREDICTION_COMMAND_IMMEDIATE;
}

bool Worr_EventJournalFindAuthoritativeAtOrAfterV1(
    const worr_event_journal_v1 *journal,
    uint32_t min_sequence,
    worr_event_slot_ref_v1 *slot_out)
{
    uint32_t index;
    uint32_t best = WORR_EVENT_SLOT_INVALID;
    uint32_t best_sequence = UINT32_MAX;
    set_slot_out(slot_out, invalid_slot_ref());
    if (!journal_valid(journal) || !slot_out || min_sequence == 0)
        return false;
    for (index = 0; index < journal->capacity; ++index) {
        const worr_event_journal_slot_v1 *slot = &journal->slots[index];
        if (!slot_used(slot) ||
            (slot->state & WORR_EVENT_SLOT_RECEIVED) == 0 ||
            (slot->record.flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) == 0 ||
            slot->record.event_id.stream_epoch != journal->stream_epoch ||
            slot->record.event_id.sequence < min_sequence ||
            (best != WORR_EVENT_SLOT_INVALID &&
             slot->record.event_id.sequence >= best_sequence)) {
            continue;
        }
        best = index;
        best_sequence = slot->record.event_id.sequence;
    }
    if (best == WORR_EVENT_SLOT_INVALID)
        return false;
    *slot_out = slot_ref(journal, best);
    return true;
}

bool Worr_EventJournalFindAuthoritativeV1(
    const worr_event_journal_v1 *journal,
    worr_event_id_v1 event_id,
    worr_event_slot_ref_v1 *slot_out)
{
    uint32_t index;
    set_slot_out(slot_out, invalid_slot_ref());
    if (!journal_valid(journal) || !slot_out || event_id.sequence == 0)
        return false;
    for (index = 0; index < journal->capacity; ++index) {
        const worr_event_journal_slot_v1 *slot = &journal->slots[index];
        if (slot_used(slot) &&
            (slot->state & WORR_EVENT_SLOT_RECEIVED) != 0 &&
            (slot->record.flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) != 0 &&
            id_equal(slot->record.event_id, event_id)) {
            *slot_out = slot_ref(journal, index);
            return true;
        }
    }
    return false;
}
