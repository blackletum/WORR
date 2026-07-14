/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/rewind_observation.h"

#include <math.h>
#include <string.h>

#define FNV1A64_OFFSET UINT64_C(14695981039346656037)
#define FNV1A64_PRIME UINT64_C(1099511628211)

typedef struct byte_range_s {
  uintptr_t begin;
  uintptr_t end;
} byte_range;

static bool aligned_pointer(const void *pointer, size_t alignment) {
  return pointer && ((uintptr_t)pointer % alignment) == 0;
}

static bool checked_size(uint32_t count, size_t element_size,
                         size_t *size_out) {
  if (!size_out ||
      (element_size != 0 && count > SIZE_MAX / element_size))
    return false;
  *size_out = (size_t)count * element_size;
  return true;
}

static bool make_range(const void *pointer, size_t size, byte_range *out) {
  const uintptr_t begin = (uintptr_t)pointer;
  if (!pointer || !out || size == 0 || begin > UINTPTR_MAX - size)
    return false;
  out->begin = begin;
  out->end = begin + size;
  return true;
}

static bool ranges_overlap(byte_range a, byte_range b) {
  return a.begin < b.end && b.begin < a.end;
}

static bool entity_ref_valid(worr_event_entity_ref_v1 ref) {
  return (ref.index == WORR_EVENT_NO_ENTITY && ref.generation == 0) ||
         (ref.index != WORR_EVENT_NO_ENTITY && ref.generation != 0);
}

static bool command_id_valid_or_absent(worr_command_id_v1 id) {
  return (id.epoch == 0 && id.sequence == 0) ||
         (id.epoch != 0 && id.sequence != 0);
}

static bool snapshot_id_valid_or_absent(worr_snapshot_id_v2 id) {
  return (id.epoch == 0 && id.sequence == 0) ||
         (id.epoch != 0 && id.sequence != 0);
}

static void saturating_increment(uint64_t *value) {
  if (*value != UINT64_MAX)
    ++*value;
}

static void saturating_add(uint64_t *value, uint64_t amount) {
  if (*value > UINT64_MAX - amount)
    *value = UINT64_MAX;
  else
    *value += amount;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value) {
  unsigned int shift;
  for (shift = 0; shift < 32; shift += 8) {
    hash ^= (uint8_t)(value >> shift);
    hash *= FNV1A64_PRIME;
  }
  return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value) {
  unsigned int shift;
  for (shift = 0; shift < 64; shift += 8) {
    hash ^= (uint8_t)(value >> shift);
    hash *= FNV1A64_PRIME;
  }
  return hash;
}

static uint64_t hash_float(uint64_t hash, float value) {
  uint32_t bits;
  if (value == 0.0f)
    value = 0.0f;
  memcpy(&bits, &value, sizeof(bits));
  return hash_u32(hash, bits);
}

bool Worr_RewindObservationInitV1(worr_rewind_observation_v1 *observation,
                                  uint32_t weapon_policy) {
  worr_rewind_observation_v1 output;
  if (!aligned_pointer(observation, _Alignof(worr_rewind_observation_v1)) ||
      weapon_policy >= WORR_REWIND_WEAPON_POLICY_COUNT) {
    return false;
  }
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_OBSERVATION_VERSION;
  output.weapon_policy = weapon_policy;
  output.hit_entity.index = WORR_EVENT_NO_ENTITY;
  output.trace_fraction = 1.0f;
  *observation = output;
  return true;
}

