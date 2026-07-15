/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_EVENT_JOURNAL_VERSION 1u
#define WORR_EVENT_SLOT_INVALID UINT32_MAX

enum {
    WORR_EVENT_SLOT_RECEIVED = 1u << 0,
    WORR_EVENT_SLOT_PREDICTED = 1u << 1,
    WORR_EVENT_SLOT_MATCHED = 1u << 2,
    WORR_EVENT_SLOT_PRESENTED = 1u << 3,
    WORR_EVENT_SLOT_EXPIRED = 1u << 4,
    WORR_EVENT_SLOT_CANCELED = 1u << 5,
    /* Authority was accepted for this prediction key but its semantic body
     * differed from the speculative record.  PRESENTED distinguishes a
     * correction discovered before presentation from one discovered after an
     * immediate one-shot was already emitted. */
    WORR_EVENT_SLOT_CORRECTED = 1u << 6,
};

typedef struct worr_event_slot_ref_v1_s {
    uint32_t index;
    uint32_t generation;
} worr_event_slot_ref_v1;

typedef struct worr_event_journal_slot_v1_s {
    worr_event_record_v1 record;
    uint64_t resident_order;
    uint32_t generation;
    uint32_t state;
} worr_event_journal_slot_v1;

/* Runtime-only envelope.  It is not serialized and owns no memory. */
typedef struct worr_event_journal_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_event_journal_slot_v1 *slots;
    uint32_t capacity;
    uint32_t occupied;
    uint32_t max_entities;
    uint32_t stream_epoch;
    uint64_t next_resident_order;
    worr_event_receipt_ack_v1 receipt;
} worr_event_journal_v1;

typedef enum worr_event_journal_result_v1_e {
    WORR_EVENT_JOURNAL_INSERTED = 0,
    WORR_EVENT_JOURNAL_DUPLICATE = 1,
    WORR_EVENT_JOURNAL_MATCHED = 2,
    WORR_EVENT_JOURNAL_COALESCED = 3,
    WORR_EVENT_JOURNAL_SUPERSEDED = 4,
    WORR_EVENT_JOURNAL_ALREADY_PRESENTED = 5,
    WORR_EVENT_JOURNAL_TERMINAL = 6,
    WORR_EVENT_JOURNAL_NOT_FOUND = 7,
    WORR_EVENT_JOURNAL_DROPPED_OVERFLOW = 8,
    WORR_EVENT_JOURNAL_CAPACITY_FATAL = 9,
    WORR_EVENT_JOURNAL_CONFLICT = 10,
    WORR_EVENT_JOURNAL_WRONG_EPOCH = 11,
    WORR_EVENT_JOURNAL_ACK_WINDOW = 12,
    WORR_EVENT_JOURNAL_INVALID_RECORD = 13,
    WORR_EVENT_JOURNAL_INVALID_ARGUMENT = 14,
    WORR_EVENT_JOURNAL_DROPPED_STALE = 15,
    WORR_EVENT_JOURNAL_NOT_READY = 16,
    WORR_EVENT_JOURNAL_CORRECTED = 17,
    WORR_EVENT_JOURNAL_CORRECTED_AFTER_PRESENTATION = 18,
} worr_event_journal_result_v1;

bool Worr_EventJournalInitV1(worr_event_journal_v1 *journal,
                             worr_event_journal_slot_v1 *storage,
                             uint32_t capacity,
                             uint32_t max_entities,
                             uint32_t stream_epoch);
bool Worr_EventJournalAdvanceEpochV1(worr_event_journal_v1 *journal,
                                     uint32_t stream_epoch);

worr_event_journal_result_v1 Worr_EventJournalInsertAuthoritativeV1(
    worr_event_journal_v1 *journal,
    const worr_event_record_v1 *record,
    worr_event_slot_ref_v1 *slot_out);
worr_event_journal_result_v1 Worr_EventJournalInsertPredictedV1(
    worr_event_journal_v1 *journal,
    const worr_event_record_v1 *record,
    worr_event_slot_ref_v1 *slot_out);

worr_event_journal_result_v1 Worr_EventJournalMarkPresentedV1(
    worr_event_journal_v1 *journal, worr_event_slot_ref_v1 slot);
worr_event_journal_result_v1 Worr_EventJournalCancelV1(
    worr_event_journal_v1 *journal, worr_event_slot_ref_v1 slot);
uint32_t Worr_EventJournalExpireV1(worr_event_journal_v1 *journal,
                                   uint32_t now_tick);

bool Worr_EventJournalNeedsPresentationV1(
    const worr_event_journal_v1 *journal,
    worr_event_slot_ref_v1 slot,
    uint32_t now_tick);
const worr_event_journal_slot_v1 *Worr_EventJournalResolveV1(
    const worr_event_journal_v1 *journal, worr_event_slot_ref_v1 slot);

/* Returns the lowest retained authoritative sequence at or after min_sequence.
 * This makes global stream order independent of packet arrival order. */
bool Worr_EventJournalFindAuthoritativeAtOrAfterV1(
    const worr_event_journal_v1 *journal,
    uint32_t min_sequence,
    worr_event_slot_ref_v1 *slot_out);
bool Worr_EventJournalFindAuthoritativeV1(
    const worr_event_journal_v1 *journal,
    worr_event_id_v1 event_id,
    worr_event_slot_ref_v1 *slot_out);

#ifdef __cplusplus
}
#endif
