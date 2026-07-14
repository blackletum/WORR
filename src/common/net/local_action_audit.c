/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/local_action_audit.h"

#include <string.h>

#define LOCAL_ACTION_AUDIT_KNOWN_SLOT_FLAGS                                    \
  ((uint32_t)(WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED |                     \
              WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE |                 \
              WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED))

#define LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY 1024u

_Static_assert(LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY >=
                   WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY * 2u,
               "local action audit ID table must stay at most half full");
_Static_assert((LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY &
                (LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY - 1u)) == 0,
               "local action audit ID table must be a power of two");

typedef struct byte_range_s {
  uintptr_t begin;
  uintptr_t end;
} byte_range;

static bool aligned_pointer(const void *pointer, size_t alignment) {
  return pointer && ((uintptr_t)pointer % alignment) == 0;
}

static bool checked_array_size(uint32_t count, size_t element_size,
                               size_t *size_out) {
  if (!size_out || (count != 0 && element_size > SIZE_MAX / (size_t)count)) {
    return false;
  }
  *size_out = (size_t)count * element_size;
  return true;
}

static bool make_range(const void *pointer, size_t size,
                       byte_range *range_out) {
  const uintptr_t begin = (uintptr_t)pointer;
  if (!pointer || !range_out || size == 0 || size > UINTPTR_MAX - begin) {
    return false;
  }
  range_out->begin = begin;
  range_out->end = begin + (uintptr_t)size;
  return true;
}

static bool ranges_overlap(byte_range left, byte_range right) {
  return left.begin < right.end && right.begin < left.end;
}

static bool bytes_are_zero(const void *data, size_t size) {
  const uint8_t *bytes = (const uint8_t *)data;
  size_t index;
  if (!data)
    return false;
  for (index = 0; index < size; ++index) {
    if (bytes[index] != 0)
      return false;
  }
  return true;
}

static void saturating_increment(uint64_t *counter) {
  if (*counter != UINT64_MAX)
    ++*counter;
}

static void saturating_add(uint64_t *counter, uint64_t amount) {
  if (*counter > UINT64_MAX - amount)
    *counter = UINT64_MAX;
  else
    *counter += amount;
}

static uint64_t saturated_sum(uint64_t left, uint64_t right) {
  return left > UINT64_MAX - right ? UINT64_MAX : left + right;
}

static bool counter_not_greater(uint64_t value, uint64_t upper_bound) {
  return upper_bound == UINT64_MAX || value <= upper_bound;
}

static bool command_id_equal(worr_command_id_v1 left,
                             worr_command_id_v1 right) {
  return left.epoch == right.epoch && left.sequence == right.sequence;
}

static int command_id_compare(worr_command_id_v1 left,
                              worr_command_id_v1 right) {
  if (left.epoch != right.epoch)
    return left.epoch < right.epoch ? -1 : 1;
  if (left.sequence != right.sequence)
    return left.sequence < right.sequence ? -1 : 1;
  return 0;
}

static bool command_id_valid_or_absent(worr_command_id_v1 command_id) {
  return (command_id.epoch == 0 && command_id.sequence == 0) ||
         Worr_CommandIdValidV1(command_id, false);
}

static uint32_t ring_index(const worr_local_action_audit_v1 *audit,
                           uint32_t offset) {
  return (uint32_t)(((uint64_t)audit->head + offset) % audit->capacity);
}

static worr_local_action_audit_slot_v1 *
slot_at(worr_local_action_audit_v1 *audit, uint32_t chronological_index) {
  return &audit->slots[ring_index(audit, chronological_index)];
}

static const worr_local_action_audit_slot_v1 *
slot_at_const(const worr_local_action_audit_v1 *audit,
              uint32_t chronological_index) {
  return &audit->slots[ring_index(audit, chronological_index)];
}

static bool audit_ranges(const worr_local_action_audit_v1 *audit,
                         byte_range *audit_range_out,
                         byte_range *storage_range_out) {
  size_t storage_size;
  return audit && audit->slots && audit->capacity != 0 &&
         checked_array_size(audit->capacity, sizeof(*audit->slots),
                            &storage_size) &&
         make_range(audit, sizeof(*audit), audit_range_out) &&
         make_range(audit->slots, storage_size, storage_range_out);
}

static bool physical_slot_is_active(const worr_local_action_audit_v1 *audit,
                                    uint32_t physical_index) {
  uint32_t distance;
  if (audit->count == 0)
    return false;
  if (physical_index >= audit->head)
    distance = physical_index - audit->head;
  else
    distance = audit->capacity - audit->head + physical_index;
  return distance < audit->count;
}

