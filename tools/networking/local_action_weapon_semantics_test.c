#include "shared/local_action_weapon_semantics.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__,         \
              #condition);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int test_complete_catalog(uint64_t *digest_out) {
  uint32_t trigger_counts[7] = {0};
  uint32_t catalog_id;
  const uint32_t common_blockers =
      WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_COMMAND_TIME_OWNERSHIP |
      WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_LEGACY_FRAME_TIMELINE |
      WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_WORLD_AUTHORITY |
      WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_ASSET_EVENT_MAPPING;

  CHECK(sizeof(worr_local_action_weapon_semantics_v1) == 64);
  for (catalog_id = 1; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
       ++catalog_id) {
    worr_local_action_weapon_semantics_v1 semantics;
    uint32_t blockers = 0;

    memset(&semantics, 0, sizeof(semantics));
    CHECK(Worr_LocalActionWeaponSemanticsCopyV1(catalog_id, &semantics));
    CHECK(Worr_LocalActionWeaponSemanticsValidateV1(&semantics));
    CHECK(semantics.catalog_id == catalog_id);
    CHECK(semantics.trigger_model >=
          WORR_LOCAL_ACTION_WEAPON_TRIGGER_SINGLE_PRESS);
    CHECK(semantics.trigger_model <=
          WORR_LOCAL_ACTION_WEAPON_TRIGGER_WINDUP_COMMIT);
    ++trigger_counts[semantics.trigger_model];
    CHECK(Worr_LocalActionWeaponSemanticsAuditV2V1(&semantics, &blockers));
    CHECK(blockers == semantics.v2_blockers);
    CHECK((blockers & common_blockers) == common_blockers);
  }

  CHECK(trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_SINGLE_PRESS] == 9);
  CHECK(trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_HELD_REPEAT] == 7);
  CHECK(trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_SPIN_REPEAT] == 1);
  CHECK(trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_THROW_HOLD_RELEASE] ==
        3);
  CHECK(trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_STAGED_MULTI_FRAME] ==
        1);
  CHECK(trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_WINDUP_COMMIT] == 1);

  CHECK(Worr_LocalActionWeaponSemanticsDigestV1(digest_out));
  CHECK(*digest_out == UINT64_C(0xa5d823b554b31ee8));
  printf("local_action_weapon_semantics entries=%u single=%u repeat=%u "
         "spin=%u throw=%u staged=%u windup=%u v2_representable=0 "
         "digest=%016llx\n",
         WORR_LOCAL_ACTION_CATALOG_COUNT,
         trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_SINGLE_PRESS],
         trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_HELD_REPEAT],
         trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_SPIN_REPEAT],
         trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_THROW_HOLD_RELEASE],
         trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_STAGED_MULTI_FRAME],
         trigger_counts[WORR_LOCAL_ACTION_WEAPON_TRIGGER_WINDUP_COMMIT],
         (unsigned long long)*digest_out);
  return 0;
}

