/* Shared canonical construction coverage for decoded legacy temp entities. */

#include "common/net/legacy_temp_event_candidate.h"

#include "common/intreadwrite.h"
#include "shared/shared.h"
#include "common/math.h"
#include "common/protocol.h"

#include <math.h>
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

static void write_i16(uint8_t *bytes, size_t *cursor, int16_t value)
{
  WL16(bytes + *cursor, (uint16_t)value);
  *cursor += 2u;
}

static void write_i32(uint8_t *bytes, size_t *cursor, int32_t value)
{
  WL32(bytes + *cursor, (uint32_t)value);
  *cursor += 4u;
}

static void write_float(uint8_t *bytes, size_t *cursor, float value)
{
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  WL32(bytes + *cursor, bits);
  *cursor += 4u;
}

static void write_position(uint8_t *bytes, size_t *cursor, float x, float y,
                           float z)
{
  write_float(bytes, cursor, x);
  write_float(bytes, cursor, y);
  write_float(bytes, cursor, z);
}

static int test_exact_raw_decoder(void)
{
  uint8_t raw[64];
  size_t cursor;
  q2proto_svc_temp_entity_t temp;
  q2proto_svc_temp_entity_t before;

  memset(raw, 0, sizeof(raw));
  cursor = 0;
  raw[cursor++] = svc_temp_entity;
  raw[cursor++] = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
  write_position(raw, &cursor, 12.5f, -8.0f, 3.25f);
  raw[cursor++] = 7;
  CHECK(Worr_LegacyTempEventDecodeRawV1(raw, cursor, &temp) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
  CHECK(temp.type == WORR_EVENT_LEGACY_TEMP_GUNSHOT &&
        temp.position1[0] == 12.5f && temp.position1[1] == -8.0f &&
        temp.position1[2] == 3.25f &&
        temp.direction[0] == bytedirs[7][0] &&
        temp.direction[1] == bytedirs[7][1] &&
        temp.direction[2] == bytedirs[7][2]);

  cursor = 0;
  raw[cursor++] = svc_temp_entity;
  raw[cursor++] = WORR_EVENT_LEGACY_TEMP_SPLASH;
  raw[cursor++] = 9;
  write_position(raw, &cursor, 1.0f, 2.0f, 3.0f);
  raw[cursor++] = 42;
  raw[cursor++] = 6;
  CHECK(Worr_LegacyTempEventDecodeRawV1(raw, cursor, &temp) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
  CHECK(temp.count == 9 && temp.color == 6 &&
        temp.position1[2] == 3.0f &&
        temp.direction[0] == bytedirs[42][0]);

  cursor = 0;
  raw[cursor++] = svc_temp_entity;
  raw[cursor++] = WORR_EVENT_LEGACY_TEMP_LIGHTNING;
  write_i16(raw, &cursor, 5);
  write_i16(raw, &cursor, 9);
  write_position(raw, &cursor, 1.0f, 2.0f, 3.0f);
  write_position(raw, &cursor, 4.0f, 5.0f, 6.0f);
  CHECK(Worr_LegacyTempEventDecodeRawV1(raw, cursor, &temp) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
  CHECK(temp.entity1 == 5 && temp.entity2 == 9 &&
        temp.position2[0] == 4.0f && temp.position2[2] == 6.0f);

  cursor = 0;
  raw[cursor++] = svc_temp_entity;
  raw[cursor++] = WORR_EVENT_LEGACY_TEMP_STEAM;
  write_i16(raw, &cursor, 17);
  raw[cursor++] = 3;
  write_position(raw, &cursor, 7.0f, 8.0f, 9.0f);
  raw[cursor++] = 12;
  raw[cursor++] = 200;
  write_i16(raw, &cursor, 64);
  write_i32(raw, &cursor, 500);
  CHECK(Worr_LegacyTempEventDecodeRawV1(raw, cursor, &temp) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
  CHECK(temp.entity1 == 17 && temp.entity2 == 64 && temp.count == 3 &&
        temp.color == 200 && temp.time == 500);

  memset(&temp, 0xa5, sizeof(temp));
  before = temp;
  raw[cursor++] = 0;
  CHECK(Worr_LegacyTempEventDecodeRawV1(raw, cursor, &temp) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&temp, &before, sizeof(temp)) == 0);
  raw[1] = UINT8_MAX;
  CHECK(Worr_LegacyTempEventDecodeRawV1(raw, 2, &temp) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_UNSUPPORTED_SUBTYPE);
  CHECK(memcmp(&temp, &before, sizeof(temp)) == 0);
  return 0;
}

static int test_exact_raw_sequence_decoder(void)
{
  uint8_t raw[64];
  size_t cursor = 0;
  q2proto_svc_temp_entity_t
      temps[WORR_LEGACY_TEMP_EVENT_CANDIDATE_SEQUENCE_MAX];
  q2proto_svc_temp_entity_t
      temps_before[WORR_LEGACY_TEMP_EVENT_CANDIDATE_SEQUENCE_MAX];
  uint32_t count = UINT32_C(0x12345678);
  uint32_t count_before;

  memset(raw, 0, sizeof(raw));
  raw[cursor++] = svc_temp_entity;
  raw[cursor++] = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
  write_position(raw, &cursor, 1.0f, 2.0f, 3.0f);
  raw[cursor++] = 5;
  raw[cursor++] = svc_temp_entity;
  raw[cursor++] = WORR_EVENT_LEGACY_TEMP_LIGHTNING;
  write_i16(raw, &cursor, 4);
  write_i16(raw, &cursor, 7);
  write_position(raw, &cursor, 4.0f, 5.0f, 6.0f);
  write_position(raw, &cursor, 7.0f, 8.0f, 9.0f);

  CHECK(Worr_LegacyTempEventDecodeRawSequenceV1(
            raw, cursor, temps, q_countof(temps), &count) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
  CHECK(count == 2 && temps[0].type == WORR_EVENT_LEGACY_TEMP_GUNSHOT &&
        temps[0].position1[1] == 2.0f && temps[1].type ==
        WORR_EVENT_LEGACY_TEMP_LIGHTNING && temps[1].entity1 == 4 &&
        temps[1].entity2 == 7 && temps[1].position2[2] == 9.0f);

  memset(temps, 0xa5, sizeof(temps));
  memcpy(temps_before, temps, sizeof(temps));
  count = UINT32_C(0x12345678);
  count_before = count;
  CHECK(Worr_LegacyTempEventDecodeRawSequenceV1(
            raw, cursor, temps, 1, &count) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_CAPACITY);
  CHECK(memcmp(temps, temps_before, sizeof(temps)) == 0 &&
        count == count_before);

  raw[cursor++] = svc_temp_entity;
  CHECK(Worr_LegacyTempEventDecodeRawSequenceV1(
            raw, cursor, temps, q_countof(temps), &count) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(temps, temps_before, sizeof(temps)) == 0 &&
        count == count_before);
  return 0;
}

static int test_visual_and_gameplay_shapes(void)
{
  q2proto_svc_temp_entity_t temp;
  worr_event_record_v1 record;
  worr_event_payload_legacy_temp_v1 payload;
  uint32_t source_entity;
  uint32_t subject_entity;

  memset(&temp, 0, sizeof(temp));
  temp.type = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
  temp.position1[0] = 10.0f;
  temp.direction[2] = 1.0f;
  CHECK(Worr_LegacyTempEventCandidateBuildV1(
            &temp, 17, UINT64_C(425000), 64, &record, &source_entity,
            &subject_entity) == WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
  CHECK(record.source_entity.index == WORR_EVENT_NO_ENTITY &&
        record.source_entity.generation == 0 &&
        record.subject_entity.index == WORR_EVENT_NO_ENTITY &&
        record.subject_entity.generation == 0);
  record.source_entity.index = 0;
  record.source_entity.generation = 1;
  CHECK(Worr_EventRecordCandidateValidateV1(&record, 64));
  CHECK(record.source_tick == 17 && record.source_time_us == UINT64_C(425000) &&
        record.event_type == WORR_EVENT_TYPE_VISUAL_EFFECT &&
        source_entity == 0 && subject_entity == WORR_EVENT_NO_ENTITY);
  memcpy(&payload, record.payload, sizeof(payload));
  CHECK(payload.subtype == WORR_EVENT_LEGACY_TEMP_GUNSHOT &&
        (payload.valid_fields & WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1) != 0 &&
        (payload.valid_fields & WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION) != 0 &&
        payload.position1[0] == 10.0f && payload.direction[2] == 1.0f);

  memset(&temp, 0, sizeof(temp));
  temp.type = WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT;
  temp.count = 48;
  CHECK(Worr_LegacyTempEventCandidateBuildV1(
            &temp, 18, UINT64_C(435000), 64, &record, &source_entity,
            &subject_entity) == WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
  memcpy(&payload, record.payload, sizeof(payload));
  CHECK(record.event_type == WORR_EVENT_TYPE_GAMEPLAY_CUE &&
        payload.count_or_amount == 48 && source_entity == 0 &&
        subject_entity == WORR_EVENT_NO_ENTITY);
  return 0;
}

static int test_lineage_indices_and_atomic_rejection(void)
{
  q2proto_svc_temp_entity_t temp;
  worr_event_record_v1 record;
  worr_event_record_v1 record_before;
  uint32_t source_entity = 0;
  uint32_t subject_entity = 0;
  uint32_t source_before;
  uint32_t subject_before;

  memset(&temp, 0, sizeof(temp));
  temp.type = WORR_EVENT_LEGACY_TEMP_LIGHTNING;
  temp.entity1 = 5;
  temp.entity2 = 9;
  CHECK(Worr_LegacyTempEventCandidateBuildV1(
            &temp, 19, UINT64_C(445000), 64, &record, &source_entity,
            &subject_entity) == WORR_LEGACY_TEMP_EVENT_CANDIDATE_OK);
  CHECK(source_entity == 5 && subject_entity == 9 &&
        record.source_entity.index == WORR_EVENT_NO_ENTITY &&
        record.source_entity.generation == 0);
  record.source_entity.index = 5;
  record.source_entity.generation = 1;
  record.subject_entity.index = 9;
  record.subject_entity.generation = 1;
  CHECK(Worr_EventRecordCandidateValidateV1(&record, 64));

  memset(&record, 0xa5, sizeof(record));
  record_before = record;
  source_entity = UINT32_C(0x12345678);
  subject_entity = UINT32_C(0x87654321);
  source_before = source_entity;
  subject_before = subject_entity;
  temp.entity2 = 64;
  CHECK(Worr_LegacyTempEventCandidateBuildV1(
            &temp, 20, UINT64_C(455000), 64, &record, &source_entity,
            &subject_entity) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before && subject_entity == subject_before);

  memset(&temp, 0, sizeof(temp));
  temp.type = UINT8_MAX;
  CHECK(Worr_LegacyTempEventCandidateBuildV1(
            &temp, 20, UINT64_C(455000), 64, &record, &source_entity,
            &subject_entity) ==
        WORR_LEGACY_TEMP_EVENT_CANDIDATE_UNSUPPORTED_SUBTYPE);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before && subject_entity == subject_before);

  temp.type = WORR_EVENT_LEGACY_TEMP_STEAM;
  temp.entity1 = 0;
  CHECK(Worr_LegacyTempEventCandidateBuildV1(
            &temp, 20, UINT64_C(455000), 64, &record, &source_entity,
            &subject_entity) == WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before && subject_entity == subject_before);

  memset(&temp, 0, sizeof(temp));
  temp.type = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
  temp.position1[0] = NAN;
  CHECK(Worr_LegacyTempEventCandidateBuildV1(
            &temp, 20, UINT64_C(455000), 64, &record, &source_entity,
            &subject_entity) == WORR_LEGACY_TEMP_EVENT_CANDIDATE_INVALID_RECORD);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before && subject_entity == subject_before);
  return 0;
}

int main(void)
{
  if (test_exact_raw_decoder() != 0 ||
      test_exact_raw_sequence_decoder() != 0 ||
      test_visual_and_gameplay_shapes() != 0 ||
      test_lineage_indices_and_atomic_rejection() != 0)
    return EXIT_FAILURE;
  puts("legacy temp event candidate tests passed");
  return EXIT_SUCCESS;
}