static bool slot_validate(const worr_local_action_audit_slot_v1 *slot,
                          worr_command_id_v1 pruned_through,
                          bool reproduce_correction) {
  const uint32_t role_flags =
      slot ? slot->flags & (WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED |
                            WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE)
           : 0;
  worr_local_action_correction_v2 expected;

  if (!slot || slot->struct_size != sizeof(*slot) ||
      slot->schema_version != WORR_LOCAL_ACTION_AUDIT_VERSION ||
      (slot->flags & ~LOCAL_ACTION_AUDIT_KNOWN_SLOT_FLAGS) != 0 ||
      slot->reserved0 != 0 || !Worr_CommandIdValidV1(slot->command_id, false) ||
      command_id_compare(slot->command_id, pruned_through) <= 0 ||
      slot->first_arrival_serial == 0) {
    return false;
  }

  if (role_flags == WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED) {
    if ((slot->flags & WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED) != 0 ||
        !Worr_LocalActionTransactionValidateV2(&slot->predicted) ||
        slot->predicted.producer_role != WORR_LOCAL_ACTION_PRODUCER_PREDICTED ||
        !command_id_equal(slot->predicted.command.command_id,
                          slot->command_id) ||
        !bytes_are_zero(&slot->authoritative, sizeof(slot->authoritative)) ||
        !bytes_are_zero(&slot->correction, sizeof(slot->correction))) {
      return false;
    }
    return true;
  }

  if (role_flags == WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE) {
    if ((slot->flags & WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED) != 0 ||
        !bytes_are_zero(&slot->predicted, sizeof(slot->predicted)) ||
        !Worr_LocalActionTransactionValidateV2(&slot->authoritative) ||
        slot->authoritative.producer_role !=
            WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE ||
        !command_id_equal(slot->authoritative.command.command_id,
                          slot->command_id) ||
        !bytes_are_zero(&slot->correction, sizeof(slot->correction))) {
      return false;
    }
    return true;
  }

  if (role_flags != (WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED |
                     WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE) ||
      (slot->flags & WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED) == 0 ||
      !Worr_LocalActionTransactionValidateV2(&slot->predicted) ||
      !Worr_LocalActionTransactionValidateV2(&slot->authoritative) ||
      slot->predicted.producer_role != WORR_LOCAL_ACTION_PRODUCER_PREDICTED ||
      slot->authoritative.producer_role !=
          WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE ||
      !command_id_equal(slot->predicted.command.command_id, slot->command_id) ||
      !command_id_equal(slot->authoritative.command.command_id,
                        slot->command_id) ||
      !Worr_LocalActionCorrectionValidateV2(&slot->correction)) {
    return false;
  }

  if (!reproduce_correction)
    return true;
  memset(&expected, 0, sizeof(expected));
  return Worr_LocalActionCorrectionClassifyV2(
             &slot->predicted, &slot->authoritative, &expected) &&
         memcmp(&slot->correction, &expected, sizeof(expected)) == 0;
}

static bool transaction_header_is_zero(
    const worr_local_action_transaction_v2 *transaction) {
  return transaction->struct_size == 0 && transaction->schema_version == 0 &&
         transaction->model_revision == 0 && transaction->producer_role == 0 &&
         transaction->reserved0 == 0 && transaction->event_count == 0 &&
         transaction->transaction_hash == 0;
}

static bool
transaction_header_validate(const worr_local_action_transaction_v2 *transaction,
                            uint32_t expected_role,
                            worr_command_id_v1 expected_command_id) {
  return transaction->struct_size == sizeof(*transaction) &&
         transaction->schema_version == WORR_LOCAL_ACTION_ABI_VERSION &&
         transaction->model_revision == WORR_LOCAL_ACTION_MODEL_REVISION &&
         transaction->producer_role == expected_role &&
         transaction->reserved0 == 0 &&
         transaction->event_count <= WORR_LOCAL_ACTION_MAX_EVENTS &&
         command_id_equal(transaction->command.command_id, expected_command_id);
}

static bool
correction_header_is_zero(const worr_local_action_correction_v2 *correction) {
  return correction->struct_size == 0 && correction->schema_version == 0 &&
         correction->model_revision == 0 && correction->classification == 0 &&
         correction->difference_bits == 0 && correction->flags == 0 &&
         correction->predicted_transaction_hash == 0 &&
         correction->authoritative_transaction_hash == 0;
}

/*
 * This is intentionally a structural validator.  It reads a bounded header
 * from every active slot, while semantic reconstruction is reserved for a
 * touched slot or the explicit deep audit.
 */
