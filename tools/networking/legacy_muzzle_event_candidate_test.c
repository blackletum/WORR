/* Shared canonical construction coverage for decoded legacy muzzle flashes. */

#include "common/net/legacy_muzzle_event_candidate.h"
#include "shared/shared.h"
#include "common/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,      \
              #condition);                                                     \
      return 1;                                                                 \
    }                                                                           \
  } while (0)

static int test_player_and_monster_muzzles(void)
{
  q2proto_svc_muzzleflash_t muzzleflash;
  worr_event_record_v1 record;
  worr_event_payload_muzzle_v1 payload;
  uint32_t source_entity;

  memset(&muzzleflash, 0, sizeof(muzzleflash));
  muzzleflash.entity = 5;
  muzzleflash.weapon = WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN;
  muzzleflash.silenced = true;
  CHECK(Worr_LegacyMuzzleEventCandidateBuildV1(
            &muzzleflash, WORR_EVENT_MUZZLE_FAMILY_PLAYER, 17,
            UINT64_C(425000), 64, &record, &source_entity) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK);
  CHECK(source_entity == 5 &&
        record.source_entity.index == WORR_EVENT_NO_ENTITY &&
        record.source_entity.generation == 0);
  record.source_entity.index = 5;
  record.source_entity.generation = 2;
  CHECK(Worr_EventRecordCandidateValidateV1(&record, 64));
  memcpy(&payload, record.payload, sizeof(payload));
  CHECK(record.source_tick == 17 && record.source_time_us == UINT64_C(425000) &&
        record.event_type == WORR_EVENT_TYPE_WEAPON_FIRE &&
        payload.family == WORR_EVENT_MUZZLE_FAMILY_PLAYER &&
        payload.flash_id == WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN &&
        payload.flags == WORR_EVENT_MUZZLE_FLAG_SILENCED);

  muzzleflash.weapon = WORR_EVENT_PLAYER_MUZZLE_LOGIN;
  muzzleflash.silenced = false;
  CHECK(Worr_LegacyMuzzleEventCandidateBuildV1(
            &muzzleflash, WORR_EVENT_MUZZLE_FAMILY_PLAYER, 18,
            UINT64_C(435000), 64, &record, &source_entity) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK);
  record.source_entity.index = 5;
  record.source_entity.generation = 2;
  CHECK(record.event_type == WORR_EVENT_TYPE_STATE_CHANGE &&
        Worr_EventRecordCandidateValidateV1(&record, 64));

  muzzleflash.entity = 8;
  muzzleflash.weapon = WORR_EVENT_MONSTER_MUZZLE_LAST;
  CHECK(Worr_LegacyMuzzleEventCandidateBuildV1(
            &muzzleflash, WORR_EVENT_MUZZLE_FAMILY_MONSTER, 19,
            UINT64_C(445000), 64, &record, &source_entity) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK);
  record.source_entity.index = 8;
  record.source_entity.generation = 3;
  memcpy(&payload, record.payload, sizeof(payload));
  CHECK(record.event_type == WORR_EVENT_TYPE_WEAPON_FIRE &&
        payload.family == WORR_EVENT_MUZZLE_FAMILY_MONSTER &&
        payload.flash_id == WORR_EVENT_MONSTER_MUZZLE_LAST &&
        payload.flags == 0 && Worr_EventRecordCandidateValidateV1(&record, 64));
  return 0;
}

static int test_exact_raw_decode(void)
{
  const uint8_t player[] = {svc_muzzleflash, 5, 0,
                            WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN | MZ_SILENCED};
  const uint8_t monster[] = {svc_muzzleflash2, 8, 0, 19};
  const uint8_t monster3[] = {svc_muzzleflash3, 9, 0,
                              WORR_EVENT_MONSTER_MUZZLE_LAST & 0xff,
                              WORR_EVENT_MONSTER_MUZZLE_LAST >> 8};
  const uint8_t combined[] = {svc_muzzleflash, 5, 0,
                              WORR_EVENT_PLAYER_MUZZLE_BLASTER, svc_nop};
  q2proto_svc_muzzleflash_t muzzleflash;
  q2proto_svc_muzzleflash_t before;
  uint32_t family = UINT32_C(0xdeadbeef);
  uint32_t family_before;

  CHECK(Worr_LegacyMuzzleEventDecodeRawV1(
            player, sizeof(player), &muzzleflash, &family) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK);
  CHECK(family == WORR_EVENT_MUZZLE_FAMILY_PLAYER && muzzleflash.entity == 5 &&
        muzzleflash.weapon == WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN &&
        muzzleflash.silenced);

  CHECK(Worr_LegacyMuzzleEventDecodeRawV1(
            monster, sizeof(monster), &muzzleflash, &family) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK);
  CHECK(family == WORR_EVENT_MUZZLE_FAMILY_MONSTER && muzzleflash.entity == 8 &&
        muzzleflash.weapon == 19 && !muzzleflash.silenced);

  CHECK(Worr_LegacyMuzzleEventDecodeRawV1(
            monster3, sizeof(monster3), &muzzleflash, &family) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK);
  CHECK(family == WORR_EVENT_MUZZLE_FAMILY_MONSTER && muzzleflash.entity == 9 &&
        muzzleflash.weapon == WORR_EVENT_MONSTER_MUZZLE_LAST &&
        !muzzleflash.silenced);

  before = muzzleflash;
  family_before = family;
  CHECK(Worr_LegacyMuzzleEventDecodeRawV1(
            combined, sizeof(combined), &muzzleflash, &family) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&muzzleflash, &before, sizeof(muzzleflash)) == 0 &&
        family == family_before);
  return 0;
}

