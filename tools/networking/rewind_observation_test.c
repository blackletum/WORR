#include "common/net/rewind_observation.h"

#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,       \
              #expression);                                                    \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static worr_rewind_observation_v1 make_observation(uint32_t weapon,
                                                    uint32_t outcome,
                                                    uint64_t duration_ns) {
  worr_rewind_observation_v1 value;
  CHECK(Worr_RewindObservationInitV1(&value, weapon));
  value.path = WORR_REWIND_OBSERVATION_PATH_CANONICAL;
  value.outcome = outcome;
  value.flags = WORR_REWIND_OBSERVATION_MASTER_ENABLED |
                WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT |
                WORR_REWIND_OBSERVATION_POLICY_ACCEPTED |
                WORR_REWIND_OBSERVATION_HISTORICAL_QUERY |
                WORR_REWIND_OBSERVATION_HISTORICAL_SCENE |
                WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED |
                WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
  value.policy_reason = WORR_REWIND_POLICY_EXACT;
  value.query_reason = WORR_REWIND_QUERY_INTERPOLATED;
  value.candidate_count = 2;
  value.command_id.epoch = 5;
  value.command_id.sequence = 7;
  value.snapshot_id.epoch = 3;
  value.snapshot_id.sequence = 11;
  value.source_snapshot_id.epoch = 3;
  value.source_snapshot_id.sequence = 10;
  value.hit_entity.index = 2;
  value.hit_entity.generation = 9;
  value.requested_time_us = 900000;
  value.mapped_time_us = 900000;
  value.applied_time_us = 900000;
  value.scene_hash = UINT64_C(0x1234);
  value.authoritative_hash_before = UINT64_C(0x9876);
  value.authoritative_hash_after = UINT64_C(0x9876);
  value.duration_ns = duration_ns;
  value.trace_fraction = 0.5f;
  return value;
}

static void test_record(void) {
  worr_rewind_observation_v1 value = make_observation(
      WORR_REWIND_WEAPON_MACHINEGUN,
      WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT, 100);
  worr_rewind_observation_v1 before;
  uint64_t hash = 0;
  uint64_t hash_again = 0;

  CHECK(Worr_RewindObservationValidateV1(&value));
  CHECK(Worr_RewindObservationHashV1(&value, &hash));
  CHECK(hash != 0);
  CHECK(Worr_RewindObservationHashV1(&value, &hash_again));
  CHECK(hash == hash_again);

  before = value;
  value.flags &= ~(uint32_t)WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
  value.authoritative_hash_after ^= 1;
  CHECK(Worr_RewindObservationValidateV1(&value));
  CHECK(Worr_RewindObservationHashV1(&value, &hash_again));
  CHECK(hash != hash_again);

  value = before;
  value.authoritative_hash_after ^= 1;
  CHECK(!Worr_RewindObservationValidateV1(&value));
  value = before;
  value.flags &= ~(uint32_t)WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED;
  CHECK(!Worr_RewindObservationValidateV1(&value));
  value = before;
  value.weapon_policy = WORR_REWIND_WEAPON_POLICY_COUNT;
  CHECK(!Worr_RewindObservationValidateV1(&value));
}

static void test_journal(void) {
  worr_rewind_observation_journal_v1 journal;
  worr_rewind_observation_v1 storage[2];
  worr_rewind_observation_v1 copied[2];
  worr_rewind_observation_v1 first = make_observation(
      WORR_REWIND_WEAPON_SHOTGUN,
      WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_MISS, 10);
  worr_rewind_observation_v1 second = make_observation(
      WORR_REWIND_WEAPON_RAILGUN,
      WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT, 20);
  worr_rewind_observation_v1 third = make_observation(
      WORR_REWIND_WEAPON_THUNDERBOLT,
      WORR_REWIND_OBSERVATION_OUTCOME_HISTORY_MISS, 30);
  uint64_t sequence = 0;
  uint32_t count = 0;

  third.flags &= ~(uint32_t)(WORR_REWIND_OBSERVATION_HISTORICAL_QUERY |
                             WORR_REWIND_OBSERVATION_HISTORICAL_SCENE);
  third.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
  third.fallback_reason = WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS;
  third.query_reason = WORR_REWIND_QUERY_MISS_EMPTY;
  third.candidate_count = 0;
  third.hit_entity.index = WORR_EVENT_NO_ENTITY;
  third.hit_entity.generation = 0;
  third.trace_fraction = 1.0f;

  CHECK(Worr_RewindObservationJournalInitV1(&journal, storage, 2));
  CHECK(Worr_RewindObservationJournalValidateV1(&journal));
  CHECK(Worr_RewindObservationJournalAppendV1(&journal, &first, &sequence));
  CHECK(sequence == 1);
  CHECK(Worr_RewindObservationJournalAppendV1(&journal, &second, &sequence));
  CHECK(sequence == 2);
  CHECK(Worr_RewindObservationJournalAppendV1(&journal, &third, &sequence));
  CHECK(sequence == 3);
  CHECK(journal.count == 2 && journal.head == 1);
  CHECK(journal.telemetry.observations == 3);
  CHECK(journal.telemetry.overwritten == 1);
  CHECK(journal.telemetry.canonical == 3);
  CHECK(journal.telemetry.historical_queries == 2);
  CHECK(journal.telemetry.historical_hits == 1);
  CHECK(journal.telemetry.historical_misses == 1);
  CHECK(journal.telemetry.current_fallbacks == 1);
  CHECK(journal.telemetry.history_misses == 1);
  CHECK(journal.telemetry.authority_guard_checks == 3);
  CHECK(journal.telemetry.authority_mutations == 0);
  CHECK(journal.telemetry.duration_total_ns == 60);
  CHECK(journal.telemetry.duration_max_ns == 30);
  CHECK(Worr_RewindObservationJournalValidateV1(&journal));
  CHECK(Worr_RewindObservationJournalCopyV1(&journal, copied, 2, &count));
  CHECK(count == 2);
  CHECK(copied[0].observation_sequence == 2);
  CHECK(copied[1].observation_sequence == 3);

  first.observation_sequence = 9;
  CHECK(!Worr_RewindObservationJournalAppendV1(&journal, &first, &sequence));
  first.observation_sequence = 0;
  CHECK(!Worr_RewindObservationJournalAppendV1(
      &journal, &first, (uint64_t *)(void *)storage));
  CHECK(!Worr_RewindObservationJournalCopyV1(
      &journal, storage, 2, (uint32_t *)(void *)storage));
}

int main(void) {
  test_record();
  test_journal();
  if (failures != 0) {
    fprintf(stderr, "rewind observation: %d failure(s)\n", failures);
    return 1;
  }
  puts("rewind observation: deterministic journal checks passed");
  return 0;
}