static bool
slot_validate_operational(const worr_local_action_audit_slot_v1 *slot,
                          worr_command_id_v1 pruned_through) {
  const uint32_t role_flags =
      slot ? slot->flags & (WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED |
                            WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE)
           : 0;
  const bool has_predicted =
      (role_flags & WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED) != 0;
  const bool has_authoritative =
      (role_flags & WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE) != 0;
  const bool classified =
      slot && (slot->flags & WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED) != 0;

  if (!slot || slot->struct_size != sizeof(*slot) ||
      slot->schema_version != WORR_LOCAL_ACTION_AUDIT_VERSION ||
      (slot->flags & ~LOCAL_ACTION_AUDIT_KNOWN_SLOT_FLAGS) != 0 ||
      slot->reserved0 != 0 || !Worr_CommandIdValidV1(slot->command_id, false) ||
      command_id_compare(slot->command_id, pruned_through) <= 0 ||
      slot->first_arrival_serial == 0 || role_flags == 0 ||
      classified != (has_predicted && has_authoritative)) {
    return false;
  }

  if (has_predicted) {
    if (!transaction_header_validate(&slot->predicted,
                                     WORR_LOCAL_ACTION_PRODUCER_PREDICTED,
                                     slot->command_id)) {
      return false;
    }
  } else if (!transaction_header_is_zero(&slot->predicted)) {
    return false;
  }

  if (has_authoritative) {
    if (!transaction_header_validate(&slot->authoritative,
                                     WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE,
                                     slot->command_id)) {
      return false;
    }
  } else if (!transaction_header_is_zero(&slot->authoritative)) {
    return false;
  }

  if (!classified)
    return correction_header_is_zero(&slot->correction);

  return Worr_LocalActionCorrectionValidateV2(&slot->correction) &&
         slot->correction.predicted_transaction_hash ==
             slot->predicted.transaction_hash &&
         slot->correction.authoritative_transaction_hash ==
             slot->authoritative.transaction_hash &&
         memcmp(&slot->correction.predicted_cursor,
                &slot->predicted.state_after.applied_cursor,
                sizeof(slot->correction.predicted_cursor)) == 0 &&
         memcmp(&slot->correction.authoritative_cursor,
                &slot->authoritative.state_after.applied_cursor,
                sizeof(slot->correction.authoritative_cursor)) == 0;
}

static uint32_t command_id_hash(worr_command_id_v1 command_id) {
  uint64_t value =
      ((uint64_t)command_id.epoch << 32) | (uint64_t)command_id.sequence;
  value ^= value >> 33;
  value *= UINT64_C(0xff51afd7ed558ccd);
  value ^= value >> 33;
  value *= UINT64_C(0xc4ceb9fe1a85ec53);
  value ^= value >> 33;
  return (uint32_t)value;
}

static bool command_id_set_insert(
    worr_command_id_v1 entries[LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY],
    worr_command_id_v1 command_id) {
  uint32_t probe =
      command_id_hash(command_id) & (LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY - 1u);
  uint32_t attempts;

  for (attempts = 0; attempts < LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY;
       ++attempts) {
    worr_command_id_v1 *entry = &entries[probe];
    if (entry->epoch == 0) {
      *entry = command_id;
      return true;
    }
    if (command_id_equal(*entry, command_id))
      return false;
    probe = (probe + 1u) & (LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY - 1u);
  }
  return false;
}

static const worr_local_action_audit_slot_v1 *
find_slot_const(const worr_local_action_audit_v1 *audit,
                worr_command_id_v1 command_id) {
  uint32_t index;
  for (index = 0; index < audit->count; ++index) {
    const worr_local_action_audit_slot_v1 *slot = slot_at_const(audit, index);
    if (command_id_equal(slot->command_id, command_id))
      return slot;
  }
  return NULL;
}

static worr_local_action_audit_slot_v1 *
find_slot(worr_local_action_audit_v1 *audit, worr_command_id_v1 command_id) {
  return (worr_local_action_audit_slot_v1 *)find_slot_const(audit, command_id);
}