bool Worr_RewindObservationValidateV1(
    const worr_rewind_observation_v1 *observation) {
  const uint32_t guard_flags =
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED |
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
  if (!aligned_pointer(observation, _Alignof(worr_rewind_observation_v1)) ||
      observation->struct_size != sizeof(*observation) ||
      observation->schema_version != WORR_REWIND_OBSERVATION_VERSION ||
      observation->weapon_policy >= WORR_REWIND_WEAPON_POLICY_COUNT ||
      observation->path > WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD ||
      observation->outcome > WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED ||
      observation->fallback_reason >
          WORR_REWIND_OBSERVATION_FALLBACK_CURRENT_WORLD_BLOCKED ||
      (observation->flags & ~WORR_REWIND_OBSERVATION_KNOWN_FLAGS) != 0 ||
      observation->policy_reason > WORR_REWIND_POLICY_REJECT_MAPPING_PROOF ||
      observation->query_reason > WORR_REWIND_QUERY_INVALID ||
      !command_id_valid_or_absent(observation->command_id) ||
      !snapshot_id_valid_or_absent(observation->snapshot_id) ||
      !snapshot_id_valid_or_absent(observation->source_snapshot_id) ||
      !entity_ref_valid(observation->hit_entity) ||
      !isfinite(observation->trace_fraction) ||
      observation->trace_fraction < 0.0f ||
      observation->trace_fraction > 1.0f || observation->reserved0 != 0 ||
      observation->reserved1 != 0) {
    return false;
  }
  if ((observation->flags & WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT) != 0 &&
      observation->path != WORR_REWIND_OBSERVATION_PATH_CANONICAL) {
    return false;
  }
  if ((observation->flags & WORR_REWIND_OBSERVATION_POLICY_ACCEPTED) != 0 &&
      observation->path != WORR_REWIND_OBSERVATION_PATH_CANONICAL &&
      observation->path != WORR_REWIND_OBSERVATION_PATH_LEGACY) {
    return false;
  }
  if ((observation->flags & WORR_REWIND_OBSERVATION_HISTORICAL_SCENE) != 0 &&
      (observation->flags & WORR_REWIND_OBSERVATION_HISTORICAL_QUERY) == 0) {
    return false;
  }
  if ((observation->flags & guard_flags) ==
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED) {
    return false;
  }
  if ((observation->flags & WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED) !=
      0) {
    if (observation->authoritative_hash_before == 0 ||
        observation->authoritative_hash_after == 0) {
      return false;
    }
    if ((observation->flags &
         WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED) != 0 &&
        observation->authoritative_hash_before !=
            observation->authoritative_hash_after) {
      return false;
    }
  } else if (observation->authoritative_hash_before != 0 ||
             observation->authoritative_hash_after != 0) {
    return false;
  }
  return true;
}

bool Worr_RewindObservationHashV1(
    const worr_rewind_observation_v1 *observation, uint64_t *hash_out) {
  byte_range observation_range;
  byte_range output_range;
  uint64_t hash = FNV1A64_OFFSET;
  if (!hash_out || !aligned_pointer(hash_out, _Alignof(uint64_t)) ||
      !Worr_RewindObservationValidateV1(observation) ||
      !make_range(observation, sizeof(*observation), &observation_range) ||
      !make_range(hash_out, sizeof(*hash_out), &output_range) ||
      ranges_overlap(observation_range, output_range)) {
    return false;
  }
  hash = hash_u64(hash, observation->observation_sequence);
  hash = hash_u32(hash, observation->weapon_policy);
  hash = hash_u32(hash, observation->path);
  hash = hash_u32(hash, observation->outcome);
  hash = hash_u32(hash, observation->fallback_reason);
  hash = hash_u32(hash, observation->flags);
  hash = hash_u32(hash, observation->policy_reason);
  hash = hash_u32(hash, observation->query_reason);
  hash = hash_u32(hash, observation->candidate_count);
  hash = hash_u32(hash, observation->command_id.epoch);
  hash = hash_u32(hash, observation->command_id.sequence);
  hash = hash_u32(hash, observation->snapshot_id.epoch);
  hash = hash_u32(hash, observation->snapshot_id.sequence);
  hash = hash_u32(hash, observation->source_snapshot_id.epoch);
  hash = hash_u32(hash, observation->source_snapshot_id.sequence);
  hash = hash_u32(hash, observation->hit_entity.index);
  hash = hash_u32(hash, observation->hit_entity.generation);
  hash = hash_u64(hash, observation->requested_time_us);
  hash = hash_u64(hash, observation->mapped_time_us);
  hash = hash_u64(hash, observation->applied_time_us);
  hash = hash_u64(hash, observation->mapping_error_bound_us);
  hash = hash_u64(hash, observation->scene_hash);
  hash = hash_u64(hash, observation->authoritative_hash_before);
  hash = hash_u64(hash, observation->authoritative_hash_after);
  hash = hash_u64(hash, observation->duration_ns);
  hash = hash_float(hash, observation->trace_fraction);
  *hash_out = hash;
  return true;
}

static bool journal_storage_range(
    const worr_rewind_observation_journal_v1 *journal, byte_range *out) {
  size_t size;
  return journal && journal->slots && journal->capacity != 0 &&
         checked_size(journal->capacity, sizeof(*journal->slots), &size) &&
         make_range(journal->slots, size, out);
}