static int test_exact_raw_sequence_decode(void)
{
  const uint8_t sequence[] = {
      svc_muzzleflash, 5, 0,
      WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN | MZ_SILENCED,
      svc_muzzleflash3, 9, 0, WORR_EVENT_MONSTER_MUZZLE_LAST & 0xff,
      WORR_EVENT_MONSTER_MUZZLE_LAST >> 8};
  uint8_t malformed[sizeof(sequence) + 1];
  q2proto_svc_muzzleflash_t
      muzzleflashes[WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX];
  q2proto_svc_muzzleflash_t
      muzzleflashes_before[WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX];
  uint32_t families[WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX];
  uint32_t families_before[WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX];
  uint32_t count = UINT32_C(0x12345678);
  uint32_t count_before;

  CHECK(Worr_LegacyMuzzleEventDecodeRawSequenceV1(
            sequence, sizeof(sequence), muzzleflashes, families,
            WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX, &count) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_OK);
  CHECK(count == 2 && muzzleflashes[0].entity == 5 &&
        muzzleflashes[0].silenced &&
        families[0] == WORR_EVENT_MUZZLE_FAMILY_PLAYER &&
        muzzleflashes[1].entity == 9 &&
        muzzleflashes[1].weapon == WORR_EVENT_MONSTER_MUZZLE_LAST &&
        families[1] == WORR_EVENT_MUZZLE_FAMILY_MONSTER);

  memset(muzzleflashes, 0xa5, sizeof(muzzleflashes));
  memset(families, 0xa5, sizeof(families));
  memcpy(muzzleflashes_before, muzzleflashes, sizeof(muzzleflashes));
  memcpy(families_before, families, sizeof(families));
  count = UINT32_C(0x12345678);
  count_before = count;
  CHECK(Worr_LegacyMuzzleEventDecodeRawSequenceV1(
            sequence, sizeof(sequence), muzzleflashes, families, 1, &count) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_CAPACITY);
  CHECK(memcmp(muzzleflashes, muzzleflashes_before, sizeof(muzzleflashes)) == 0 &&
        memcmp(families, families_before, sizeof(families)) == 0 &&
        count == count_before);

  memcpy(malformed, sequence, sizeof(sequence));
  malformed[sizeof(sequence)] = svc_muzzleflash2;
  CHECK(Worr_LegacyMuzzleEventDecodeRawSequenceV1(
            malformed, sizeof(malformed), muzzleflashes, families,
            WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_SEQUENCE_MAX, &count) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(muzzleflashes, muzzleflashes_before, sizeof(muzzleflashes)) == 0 &&
        memcmp(families, families_before, sizeof(families)) == 0 &&
        count == count_before);
  return 0;
}

static int test_atomic_rejection(void)
{
  q2proto_svc_muzzleflash_t muzzleflash;
  worr_event_record_v1 record;
  worr_event_record_v1 record_before;
  uint32_t source_entity = UINT32_C(0x12345678);
  const uint32_t source_before = source_entity;

  memset(&record, 0xa5, sizeof(record));
  record_before = record;
  memset(&muzzleflash, 0, sizeof(muzzleflash));
  muzzleflash.entity = 64;
  muzzleflash.weapon = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
  CHECK(Worr_LegacyMuzzleEventCandidateBuildV1(
            &muzzleflash, WORR_EVENT_MUZZLE_FAMILY_PLAYER, 19,
            UINT64_C(445000), 64, &record, &source_entity) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before);

  muzzleflash.entity = 1;
  muzzleflash.weapon = WORR_EVENT_PLAYER_MUZZLE_RESERVED_FIRST;
  CHECK(Worr_LegacyMuzzleEventCandidateBuildV1(
            &muzzleflash, WORR_EVENT_MUZZLE_FAMILY_PLAYER, 19,
            UINT64_C(445000), 64, &record, &source_entity) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before);

  muzzleflash.weapon = WORR_EVENT_MONSTER_MUZZLE_FIRST;
  muzzleflash.silenced = true;
  CHECK(Worr_LegacyMuzzleEventCandidateBuildV1(
            &muzzleflash, WORR_EVENT_MUZZLE_FAMILY_MONSTER, 19,
            UINT64_C(445000), 64, &record, &source_entity) ==
        WORR_LEGACY_MUZZLE_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before);
  return 0;
}

int main(void)
{
  if (test_player_and_monster_muzzles() != 0 || test_exact_raw_decode() != 0 ||
      test_exact_raw_sequence_decode() != 0 ||
      test_atomic_rejection() != 0)
    return EXIT_FAILURE;
  puts("legacy muzzle event candidate tests passed");
  return EXIT_SUCCESS;
}