bool Worr_LocalActionAuditInitV1(worr_local_action_audit_v1 *audit,
                                 worr_local_action_audit_slot_v1 *storage,
                                 uint32_t capacity, uint32_t connection_epoch) {
  worr_local_action_audit_v1 initialized;
  byte_range audit_range;
  byte_range storage_range;
  size_t storage_size;

  if (!aligned_pointer(audit, _Alignof(worr_local_action_audit_v1)) ||
      !aligned_pointer(storage, _Alignof(worr_local_action_audit_slot_v1)) ||
      capacity == 0 || capacity > WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY ||
      connection_epoch == 0 ||
      !checked_array_size(capacity, sizeof(*storage), &storage_size) ||
      !make_range(audit, sizeof(*audit), &audit_range) ||
      !make_range(storage, storage_size, &storage_range) ||
      ranges_overlap(audit_range, storage_range)) {
    return false;
  }

  memset(&initialized, 0, sizeof(initialized));
  initialized.struct_size = sizeof(initialized);
  initialized.schema_version = WORR_LOCAL_ACTION_AUDIT_VERSION;
  initialized.slots = storage;
  initialized.capacity = capacity;
  initialized.connection_epoch = connection_epoch;
  initialized.generation = 1;
  initialized.next_arrival_serial = 1;

  memset(storage, 0, storage_size);
  *audit = initialized;
  return true;
}

bool Worr_LocalActionAuditInitV2(worr_local_action_audit_v1 *audit,
                                 worr_local_action_audit_slot_v1 *storage,
                                 uint32_t capacity, uint32_t connection_epoch) {
  return Worr_LocalActionAuditInitV1(audit, storage, capacity,
                                     connection_epoch);
}

static bool local_action_audit_validate(const worr_local_action_audit_v1 *audit,
                                        bool deep) {
  byte_range audit_range;
  byte_range storage_range;
  worr_command_id_v1 command_ids[LOCAL_ACTION_AUDIT_ID_TABLE_CAPACITY] = {
      {0, 0}};
  uint64_t unmatched_predicted = 0;
  uint64_t unmatched_authoritative = 0;
  uint64_t retained_pairs = 0;
  uint64_t correction_total;
  uint64_t previous_serial = 0;
  uint32_t index;

  if (!aligned_pointer(audit, _Alignof(worr_local_action_audit_v1)) ||
      audit->struct_size != sizeof(*audit) ||
      audit->schema_version != WORR_LOCAL_ACTION_AUDIT_VERSION ||
      !aligned_pointer(audit->slots,
                       _Alignof(worr_local_action_audit_slot_v1)) ||
      audit->capacity == 0 ||
      audit->capacity > WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY ||
      audit->count > audit->capacity || audit->connection_epoch == 0 ||
      audit->generation == 0 || audit->reserved0 != 0 ||
      !command_id_valid_or_absent(audit->pruned_through) ||
      audit->next_arrival_serial == 0 ||
      (audit->count == 0 ? audit->head != 0 : audit->head >= audit->capacity) ||
      !audit_ranges(audit, &audit_range, &storage_range) ||
      ranges_overlap(audit_range, storage_range)) {
    return false;
  }

  if (deep) {
    for (index = 0; index < audit->capacity; ++index) {
      if (!physical_slot_is_active(audit, index) &&
          !bytes_are_zero(&audit->slots[index], sizeof(audit->slots[index]))) {
        return false;
      }
    }
  }

  for (index = 0; index < audit->count; ++index) {
    const worr_local_action_audit_slot_v1 *slot = slot_at_const(audit, index);
    if (!slot_validate_operational(slot, audit->pruned_through) ||
        (deep && !slot_validate(slot, audit->pruned_through, true)) ||
        slot->first_arrival_serial >= audit->next_arrival_serial ||
        (previous_serial != 0 &&
         slot->first_arrival_serial <= previous_serial) ||
        !command_id_set_insert(command_ids, slot->command_id)) {
      return false;
    }
    previous_serial = slot->first_arrival_serial;

    if ((slot->flags & WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED) != 0)
      ++retained_pairs;
    else if ((slot->flags & WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED) != 0)
      ++unmatched_predicted;
    else
      ++unmatched_authoritative;
  }

  correction_total = saturated_sum(audit->telemetry.exact_pairs,
                                   audit->telemetry.presentation_corrections);
  correction_total =
      saturated_sum(correction_total, audit->telemetry.gameplay_corrections);
  correction_total =
      saturated_sum(correction_total, audit->telemetry.hard_resync_corrections);

  if (audit->telemetry.unmatched_predicted != unmatched_predicted ||
      audit->telemetry.unmatched_authoritative != unmatched_authoritative ||
      correction_total != audit->telemetry.pairs_classified ||
      !counter_not_greater(audit->count, audit->telemetry.slots_created) ||
      !counter_not_greater(retained_pairs, audit->telemetry.pairs_classified) ||
      !counter_not_greater(audit->telemetry.pairs_classified,
                           audit->telemetry.slots_created) ||
      !counter_not_greater(audit->telemetry.predicted_submissions,
                           audit->telemetry.submit_attempts) ||
      !counter_not_greater(audit->telemetry.authoritative_submissions,
                           audit->telemetry.submit_attempts) ||
      !counter_not_greater(audit->telemetry.copies,
                           audit->telemetry.copy_attempts) ||
      !counter_not_greater(audit->telemetry.prunes,
                           audit->telemetry.prune_attempts) ||
      !counter_not_greater(audit->telemetry.prune_blocks,
                           audit->telemetry.prune_attempts) ||
      !counter_not_greater(audit->telemetry.capacity_stalls,
                           audit->telemetry.submit_attempts) ||
      !counter_not_greater(audit->telemetry.serial_exhaustions,
                           audit->telemetry.submit_attempts) ||
      !counter_not_greater(audit->telemetry.conflicts,
                           audit->telemetry.submit_attempts) ||
      !counter_not_greater(audit->telemetry.duplicates,
                           audit->telemetry.submit_attempts) ||
      !counter_not_greater(audit->telemetry.classification_failures,
                           audit->telemetry.submit_attempts) ||
      !counter_not_greater(saturated_sum(audit->telemetry.invalid_transactions,
                                         audit->telemetry.invalid_roles),
                           audit->telemetry.submit_attempts) ||
      !counter_not_greater(saturated_sum(audit->telemetry.resets,
                                         audit->telemetry.reset_rejections),
                           audit->telemetry.reset_attempts)) {
    return false;
  }
  return true;
}

