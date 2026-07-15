/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "server/event_shadow.h"

#include "common/net/event_journal.h"

#include <string.h>

typedef struct event_shadow_state_s {
    worr_event_journal_v1 journal;
    worr_event_journal_slot_v1 slots[WORR_EVENT_SHADOW_CAPACITY];
    worr_event_id_v1 last_id;
    uint64_t reset_count;
    uint64_t submit_attempts;
    uint64_t accepted;
    uint64_t duplicates;
    uint64_t invalid;
    uint64_t capacity_failures;
    uint64_t conflicts;
    uint64_t id_exhaustions;
    uint64_t sequence_wraps;
    uint64_t query_count;
    uint64_t last_record_hash;
    worr_event_shadow_submit_result_v1 last_result;
    bool active;
} event_shadow_state_t;

static event_shadow_state_t shadow;
static uint32_t shadow_epoch_seed;
static uint64_t shadow_reset_count;

static bool entity_ref_equal(worr_event_entity_ref_v1 a,
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

static bool candidate_matches_record(const worr_event_record_v1 *candidate,
                                     const worr_event_record_v1 *record)
{
    return candidate->model_revision == record->model_revision &&
           candidate->flags ==
               (record->flags &
                ~(uint32_t)WORR_EVENT_FLAG_HAS_AUTHORITY_ID) &&
           candidate->source_tick == record->source_tick &&
           candidate->source_ordinal == record->source_ordinal &&
           candidate->source_time_us == record->source_time_us &&
           entity_ref_equal(candidate->source_entity,
                            record->source_entity) &&
           entity_ref_equal(candidate->subject_entity,
                            record->subject_entity) &&
           candidate->event_type == record->event_type &&
           candidate->delivery_class == record->delivery_class &&
           candidate->prediction_class == record->prediction_class &&
           prediction_key_equal(candidate->prediction_key,
                                record->prediction_key) &&
           candidate->expiry_tick == record->expiry_tick &&
           candidate->payload_kind == record->payload_kind &&
           candidate->payload_size == record->payload_size &&
           memcmp(candidate->payload, record->payload,
                  WORR_EVENT_PAYLOAD_CAPACITY) == 0;
}

static bool candidate_retained(const worr_event_record_v1 *candidate)
{
    uint32_t index;
    for (index = 0; index < shadow.journal.capacity; ++index) {
        const worr_event_journal_slot_v1 *slot = &shadow.slots[index];
        if (slot->state != 0 &&
            candidate_matches_record(candidate, &slot->record)) {
            return true;
        }
    }
    return false;
}

static worr_event_shadow_submit_result_v1 submit_candidate(
    const worr_event_record_v1 *candidate)
{
    worr_event_record_v1 authoritative;
    worr_event_slot_ref_v1 slot;
    worr_event_id_v1 next_id;
    worr_event_journal_result_v1 result;
    uint64_t record_hash;

    ++shadow.submit_attempts;
    if (!shadow.active) {
        shadow.last_result = WORR_EVENT_SHADOW_UNAVAILABLE;
        return shadow.last_result;
    }
    if (!candidate || candidate->struct_size != sizeof(*candidate) ||
        (candidate->flags & WORR_EVENT_FLAG_HAS_AUTHORITY_ID) != 0 ||
        candidate->event_id.stream_epoch != 0 ||
        candidate->event_id.sequence != 0) {
        ++shadow.invalid;
        shadow.last_result = WORR_EVENT_SHADOW_INVALID;
        return shadow.last_result;
    }
    if (!Worr_EventIdNextV1(shadow.last_id, &next_id)) {
        ++shadow.id_exhaustions;
        shadow.last_result = WORR_EVENT_SHADOW_ID_EXHAUSTED;
        return shadow.last_result;
    }

    authoritative = *candidate;
    authoritative.flags |= WORR_EVENT_FLAG_HAS_AUTHORITY_ID;
    authoritative.event_id = next_id;
    if (!Worr_EventRecordValidateV1(
            &authoritative, WORR_EVENT_SHADOW_MAX_ENTITIES) ||
        !Worr_EventRecordHashV1(&authoritative,
                                WORR_EVENT_SHADOW_MAX_ENTITIES,
                                &record_hash)) {
        ++shadow.invalid;
        shadow.last_result = WORR_EVENT_SHADOW_INVALID;
        return shadow.last_result;
    }

    if (next_id.stream_epoch == shadow.journal.stream_epoch &&
        candidate_retained(candidate)) {
        ++shadow.duplicates;
        shadow.last_result = WORR_EVENT_SHADOW_DUPLICATE;
        return shadow.last_result;
    }

    if (next_id.stream_epoch != shadow.journal.stream_epoch) {
        if (!Worr_EventJournalAdvanceEpochV1(&shadow.journal,
                                             next_id.stream_epoch)) {
            ++shadow.id_exhaustions;
            shadow.last_result = WORR_EVENT_SHADOW_ID_EXHAUSTED;
            return shadow.last_result;
        }
        shadow_epoch_seed = next_id.stream_epoch;
        ++shadow.sequence_wraps;
    }

    result = Worr_EventJournalInsertAuthoritativeV1(
        &shadow.journal, &authoritative, &slot);
    switch (result) {
    case WORR_EVENT_JOURNAL_INSERTED:
    case WORR_EVENT_JOURNAL_MATCHED:
    case WORR_EVENT_JOURNAL_CORRECTED:
    case WORR_EVENT_JOURNAL_CORRECTED_AFTER_PRESENTATION:
    case WORR_EVENT_JOURNAL_COALESCED:
    case WORR_EVENT_JOURNAL_SUPERSEDED:
        shadow.last_id = next_id;
        shadow.last_record_hash = record_hash;
        ++shadow.accepted;
        shadow.last_result = WORR_EVENT_SHADOW_ACCEPTED;
        return shadow.last_result;
    case WORR_EVENT_JOURNAL_DUPLICATE:
        ++shadow.duplicates;
        shadow.last_result = WORR_EVENT_SHADOW_DUPLICATE;
        return shadow.last_result;
    case WORR_EVENT_JOURNAL_DROPPED_OVERFLOW:
    case WORR_EVENT_JOURNAL_CAPACITY_FATAL:
        ++shadow.capacity_failures;
        shadow.last_result = WORR_EVENT_SHADOW_CAPACITY_EXHAUSTED;
        return shadow.last_result;
    default:
        ++shadow.conflicts;
        shadow.last_result = WORR_EVENT_SHADOW_CONFLICT;
        return shadow.last_result;
    }
}

static bool get_status(worr_event_shadow_status_v1 *status_out)
{
    if (!status_out)
        return false;
    ++shadow.query_count;
    memset(status_out, 0, sizeof(*status_out));
    status_out->struct_size = sizeof(*status_out);
    status_out->schema_version = WORR_EVENT_SHADOW_API_VERSION;
    status_out->stream_epoch = shadow.active ? shadow.journal.stream_epoch
                                             : shadow_epoch_seed;
    status_out->capacity = WORR_EVENT_SHADOW_CAPACITY;
    status_out->occupied = shadow.active ? shadow.journal.occupied : 0;
    status_out->last_sequence = shadow.last_id.sequence;
    status_out->last_result = (uint32_t)shadow.last_result;
    status_out->reset_count = shadow.reset_count;
    status_out->submit_attempts = shadow.submit_attempts;
    status_out->accepted = shadow.accepted;
    status_out->duplicates = shadow.duplicates;
    status_out->invalid = shadow.invalid;
    status_out->capacity_failures = shadow.capacity_failures;
    status_out->conflicts = shadow.conflicts;
    status_out->id_exhaustions = shadow.id_exhaustions;
    status_out->sequence_wraps = shadow.sequence_wraps;
    status_out->query_count = shadow.query_count;
    status_out->last_record_hash = shadow.last_record_hash;
    return true;
}

static bool get_record_from_newest(uint32_t age,
                                   worr_event_record_v1 *record_out,
                                   uint32_t *slot_state_out,
                                   uint64_t *record_hash_out)
{
    uint64_t ceiling = UINT64_MAX;
    uint32_t selected = WORR_EVENT_SLOT_INVALID;
    uint32_t rank;
    uint32_t index;

    ++shadow.query_count;
    if (!shadow.active || !record_out || age >= shadow.journal.occupied)
        return false;

    for (rank = 0; rank <= age; ++rank) {
        uint64_t best_order = 0;
        selected = WORR_EVENT_SLOT_INVALID;
        for (index = 0; index < shadow.journal.capacity; ++index) {
            const worr_event_journal_slot_v1 *slot = &shadow.slots[index];
            if (slot->state != 0 && slot->resident_order < ceiling &&
                slot->resident_order > best_order) {
                selected = index;
                best_order = slot->resident_order;
            }
        }
        if (selected == WORR_EVENT_SLOT_INVALID)
            return false;
        ceiling = shadow.slots[selected].resident_order;
    }

    *record_out = shadow.slots[selected].record;
    if (slot_state_out)
        *slot_state_out = shadow.slots[selected].state;
    if (record_hash_out &&
        !Worr_EventRecordHashV1(record_out,
                                WORR_EVENT_SHADOW_MAX_ENTITIES,
                                record_hash_out)) {
        return false;
    }
    return true;
}

static const worr_event_shadow_import_v1 shadow_import = {
    sizeof(shadow_import),
    WORR_EVENT_SHADOW_API_VERSION,
    submit_candidate,
    get_status,
    get_record_from_newest,
};

void SV_EventShadowResetMap(void)
{
    uint32_t next_epoch;

    ++shadow_reset_count;
    if (shadow_epoch_seed == UINT32_MAX) {
        memset(&shadow, 0, sizeof(shadow));
        shadow.reset_count = shadow_reset_count;
        shadow.last_result = WORR_EVENT_SHADOW_ID_EXHAUSTED;
        shadow.id_exhaustions = 1;
        return;
    }

    next_epoch = shadow_epoch_seed + 1u;
    memset(&shadow, 0, sizeof(shadow));
    shadow.reset_count = shadow_reset_count;
    shadow_epoch_seed = next_epoch;
    shadow.last_id.stream_epoch = next_epoch;
    if (!Worr_EventJournalInitV1(&shadow.journal, shadow.slots,
                                 WORR_EVENT_SHADOW_CAPACITY,
                                 WORR_EVENT_SHADOW_MAX_ENTITIES,
                                 next_epoch)) {
        shadow.last_result = WORR_EVENT_SHADOW_UNAVAILABLE;
        return;
    }
    shadow.active = true;
    shadow.last_result = WORR_EVENT_SHADOW_ACCEPTED;
}

const worr_event_shadow_import_v1 *SV_EventShadowImportV1(void)
{
    return &shadow_import;
}

#if defined(WORR_EVENT_SHADOW_TESTING)
bool SV_EventShadowTestSetCursor(uint32_t stream_epoch, uint32_t sequence)
{
    if (!shadow.active || stream_epoch < shadow.journal.stream_epoch ||
        stream_epoch == 0 || sequence == 0)
        return false;
    if (stream_epoch > shadow.journal.stream_epoch &&
        !Worr_EventJournalAdvanceEpochV1(&shadow.journal, stream_epoch)) {
        return false;
    }
    shadow_epoch_seed = stream_epoch;
    shadow.last_id.stream_epoch = stream_epoch;
    shadow.last_id.sequence = sequence;
    shadow.journal.receipt.highest_contiguous = sequence;
    shadow.journal.receipt.selective_mask = 0;
    return true;
}
#endif
