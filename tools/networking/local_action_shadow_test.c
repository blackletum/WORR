/* Deterministic checks for descriptor-complete local-action shadow evidence. */
#include "shared/local_action_shadow.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,         \
              #condition);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static uint64_t hash_u64(uint64_t hash, uint64_t value) {
  unsigned int index;
  for (index = 0; index != 8; ++index) {
    hash ^= (uint8_t)(value & UINT64_C(0xff));
    hash *= UINT64_C(1099511628211);
    value >>= 8;
  }
  return hash;
}

static worr_command_record_v1 make_command(uint32_t sequence) {
  worr_command_record_v1 command;
  memset(&command, 0, sizeof(command));
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  command.command_id.epoch = 3;
  command.command_id.sequence = sequence;
  command.sample_time_us = (uint64_t)sequence * UINT64_C(16000);
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = 16;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  return command;
}

static bool make_observation(uint32_t catalog_id,
                             worr_local_action_observation_record_v1 *output) {
  worr_local_action_catalog_entry_v1 catalog;
  worr_local_action_observation_state_v1 before;
  worr_local_action_observation_state_v1 after;
  const worr_command_record_v1 command = make_command(catalog_id);

  memset(&catalog, 0, sizeof(catalog));
  memset(&before, 0, sizeof(before));
  memset(&after, 0, sizeof(after));
  if (!Worr_LocalActionCatalogCopyEntryV1(catalog_id, &catalog))
    return false;

  before.struct_size = sizeof(before);
  before.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
  before.flags = WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE |
                 WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE |
                 WORR_LOCAL_ACTION_OBSERVATION_ATTACK_HELD;
  before.phase = WORR_LOCAL_ACTION_OBSERVATION_READY;
  before.inventory_hash = UINT64_C(0x91f003b129e70000) + catalog_id;
  before.active_weapon_id = 100u + catalog_id;
  before.presentation_frame = 10u + catalog_id;
  before.presentation_rate = 10;
  if (catalog.inventory_contract ==
      WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO) {
    before.active_ammo_item_id = 500u + catalog_id;
    before.active_ammo_units = 20;
  } else if (catalog.inventory_contract ==
             WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO) {
    before.active_ammo_item_id = before.active_weapon_id;
    before.active_ammo_units = 20;
  }
  after = before;
  after.phase = WORR_LOCAL_ACTION_OBSERVATION_FIRING;
  after.presentation_frame += 1;
  if (after.active_ammo_item_id != 0)
    after.active_ammo_units -= 1;
  return Worr_LocalActionObservationBuildV1(catalog_id - 1, &command, &before,
                                            &after, output);
}

static int test_complete_catalog(void) {
  uint64_t digest = UINT64_C(1469598103934665603);
  uint32_t catalog_id;

  CHECK(sizeof(worr_local_action_shadow_v1) == 528);
  for (catalog_id = 1; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
       ++catalog_id) {
    worr_local_action_observation_record_v1 observation;
    worr_local_action_shadow_v1 shadow;
    worr_local_action_shadow_authority_receipt_v1 receipt;

    memset(&observation, 0, sizeof(observation));
    memset(&shadow, 0, sizeof(shadow));
    memset(&receipt, 0, sizeof(receipt));
    CHECK(make_observation(catalog_id, &observation));
    CHECK(Worr_LocalActionShadowBuildV1(catalog_id, &observation, &shadow));
    CHECK(Worr_LocalActionShadowValidateV1(&shadow));
    CHECK(shadow.catalog_id == catalog_id);
    CHECK(shadow.catalog.catalog_id == catalog_id);
    CHECK(shadow.profile.catalog_id == catalog_id);
    CHECK(shadow.semantics.catalog_id == catalog_id);
    CHECK(shadow.catalog_digest == UINT64_C(0xe857eec08cfa9c00));
    CHECK(shadow.profile_digest == UINT64_C(0x4f723f6fddf5bf52));
    CHECK(shadow.semantics_digest == UINT64_C(0xa5d823b554b31ee8));
    CHECK(shadow.semantics.v2_blockers != 0);
    CHECK(shadow.flags == (WORR_LOCAL_ACTION_SHADOW_DESCRIPTORS_COMPLETE |
                           WORR_LOCAL_ACTION_SHADOW_PROMOTION_SHADOW_ONLY |
                           WORR_LOCAL_ACTION_SHADOW_V2_BLOCKED |
                           WORR_LOCAL_ACTION_SHADOW_ATTACK_HELD_BEFORE |
                           WORR_LOCAL_ACTION_SHADOW_ATTACK_HELD_AFTER |
                           WORR_LOCAL_ACTION_SHADOW_ACTIVE_WEAPON_STABLE |
                           WORR_LOCAL_ACTION_SHADOW_AMMO_COMPARABLE));
    CHECK((shadow.flags & WORR_LOCAL_ACTION_SHADOW_AMMO_COMPARABLE) != 0);
    CHECK(shadow.observed_ammo_delta ==
          (shadow.catalog.inventory_contract ==
                   WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO
               ? 0
               : -1));
    CHECK(shadow.descriptor_hash != 0 && shadow.record_hash != 0);
    CHECK(Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt));
    CHECK(Worr_LocalActionShadowAuthorityReceiptValidateV1(&receipt));
    CHECK(receipt.command_id.epoch == observation.command.command_id.epoch);
    CHECK(receipt.command_id.sequence ==
          observation.command.command_id.sequence);
    CHECK(receipt.catalog_id == catalog_id);
    CHECK(receipt.flags == shadow.flags);
    CHECK(receipt.v2_blockers == shadow.semantics.v2_blockers);
    CHECK(receipt.descriptor_hash == shadow.descriptor_hash);
    CHECK(receipt.record_hash == shadow.record_hash);
    digest = hash_u64(digest, shadow.record_hash);
  }
  CHECK(digest == UINT64_C(0x1270244792631122));
  printf("local_action_shadow entries=%u digest=%016llx\n",
         WORR_LOCAL_ACTION_CATALOG_COUNT, (unsigned long long)digest);
  return 0;
}