bool Worr_LocalActionAuditValidateV1(const worr_local_action_audit_v1 *audit) {
  return local_action_audit_validate(audit, true);
}

bool Worr_LocalActionAuditValidateOperationalV2(
    const worr_local_action_audit_v1 *audit) {
  return local_action_audit_validate(audit, false);
}

bool Worr_LocalActionAuditValidateDeepV2(
    const worr_local_action_audit_v1 *audit) {
  return local_action_audit_validate(audit, true);
}

static void record_submit_attempt(worr_local_action_audit_v1 *next,
                                  uint32_t producer_role) {
  saturating_increment(&next->telemetry.submit_attempts);
  if (producer_role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED)
    saturating_increment(&next->telemetry.predicted_submissions);
  else
    saturating_increment(&next->telemetry.authoritative_submissions);
}

static worr_local_action_audit_result_v1 local_action_audit_submit(
    worr_local_action_audit_v1 *audit, uint32_t submission_connection_epoch,
    const worr_local_action_transaction_v2 *transaction, bool deep) {
  worr_local_action_audit_v1 next;
  worr_local_action_audit_slot_v1 candidate;
  worr_local_action_audit_slot_v1 *existing;
  byte_range audit_range;
  byte_range storage_range;
  byte_range transaction_range;
  worr_command_id_v1 command_id;
  uint32_t role;
  uint32_t target;

  if (!audit)
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  if (!local_action_audit_validate(audit, deep))
    return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;

  next = *audit;
  if (!transaction ||
      !aligned_pointer(transaction,
                       _Alignof(worr_local_action_transaction_v2))) {
    saturating_increment(&next.telemetry.submit_attempts);
    saturating_increment(&next.telemetry.invalid_arguments);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  }
  if (!audit_ranges(audit, &audit_range, &storage_range) ||
      !make_range(transaction, sizeof(*transaction), &transaction_range) ||
      ranges_overlap(transaction_range, audit_range) ||
      ranges_overlap(transaction_range, storage_range)) {
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  }

  role = transaction->producer_role;
  if (role != WORR_LOCAL_ACTION_PRODUCER_PREDICTED &&
      role != WORR_LOCAL_ACTION_PRODUCER_AUTHORITATIVE) {
    saturating_increment(&next.telemetry.submit_attempts);
    saturating_increment(&next.telemetry.invalid_roles);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ROLE;
  }
  if (!Worr_LocalActionTransactionValidateV2(transaction)) {
    saturating_increment(&next.telemetry.submit_attempts);
    saturating_increment(&next.telemetry.invalid_transactions);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_TRANSACTION;
  }

  record_submit_attempt(&next, role);
  if (submission_connection_epoch == 0) {
    saturating_increment(&next.telemetry.invalid_arguments);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  }
  if (submission_connection_epoch != audit->connection_epoch) {
    saturating_increment(&next.telemetry.wrong_connection_epoch);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_WRONG_CONNECTION_EPOCH;
  }
  command_id = transaction->command.command_id;
  if (command_id_compare(command_id, audit->pruned_through) <= 0) {
    saturating_increment(&next.telemetry.stale);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_STALE;
  }

  existing = find_slot(audit, command_id);
  if (existing) {
    const worr_local_action_transaction_v2 *retained =
        role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED ? &existing->predicted
                                                     : &existing->authoritative;
    const uint32_t role_flag =
        role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED
            ? WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED
            : WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE;

    if (!deep && !slot_validate(existing, audit->pruned_through, true)) {
      return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;
    }

    if ((existing->flags & role_flag) != 0) {
      if (memcmp(retained, transaction, sizeof(*transaction)) == 0) {
        saturating_increment(&next.telemetry.duplicates);
        *audit = next;
        return WORR_LOCAL_ACTION_AUDIT_DUPLICATE;
      }
      saturating_increment(&next.telemetry.conflicts);
      *audit = next;
      return WORR_LOCAL_ACTION_AUDIT_CONFLICT;
    }

    candidate = *existing;
    if (role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED) {
      candidate.predicted = *transaction;
      candidate.flags |= WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED;
    } else {
      candidate.authoritative = *transaction;
      candidate.flags |= WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE;
    }
    candidate.flags |= WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED;
    memset(&candidate.correction, 0, sizeof(candidate.correction));
    if (!Worr_LocalActionCorrectionClassifyV2(&candidate.predicted,
                                              &candidate.authoritative,
                                              &candidate.correction) ||
        !slot_validate(&candidate, audit->pruned_through, false)) {
      saturating_increment(&next.telemetry.classification_failures);
      *audit = next;
      return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;
    }

    saturating_increment(&next.telemetry.pairs_classified);
    switch (candidate.correction.classification) {
    case WORR_LOCAL_ACTION_CORRECTION_NONE:
      saturating_increment(&next.telemetry.exact_pairs);
      break;
    case WORR_LOCAL_ACTION_CORRECTION_PRESENTATION_ONLY:
      saturating_increment(&next.telemetry.presentation_corrections);
      break;
    case WORR_LOCAL_ACTION_CORRECTION_GAMEPLAY_IMMEDIATE:
      saturating_increment(&next.telemetry.gameplay_corrections);
      break;
    case WORR_LOCAL_ACTION_CORRECTION_HARD_RESYNC:
      saturating_increment(&next.telemetry.hard_resync_corrections);
      break;
    default:
      return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;
    }
    if (role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED)
      --next.telemetry.unmatched_authoritative;
    else
      --next.telemetry.unmatched_predicted;

    *existing = candidate;
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_PAIRED;
  }

  if (audit->count == audit->capacity) {
    saturating_increment(&next.telemetry.capacity_stalls);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_CAPACITY;
  }
  if (audit->next_arrival_serial == UINT64_MAX) {
    saturating_increment(&next.telemetry.serial_exhaustions);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_SERIAL_EXHAUSTED;
  }

  memset(&candidate, 0, sizeof(candidate));
  candidate.struct_size = sizeof(candidate);
  candidate.schema_version = WORR_LOCAL_ACTION_AUDIT_VERSION;
  candidate.command_id = command_id;
  candidate.first_arrival_serial = audit->next_arrival_serial;
  if (role == WORR_LOCAL_ACTION_PRODUCER_PREDICTED) {
    candidate.flags = WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_PREDICTED;
    candidate.predicted = *transaction;
    ++next.telemetry.unmatched_predicted;
  } else {
    candidate.flags = WORR_LOCAL_ACTION_AUDIT_SLOT_HAS_AUTHORITATIVE;
    candidate.authoritative = *transaction;
    ++next.telemetry.unmatched_authoritative;
  }
  if (!slot_validate(&candidate, audit->pruned_through, false)) {
    saturating_increment(&next.telemetry.invalid_transactions);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_TRANSACTION;
  }

  target = ring_index(audit, audit->count);
  ++next.count;
  ++next.next_arrival_serial;
  saturating_increment(&next.telemetry.slots_created);
  audit->slots[target] = candidate;
  *audit = next;
  return WORR_LOCAL_ACTION_AUDIT_INSERTED;
}

