/* Bounded mixed direct game-message event decoding coverage. */

#include "common/net/legacy_game_event_candidate.h"
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

static int test_mixed_sequence_and_atomic_rejection(void)
{
  const uint8_t sequence[] = {
      svc_temp_entity, WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT, 17,
      svc_muzzleflash, 5, 0,
      WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN | MZ_SILENCED,
      svc_temp_entity, WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT, 23};
  uint8_t malformed[sizeof(sequence) + 1];
  worr_legacy_game_event_candidate_carrier_v1
      carriers[WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX];
  worr_legacy_game_event_candidate_carrier_v1
      carriers_before[WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX];
  uint32_t count = UINT32_C(0x12345678);
  uint32_t count_before;

  CHECK(Worr_LegacyGameEventDecodeRawSequenceV1(
            sequence, sizeof(sequence), carriers,
            WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX, &count) ==
        WORR_LEGACY_GAME_EVENT_CANDIDATE_OK);
  CHECK(count == 3 &&
        carriers[0].kind == WORR_LEGACY_GAME_EVENT_CANDIDATE_TEMP_ENTITY &&
        carriers[0].temp_entity.type == WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT &&
        carriers[0].temp_entity.count == 17 &&
        carriers[1].kind == WORR_LEGACY_GAME_EVENT_CANDIDATE_MUZZLEFLASH &&
        carriers[1].muzzleflash.entity == 5 &&
        carriers[1].muzzleflash.silenced &&
        carriers[1].muzzle_family == WORR_EVENT_MUZZLE_FAMILY_PLAYER &&
        carriers[2].kind == WORR_LEGACY_GAME_EVENT_CANDIDATE_TEMP_ENTITY &&
        carriers[2].temp_entity.count == 23);

  memset(carriers, 0xa5, sizeof(carriers));
  memcpy(carriers_before, carriers, sizeof(carriers));
  count = UINT32_C(0x12345678);
  count_before = count;
  CHECK(Worr_LegacyGameEventDecodeRawSequenceV1(
            sequence, sizeof(sequence), carriers, 2, &count) ==
        WORR_LEGACY_GAME_EVENT_CANDIDATE_CAPACITY);
  CHECK(memcmp(carriers, carriers_before, sizeof(carriers)) == 0 &&
        count == count_before);

  memcpy(malformed, sequence, sizeof(sequence));
  malformed[sizeof(sequence)] = svc_nop;
  CHECK(Worr_LegacyGameEventDecodeRawSequenceV1(
            malformed, sizeof(malformed), carriers,
            WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX, &count) ==
        WORR_LEGACY_GAME_EVENT_CANDIDATE_INVALID_SHAPE);
  CHECK(memcmp(carriers, carriers_before, sizeof(carriers)) == 0 &&
        count == count_before);
  return 0;
}

int main(void)
{
  if (test_mixed_sequence_and_atomic_rejection() != 0)
    return EXIT_FAILURE;
  puts("legacy game event candidate tests passed");
  return EXIT_SUCCESS;
}