static int test_switch_and_fail_closed(void) {
  worr_local_action_observation_record_v1 observation;
  worr_local_action_observation_record_v1 switched;
  worr_local_action_observation_record_v1 wrong_inventory;
  worr_local_action_observation_state_v1 wrong_before;
  worr_local_action_observation_state_v1 wrong_after;
  worr_local_action_observation_state_v1 after;
  worr_local_action_shadow_v1 shadow;
  worr_local_action_shadow_v1 stale;
  worr_local_action_shadow_v1 before_stale;
  worr_local_action_shadow_authority_receipt_v1 receipt;
  worr_local_action_shadow_authority_receipt_v1 stale_receipt;
  worr_local_action_shadow_authority_receipt_v1 before_stale_receipt;

  memset(&observation, 0, sizeof(observation));
  CHECK(make_observation(WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER,
                         &observation));
  after = observation.state_after;
  after.active_weapon_id += 1;
  after.active_ammo_item_id += 1;
  memset(&switched, 0, sizeof(switched));
  CHECK(Worr_LocalActionObservationBuildV1(
      observation.client_index, &observation.command, &observation.state_before,
      &after, &switched));
  memset(&shadow, 0, sizeof(shadow));
  CHECK(Worr_LocalActionShadowBuildV1(WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER,
                                      &switched, &shadow));
  CHECK((shadow.flags & WORR_LOCAL_ACTION_SHADOW_ACTIVE_WEAPON_STABLE) == 0);
  CHECK((shadow.flags & WORR_LOCAL_ACTION_SHADOW_AMMO_COMPARABLE) == 0);
  CHECK(shadow.observed_ammo_delta == 0);

  shadow.profile.fire_last += 1;
  CHECK(!Worr_LocalActionShadowValidateV1(&shadow));
  memset(&receipt, 0, sizeof(receipt));
  CHECK(!Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt));

  memset(&stale, 0xa5, sizeof(stale));
  before_stale = stale;
  CHECK(!Worr_LocalActionShadowBuildV1(
      WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER, &observation, &stale));
  CHECK(memcmp(&stale, &before_stale, sizeof(stale)) == 0);

  memset(&shadow, 0, sizeof(shadow));
  CHECK(!Worr_LocalActionShadowBuildV1(WORR_LOCAL_ACTION_CATALOG_NONE,
                                       &observation, &shadow));
  wrong_before = observation.state_before;
  wrong_after = observation.state_after;
  wrong_before.active_ammo_item_id = wrong_before.active_weapon_id;
  wrong_after.active_ammo_item_id = wrong_after.active_weapon_id;
  memset(&wrong_inventory, 0, sizeof(wrong_inventory));
  CHECK(Worr_LocalActionObservationBuildV1(observation.client_index,
                                           &observation.command, &wrong_before,
                                           &wrong_after, &wrong_inventory));
  memset(&shadow, 0, sizeof(shadow));
  CHECK(!Worr_LocalActionShadowBuildV1(
      WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER, &wrong_inventory, &shadow));

  memset(&shadow, 0, sizeof(shadow));
  CHECK(Worr_LocalActionShadowBuildV1(WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER,
                                      &observation, &shadow));
  memset(&receipt, 0, sizeof(receipt));
  CHECK(Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt));
  receipt.flags &= ~WORR_LOCAL_ACTION_SHADOW_V2_BLOCKED;
  CHECK(!Worr_LocalActionShadowAuthorityReceiptValidateV1(&receipt));

  memset(&stale_receipt, 0xa5, sizeof(stale_receipt));
  before_stale_receipt = stale_receipt;
  CHECK(
      !Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &stale_receipt));
  CHECK(memcmp(&stale_receipt, &before_stale_receipt, sizeof(stale_receipt)) ==
        0);
  return 0;
}

int main(void) {
  if (test_complete_catalog() != 0 || test_switch_and_fail_closed() != 0)
    return 1;
  return 0;
}