worr_local_action_audit_result_v1 Worr_LocalActionAuditSubmitV1(
    worr_local_action_audit_v1 *audit, uint32_t submission_connection_epoch,
    const worr_local_action_transaction_v2 *transaction) {
  return local_action_audit_submit(audit, submission_connection_epoch,
                                   transaction, true);
}

worr_local_action_audit_result_v1 Worr_LocalActionAuditSubmitV2(
    worr_local_action_audit_v1 *audit, uint32_t submission_connection_epoch,
    const worr_local_action_transaction_v2 *transaction) {
  return local_action_audit_submit(audit, submission_connection_epoch,
                                   transaction, false);
}

static worr_local_action_audit_result_v1
local_action_audit_copy(worr_local_action_audit_v1 *audit,
                        worr_command_id_v1 command_id,
                        worr_local_action_audit_slot_v1 *slot_out, bool deep) {
  worr_local_action_audit_v1 next;
  const worr_local_action_audit_slot_v1 *slot;
  worr_local_action_audit_slot_v1 copied;
  byte_range audit_range;
  byte_range storage_range;
  byte_range output_range;

  if (!audit)
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  if (!local_action_audit_validate(audit, deep))
    return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;
  next = *audit;

  if (!slot_out ||
      !aligned_pointer(slot_out, _Alignof(worr_local_action_audit_slot_v1))) {
    saturating_increment(&next.telemetry.copy_attempts);
    saturating_increment(&next.telemetry.invalid_arguments);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  }
  if (!audit_ranges(audit, &audit_range, &storage_range) ||
      !make_range(slot_out, sizeof(*slot_out), &output_range) ||
      ranges_overlap(output_range, audit_range) ||
      ranges_overlap(output_range, storage_range)) {
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  }

  saturating_increment(&next.telemetry.copy_attempts);
  if (!Worr_CommandIdValidV1(command_id, false)) {
    saturating_increment(&next.telemetry.invalid_arguments);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  }
  if (command_id_compare(command_id, audit->pruned_through) <= 0) {
    saturating_increment(&next.telemetry.stale);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_STALE;
  }

  slot = find_slot_const(audit, command_id);
  if (!slot) {
    saturating_increment(&next.telemetry.not_found);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_NOT_FOUND;
  }
  if (!deep && !slot_validate(slot, audit->pruned_through, true))
    return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;
  copied = *slot;
  saturating_increment(&next.telemetry.copies);
  *audit = next;
  *slot_out = copied;
  return WORR_LOCAL_ACTION_AUDIT_COPIED;
}