static int test_special_contracts(void) {
  worr_local_action_weapon_semantics_v1 shotgun;
  worr_local_action_weapon_semantics_v1 chaingun;
  worr_local_action_weapon_semantics_v1 grenade;
  worr_local_action_weapon_semantics_v1 ion;
  worr_local_action_weapon_semantics_v1 beam;
  worr_local_action_weapon_semantics_v1 thunder;
  worr_local_action_weapon_semantics_v1 phalanx;
  worr_local_action_weapon_semantics_v1 bfg;

  memset(&shotgun, 0, sizeof(shotgun));
  memset(&chaingun, 0, sizeof(chaingun));
  memset(&grenade, 0, sizeof(grenade));
  memset(&ion, 0, sizeof(ion));
  memset(&beam, 0, sizeof(beam));
  memset(&thunder, 0, sizeof(thunder));
  memset(&phalanx, 0, sizeof(phalanx));
  memset(&bfg, 0, sizeof(bfg));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(WORR_LOCAL_ACTION_CATALOG_SHOTGUN,
                                              &shotgun));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(
      WORR_LOCAL_ACTION_CATALOG_CHAINGUN, &chaingun));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(
      WORR_LOCAL_ACTION_CATALOG_HAND_GRENADES, &grenade));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(
      WORR_LOCAL_ACTION_CATALOG_ION_RIPPER, &ion));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(
      WORR_LOCAL_ACTION_CATALOG_PLASMA_BEAM, &beam));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(
      WORR_LOCAL_ACTION_CATALOG_THUNDERBOLT, &thunder));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(WORR_LOCAL_ACTION_CATALOG_PHALANX,
                                              &phalanx));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(WORR_LOCAL_ACTION_CATALOG_BFG10K,
                                              &bfg));

  CHECK(shotgun.emission_count_min == 11);
  CHECK(shotgun.emission_count_max == 12);
  CHECK(chaingun.nominal_ammo_debit_min == 0);
  CHECK(chaingun.nominal_ammo_debit_max == 3);
  CHECK(chaingun.emission_count_max == 3);
  CHECK((grenade.behavior_flags &
         WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_COOK_IN_HAND) != 0);
  CHECK((grenade.v2_blockers &
         WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_HOLD_RELEASE) != 0);
  CHECK(ion.nominal_ammo_debit_max == 10);
  CHECK(ion.emission_count_max == 15);
  CHECK((ion.behavior_flags &
         WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_CUSTOM_REFIRE) != 0);
  CHECK(beam.nominal_ammo_debit_max == 2);
  CHECK((thunder.emission_flags &
         WORR_LOCAL_ACTION_WEAPON_EMISSION_AREA_DISCHARGE) != 0);
  CHECK(phalanx.trigger_model ==
        WORR_LOCAL_ACTION_WEAPON_TRIGGER_STAGED_MULTI_FRAME);
  CHECK(phalanx.nominal_ammo_debit_min == 0);
  CHECK(phalanx.nominal_ammo_debit_max == 1);
  CHECK(bfg.trigger_model == WORR_LOCAL_ACTION_WEAPON_TRIGGER_WINDUP_COMMIT);
  CHECK(bfg.nominal_ammo_debit_min == 0);
  CHECK(bfg.nominal_ammo_debit_max == 50);
  CHECK((bfg.behavior_flags &
         WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_PRESENTATION_WINDUP) != 0);
  return 0;
}

static int test_fail_closed(void) {
  worr_local_action_weapon_semantics_v1 semantics;
  worr_local_action_weapon_semantics_v1 output;
  worr_local_action_weapon_semantics_v1 before;
  uint32_t blockers;
  uint64_t digest;

  memset(&semantics, 0, sizeof(semantics));
  CHECK(Worr_LocalActionWeaponSemanticsCopyV1(
      WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER, &semantics));
  semantics.emission_count_max = 2;
  CHECK(!Worr_LocalActionWeaponSemanticsValidateV1(&semantics));

  memset(&output, 0xa5, sizeof(output));
  before = output;
  CHECK(!Worr_LocalActionWeaponSemanticsCopyV1(
      WORR_LOCAL_ACTION_CATALOG_BLASTER, &output));
  CHECK(memcmp(&output, &before, sizeof(output)) == 0);
  CHECK(!Worr_LocalActionWeaponSemanticsCopyV1(WORR_LOCAL_ACTION_CATALOG_NONE,
                                               &output));
  CHECK(memcmp(&output, &before, sizeof(output)) == 0);

  blockers = UINT32_C(0xa5a5a5a5);
  CHECK(!Worr_LocalActionWeaponSemanticsAuditV2V1(&semantics, &blockers));
  CHECK(blockers == UINT32_C(0xa5a5a5a5));
  digest = UINT64_C(0xa5a5a5a5a5a5a5a5);
  CHECK(!Worr_LocalActionWeaponSemanticsDigestV1(&digest));
  CHECK(digest == UINT64_C(0xa5a5a5a5a5a5a5a5));
  return 0;
}

int main(void) {
  uint64_t digest = 0;

  if (test_complete_catalog(&digest) != 0 || test_special_contracts() != 0 ||
      test_fail_closed() != 0) {
    return 1;
  }
  return 0;
}
