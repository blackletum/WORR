/* Shared canonical construction coverage for decoded legacy spatial audio. */

#include "common/net/legacy_spatial_audio_event_candidate.h"

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

static int test_entity_and_world_source_shapes(void)
{
  q2proto_sound_t sound;
  worr_event_record_v1 record;
  worr_event_payload_spatial_audio_v1 payload;
  uint32_t source_entity;

  memset(&sound, 0, sizeof(sound));
  sound.index = 23;
  sound.has_entity_channel = true;
  sound.entity = 5;
  sound.channel = 3;
  sound.has_position = true;
  sound.pos[1] = 12.5f;
  sound.volume = 0.75f;
  sound.attenuation = 1.0f;
  sound.timeofs = 0.125f;
  CHECK(Worr_LegacySpatialAudioEventCandidateBuildV1(
            &sound, 17, UINT64_C(425000), 64, &record, &source_entity) ==
        WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_OK);
  CHECK(source_entity == 5 &&
        record.source_entity.index == WORR_EVENT_NO_ENTITY &&
        record.source_entity.generation == 0);
  record.source_entity.index = 5;
  record.source_entity.generation = 2;
  CHECK(Worr_EventRecordCandidateValidateV1(&record, 64));
  memcpy(&payload, record.payload, sizeof(payload));
  CHECK(record.source_tick == 17 && record.source_time_us == UINT64_C(425000) &&
        payload.asset_id == 23 && payload.raw_entity == 5 &&
        payload.channel == 3 &&
        (payload.flags & WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL) != 0 &&
        (payload.flags & WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION) != 0 &&
        payload.origin[1] == 12.5f);

  memset(&sound, 0, sizeof(sound));
  sound.index = 24;
  sound.volume = 1.0f;
  sound.attenuation = 0.0f;
  CHECK(Worr_LegacySpatialAudioEventCandidateBuildV1(
            &sound, 18, UINT64_C(435000), 64, &record, &source_entity) ==
        WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_OK);
  CHECK(source_entity == 0);
  record.source_entity.index = 0;
  record.source_entity.generation = 1;
  CHECK(Worr_EventRecordCandidateValidateV1(&record, 64));
  memcpy(&payload, record.payload, sizeof(payload));
  CHECK(payload.flags == 0 && payload.raw_entity == WORR_EVENT_NO_ENTITY &&
        payload.channel == 0 && payload.origin[0] == 0.0f);
  return 0;
}

static int test_atomic_rejection(void)
{
  q2proto_sound_t sound;
  worr_event_record_v1 record;
  worr_event_record_v1 record_before;
  uint32_t source_entity = UINT32_C(0x12345678);
  uint32_t source_before = source_entity;

  memset(&record, 0xa5, sizeof(record));
  record_before = record;
  memset(&sound, 0, sizeof(sound));
  sound.index = 1;
  sound.has_entity_channel = true;
  sound.entity = 64;
  CHECK(Worr_LegacySpatialAudioEventCandidateBuildV1(
            &sound, 19, UINT64_C(445000), 64, &record, &source_entity) ==
        WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_ENTITY_OUT_OF_RANGE);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before);

  sound.entity = 1;
  sound.channel = 8;
  CHECK(Worr_LegacySpatialAudioEventCandidateBuildV1(
            &sound, 19, UINT64_C(445000), 64, &record, &source_entity) ==
        WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_RECORD);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before);

  sound.channel = 1;
  sound.volume = NAN;
  CHECK(Worr_LegacySpatialAudioEventCandidateBuildV1(
            &sound, 19, UINT64_C(445000), 64, &record, &source_entity) ==
        WORR_LEGACY_SPATIAL_AUDIO_EVENT_CANDIDATE_INVALID_RECORD);
  CHECK(memcmp(&record, &record_before, sizeof(record)) == 0 &&
        source_entity == source_before);
  return 0;
}

int main(void)
{
  if (test_entity_and_world_source_shapes() != 0 ||
      test_atomic_rejection() != 0)
    return EXIT_FAILURE;
  puts("legacy spatial audio event candidate tests passed");
  return EXIT_SUCCESS;
}