worr_local_action_audit_result_v1
Worr_LocalActionAuditCopyV1(worr_local_action_audit_v1 *audit,
                            worr_command_id_v1 command_id,
                            worr_local_action_audit_slot_v1 *slot_out) {
  return local_action_audit_copy(audit, command_id, slot_out, true);
}

worr_local_action_audit_result_v1
Worr_LocalActionAuditCopyV2(worr_local_action_audit_v1 *audit,
                            worr_command_id_v1 command_id,
                            worr_local_action_audit_slot_v1 *slot_out) {
  return local_action_audit_copy(audit, command_id, slot_out, false);
}

static worr_local_action_audit_result_v1
local_action_audit_prune_through(worr_local_action_audit_v1 *audit,
                                 worr_command_id_v1 command_id, bool deep) {
  worr_local_action_audit_v1 next;
  uint32_t read_index;
  uint32_t write_index = 0;
  uint32_t removed = 0;

  if (!audit)
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  if (!local_action_audit_validate(audit, deep))
    return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;

  next = *audit;
  saturating_increment(&next.telemetry.prune_attempts);
  if (!Worr_CommandIdValidV1(command_id, false)) {
    saturating_increment(&next.telemetry.invalid_arguments);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  }
  if (command_id_compare(command_id, audit->pruned_through) <= 0) {
    saturating_increment(&next.telemetry.stale);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_PRUNE_REGRESSION;
  }

  for (read_index = 0; read_index < audit->count; ++read_index) {
    const worr_local_action_audit_slot_v1 *slot =
        slot_at_const(audit, read_index);
    if (command_id_compare(slot->command_id, command_id) <= 0 &&
        (slot->flags & WORR_LOCAL_ACTION_AUDIT_SLOT_CLASSIFIED) == 0) {
      saturating_increment(&next.telemetry.prune_blocks);
      *audit = next;
      return WORR_LOCAL_ACTION_AUDIT_PRUNE_BLOCKED;
    }
    if (command_id_compare(slot->command_id, command_id) <= 0) {
      ++removed;
    }
  }

  saturating_increment(&next.telemetry.prunes);
  saturating_add(&next.telemetry.pruned_slots, removed);
  next.pruned_through = command_id;

  for (read_index = 0; read_index < audit->count; ++read_index) {
    const uint32_t source_index = ring_index(audit, read_index);
    if (command_id_compare(audit->slots[source_index].command_id, command_id) >
        0) {
      const uint32_t destination_index = ring_index(audit, write_index);
      if (destination_index != source_index)
        audit->slots[destination_index] = audit->slots[source_index];
      ++write_index;
    }
  }
  for (read_index = write_index; read_index < audit->count; ++read_index)
    memset(slot_at(audit, read_index), 0, sizeof(*audit->slots));

  next.count = write_index;
  if (next.count == 0)
    next.head = 0;
  *audit = next;
  return WORR_LOCAL_ACTION_AUDIT_PRUNED;
}