static const worr_rewind_observation_v1 *journal_at(
    const worr_rewind_observation_journal_v1 *journal,
    uint32_t chronological_index) {
  const uint32_t index = (uint32_t)(
      ((uint64_t)journal->head + chronological_index) % journal->capacity);
  return &journal->slots[index];
}

bool Worr_RewindObservationJournalInitV1(
    worr_rewind_observation_journal_v1 *journal,
    worr_rewind_observation_v1 *storage, uint32_t capacity) {
  worr_rewind_observation_journal_v1 output;
  byte_range journal_range;
  byte_range storage_range;
  size_t storage_size;
  if (!aligned_pointer(journal,
                       _Alignof(worr_rewind_observation_journal_v1)) ||
      !aligned_pointer(storage, _Alignof(worr_rewind_observation_v1)) ||
      capacity == 0 ||
      !checked_size(capacity, sizeof(*storage), &storage_size) ||
      !make_range(journal, sizeof(*journal), &journal_range) ||
      !make_range(storage, storage_size, &storage_range) ||
      ranges_overlap(journal_range, storage_range)) {
    return false;
  }
  memset(storage, 0, storage_size);
  memset(&output, 0, sizeof(output));
  output.struct_size = sizeof(output);
  output.schema_version = WORR_REWIND_OBSERVATION_VERSION;
  output.slots = storage;
  output.capacity = capacity;
  output.next_sequence = 1;
  *journal = output;
  return true;
}

bool Worr_RewindObservationJournalValidateV1(
    const worr_rewind_observation_journal_v1 *journal) {
  byte_range journal_range;
  byte_range storage_range;
  uint64_t previous_sequence = 0;
  uint32_t index;
  if (!aligned_pointer(journal,
                       _Alignof(worr_rewind_observation_journal_v1)) ||
      journal->struct_size != sizeof(*journal) ||
      journal->schema_version != WORR_REWIND_OBSERVATION_VERSION ||
      !aligned_pointer(journal->slots, _Alignof(worr_rewind_observation_v1)) ||
      journal->capacity == 0 || journal->head >= journal->capacity ||
      journal->count > journal->capacity ||
      (journal->count < journal->capacity && journal->head != 0) ||
      journal->reserved0 != 0 || journal->next_sequence == 0 ||
      !make_range(journal, sizeof(*journal), &journal_range) ||
      !journal_storage_range(journal, &storage_range) ||
      ranges_overlap(journal_range, storage_range) ||
      journal->telemetry.observations < journal->count) {
    return false;
  }
  for (index = 0; index < journal->count; ++index) {
    const worr_rewind_observation_v1 *record = journal_at(journal, index);
    if (!Worr_RewindObservationValidateV1(record) ||
        record->observation_sequence == 0 ||
        (previous_sequence != 0 &&
         record->observation_sequence <= previous_sequence)) {
      return false;
    }
    previous_sequence = record->observation_sequence;
  }
  return journal->count == 0 || previous_sequence < journal->next_sequence;
}

static void update_telemetry(
    worr_rewind_observation_telemetry_v1 *telemetry,
    const worr_rewind_observation_v1 *record, bool overwritten) {
  saturating_increment(&telemetry->observations);
  if (overwritten)
    saturating_increment(&telemetry->overwritten);
  if (record->path == WORR_REWIND_OBSERVATION_PATH_CANONICAL)
    saturating_increment(&telemetry->canonical);
  else if (record->path == WORR_REWIND_OBSERVATION_PATH_LEGACY)
    saturating_increment(&telemetry->legacy);
  else if (record->path == WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD)
    saturating_increment(&telemetry->current_world);
  if (record->outcome == WORR_REWIND_OBSERVATION_OUTCOME_POLICY_REJECTED)
    saturating_increment(&telemetry->policy_rejected);
  if ((record->flags & WORR_REWIND_OBSERVATION_HISTORICAL_QUERY) != 0)
    saturating_increment(&telemetry->historical_queries);
  if (record->outcome == WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT)
    saturating_increment(&telemetry->historical_hits);
  if (record->outcome == WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_MISS)
    saturating_increment(&telemetry->historical_misses);
  if ((record->flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) != 0)
    saturating_increment(&telemetry->current_fallbacks);
  if (record->fallback_reason ==
      WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS)
    saturating_increment(&telemetry->history_misses);
  if (record->fallback_reason ==
          WORR_REWIND_OBSERVATION_FALLBACK_SCENE_REJECTED ||
      record->fallback_reason ==
          WORR_REWIND_OBSERVATION_FALLBACK_IGNORE_REJECTED ||
      record->fallback_reason ==
          WORR_REWIND_OBSERVATION_FALLBACK_TRACE_VIEW_REJECTED) {
    saturating_increment(&telemetry->scene_rejected);
  }
  if ((record->flags &
       WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED) != 0) {
    saturating_increment(&telemetry->authority_guard_checks);
    if ((record->flags &
         WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED) == 0) {
      saturating_increment(&telemetry->authority_mutations);
    }
  }
  saturating_add(&telemetry->duration_total_ns, record->duration_ns);
  if (record->duration_ns > telemetry->duration_max_ns)
    telemetry->duration_max_ns = record->duration_ns;
}