worr_local_action_audit_result_v1
Worr_LocalActionAuditPruneThroughV1(worr_local_action_audit_v1 *audit,
                                    worr_command_id_v1 command_id) {
  return local_action_audit_prune_through(audit, command_id, true);
}

worr_local_action_audit_result_v1
Worr_LocalActionAuditPruneThroughV2(worr_local_action_audit_v1 *audit,
                                    worr_command_id_v1 command_id) {
  /* Destructive evidence retirement remains a whole-ring trust boundary. */
  return local_action_audit_prune_through(audit, command_id, true);
}

static worr_local_action_audit_result_v1
local_action_audit_reset(worr_local_action_audit_v1 *audit,
                         uint32_t new_connection_epoch, bool deep) {
  worr_local_action_audit_v1 next;
  size_t storage_size;

  if (!audit)
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  if (!local_action_audit_validate(audit, deep))
    return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;

  next = *audit;
  saturating_increment(&next.telemetry.reset_attempts);
  if (new_connection_epoch == 0) {
    saturating_increment(&next.telemetry.reset_rejections);
    saturating_increment(&next.telemetry.invalid_arguments);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_INVALID_ARGUMENT;
  }
  if (new_connection_epoch <= audit->connection_epoch) {
    saturating_increment(&next.telemetry.reset_rejections);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_RESET_REGRESSION;
  }
  if (audit->generation == UINT32_MAX) {
    saturating_increment(&next.telemetry.reset_rejections);
    *audit = next;
    return WORR_LOCAL_ACTION_AUDIT_GENERATION_EXHAUSTED;
  }
  if (!checked_array_size(audit->capacity, sizeof(*audit->slots),
                          &storage_size)) {
    return WORR_LOCAL_ACTION_AUDIT_INVALID_STATE;
  }

  next.head = 0;
  next.count = 0;
  next.connection_epoch = new_connection_epoch;
  ++next.generation;
  memset(&next.pruned_through, 0, sizeof(next.pruned_through));
  next.next_arrival_serial = 1;
  next.telemetry.unmatched_predicted = 0;
  next.telemetry.unmatched_authoritative = 0;
  saturating_increment(&next.telemetry.resets);

  memset(audit->slots, 0, storage_size);
  *audit = next;
  return WORR_LOCAL_ACTION_AUDIT_RESET;
}

worr_local_action_audit_result_v1
Worr_LocalActionAuditResetV1(worr_local_action_audit_v1 *audit,
                             uint32_t new_connection_epoch) {
  return local_action_audit_reset(audit, new_connection_epoch, true);
}

worr_local_action_audit_result_v1
Worr_LocalActionAuditResetV2(worr_local_action_audit_v1 *audit,
                             uint32_t new_connection_epoch) {
  return local_action_audit_reset(audit, new_connection_epoch, false);
}

static bool local_action_audit_get_telemetry(
    const worr_local_action_audit_v1 *audit,
    worr_local_action_audit_telemetry_v1 *telemetry_out, bool deep) {
  byte_range audit_range;
  byte_range storage_range;
  byte_range output_range;

  if (!telemetry_out ||
      !aligned_pointer(telemetry_out,
                       _Alignof(worr_local_action_audit_telemetry_v1)) ||
      !local_action_audit_validate(audit, deep) ||
      !audit_ranges(audit, &audit_range, &storage_range) ||
      !make_range(telemetry_out, sizeof(*telemetry_out), &output_range) ||
      ranges_overlap(output_range, audit_range) ||
      ranges_overlap(output_range, storage_range)) {
    return false;
  }
  *telemetry_out = audit->telemetry;
  return true;
}

bool Worr_LocalActionAuditGetTelemetryV1(
    const worr_local_action_audit_v1 *audit,
    worr_local_action_audit_telemetry_v1 *telemetry_out) {
  return local_action_audit_get_telemetry(audit, telemetry_out, true);
}

bool Worr_LocalActionAuditGetTelemetryV2(
    const worr_local_action_audit_v1 *audit,
    worr_local_action_audit_telemetry_v1 *telemetry_out) {
  return local_action_audit_get_telemetry(audit, telemetry_out, false);
}