bool Worr_RewindObservationJournalAppendV1(
    worr_rewind_observation_journal_v1 *journal,
    const worr_rewind_observation_v1 *observation,
    uint64_t *assigned_sequence_out) {
  worr_rewind_observation_journal_v1 next;
  worr_rewind_observation_v1 record;
  byte_range journal_range;
  byte_range storage_range;
  byte_range observation_range;
  byte_range output_range;
  uint32_t index;
  bool overwritten;
  if (!assigned_sequence_out ||
      !aligned_pointer(assigned_sequence_out, _Alignof(uint64_t)) ||
      !Worr_RewindObservationJournalValidateV1(journal) ||
      !Worr_RewindObservationValidateV1(observation) ||
      observation->observation_sequence != 0 ||
      journal->next_sequence == UINT64_MAX ||
      !make_range(journal, sizeof(*journal), &journal_range) ||
      !journal_storage_range(journal, &storage_range) ||
      !make_range(observation, sizeof(*observation), &observation_range) ||
      !make_range(assigned_sequence_out, sizeof(*assigned_sequence_out),
                  &output_range) ||
      ranges_overlap(journal_range, observation_range) ||
      ranges_overlap(storage_range, observation_range) ||
      ranges_overlap(journal_range, output_range) ||
      ranges_overlap(storage_range, output_range) ||
      ranges_overlap(observation_range, output_range)) {
    return false;
  }
  next = *journal;
  record = *observation;
  record.observation_sequence = next.next_sequence++;
  overwritten = next.count == next.capacity;
  if (overwritten) {
    index = next.head;
    next.head = (next.head + 1u) % next.capacity;
  } else {
    index = (uint32_t)(((uint64_t)next.head + next.count) % next.capacity);
    ++next.count;
  }
  update_telemetry(&next.telemetry, &record, overwritten);
  journal->slots[index] = record;
  *journal = next;
  *assigned_sequence_out = record.observation_sequence;
  return true;
}

bool Worr_RewindObservationJournalCopyV1(
    const worr_rewind_observation_journal_v1 *journal,
    worr_rewind_observation_v1 *records_out, uint32_t records_capacity,
    uint32_t *record_count_out) {
  byte_range journal_range;
  byte_range storage_range;
  byte_range output_range;
  byte_range count_range;
  size_t output_size;
  uint32_t index;
  if (!record_count_out ||
      !aligned_pointer(record_count_out, _Alignof(uint32_t)) ||
      !Worr_RewindObservationJournalValidateV1(journal) ||
      records_capacity < journal->count ||
      (journal->count != 0 &&
       !aligned_pointer(records_out, _Alignof(worr_rewind_observation_v1))) ||
      !make_range(journal, sizeof(*journal), &journal_range) ||
      !journal_storage_range(journal, &storage_range) ||
      !make_range(record_count_out, sizeof(*record_count_out), &count_range) ||
      ranges_overlap(journal_range, count_range) ||
      ranges_overlap(storage_range, count_range)) {
    return false;
  }
  if (journal->count != 0) {
    if (!checked_size(journal->count, sizeof(*records_out), &output_size) ||
        !make_range(records_out, output_size, &output_range) ||
        ranges_overlap(journal_range, output_range) ||
        ranges_overlap(storage_range, output_range) ||
        ranges_overlap(output_range, count_range)) {
      return false;
    }
  }
  for (index = 0; index < journal->count; ++index)
    records_out[index] = *journal_at(journal, index);
  *record_count_out = journal->count;
  return true;
}
