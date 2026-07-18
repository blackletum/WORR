/* Exact sgame adapter checks for the FR-10-T08/T09 22-weapon catalog. */
#include "g_local.hpp"
#include "network/local_action_shadow.hpp"
#include "network/local_action_weapon_catalog.hpp"

#include <array>
#include <cstdio>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #condition);                                                \
      return 1;                                                                \
    }                                                                          \
  } while (0)

std::array<Item, IT_TOTAL> itemList{};

namespace {

void dummy_weapon_think(gentity_t *) {}

constexpr std::array<item_id_t, WORR_LOCAL_ACTION_CATALOG_COUNT> items{{
    IT_WEAPON_BLASTER,   IT_WEAPON_CHAINFIST,    IT_WEAPON_SHOTGUN,
    IT_WEAPON_SSHOTGUN,  IT_WEAPON_MACHINEGUN,   IT_WEAPON_ETF_RIFLE,
    IT_WEAPON_CHAINGUN,  IT_AMMO_GRENADES,       IT_AMMO_TRAP,
    IT_AMMO_TESLA,       IT_WEAPON_GLAUNCHER,    IT_WEAPON_PROXLAUNCHER,
    IT_WEAPON_RLAUNCHER, IT_WEAPON_HYPERBLASTER, IT_WEAPON_IONRIPPER,
    IT_WEAPON_PLASMAGUN, IT_WEAPON_PLASMABEAM,   IT_WEAPON_THUNDERBOLT,
    IT_WEAPON_RAILGUN,   IT_WEAPON_PHALANX,      IT_WEAPON_BFG,
    IT_WEAPON_DISRUPTOR,
}};

constexpr std::array<item_id_t, WORR_LOCAL_ACTION_CATALOG_COUNT> ammo{{
    IT_NULL,         IT_NULL,          IT_AMMO_SHELLS,
    IT_AMMO_SHELLS,  IT_AMMO_BULLETS,  IT_AMMO_FLECHETTES,
    IT_AMMO_BULLETS, IT_AMMO_GRENADES, IT_AMMO_TRAP,
    IT_AMMO_TESLA,   IT_AMMO_GRENADES, IT_AMMO_PROX,
    IT_AMMO_ROCKETS, IT_AMMO_CELLS,    IT_AMMO_CELLS,
    IT_AMMO_CELLS,   IT_AMMO_CELLS,    IT_AMMO_CELLS,
    IT_AMMO_SLUGS,   IT_AMMO_MAGSLUG,  IT_AMMO_CELLS,
    IT_AMMO_ROUNDS,
}};

constexpr std::array<Weapon, WORR_LOCAL_ACTION_CATALOG_COUNT> weapons{{
    Weapon::Blaster,        Weapon::Chainfist,       Weapon::Shotgun,
    Weapon::SuperShotgun,   Weapon::Machinegun,      Weapon::ETFRifle,
    Weapon::Chaingun,       Weapon::HandGrenades,    Weapon::Trap,
    Weapon::TeslaMine,      Weapon::GrenadeLauncher, Weapon::ProxLauncher,
    Weapon::RocketLauncher, Weapon::HyperBlaster,    Weapon::IonRipper,
    Weapon::PlasmaGun,      Weapon::PlasmaBeam,      Weapon::Thunderbolt,
    Weapon::Railgun,        Weapon::Phalanx,         Weapon::BFG10K,
    Weapon::Disruptor,
}};

void prepare_item_list() {
  for (size_t index = 0; index < itemList.size(); ++index)
    itemList[index].id = static_cast<item_id_t>(index);
  for (size_t index = 0; index < items.size(); ++index) {
    Item &item = itemList[items[index]];
    item.weaponThink = dummy_weapon_think;
    item.flags = IF_WEAPON;
    item.ammo = ammo[index];
    if (items[index] == IT_AMMO_GRENADES || items[index] == IT_AMMO_TRAP ||
        items[index] == IT_AMMO_TESLA) {
      item.flags |= IF_AMMO;
    }
  }
}

worr_command_record_v1 make_command(uint32_t sequence) {
  worr_command_record_v1 command{};
  command.struct_size = sizeof(command);
  command.schema_version = WORR_COMMAND_ABI_VERSION;
  command.command_id.epoch = 9;
  command.command_id.sequence = sequence;
  command.sample_time_us = static_cast<uint64_t>(sequence) * 16000;
  command.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
  command.command.struct_size = sizeof(command.command);
  command.command.schema_version = WORR_PREDICTION_ABI_VERSION;
  command.command.duration_ms = 16;
  command.render_watermark.struct_size = sizeof(command.render_watermark);
  command.render_watermark.schema_version = WORR_COMMAND_ABI_VERSION;
  return command;
}

bool make_observation(size_t index,
                      worr_local_action_observation_record_v1 *output) {
  worr_local_action_observation_state_v1 before{};
  before.struct_size = sizeof(before);
  before.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
  before.flags = WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE |
                 WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE;
  before.phase = WORR_LOCAL_ACTION_OBSERVATION_READY;
  before.inventory_hash = UINT64_C(0x3041018f94000000) + index;
  before.active_weapon_id = static_cast<uint32_t>(items[index]);
  before.active_ammo_item_id = static_cast<uint32_t>(ammo[index]);
  before.active_ammo_units = ammo[index] == IT_NULL ? 0 : 20;
  before.presentation_frame = 7;
  before.presentation_rate = 10;
  auto after = before;
  after.presentation_frame = 8;
  const auto command = make_command(static_cast<uint32_t>(index + 1));
  return Worr_LocalActionObservationBuildV1(static_cast<uint32_t>(index),
                                            &command, &before, &after, output);
}

} // namespace

int main() {
  prepare_item_list();
  CHECK(SG_LocalActionWeaponCatalogValidate());
  for (uint32_t value = 1; value <= WORR_LOCAL_ACTION_CATALOG_COUNT; ++value) {
    const auto catalog_id = static_cast<worr_local_action_catalog_id_v1>(value);
    const size_t index = value - 1;
    CHECK(SG_LocalActionCatalogFromItem(items[index]) == catalog_id);
    CHECK(SG_LocalActionCatalogFromWeaponIndex(weapons[index]) == catalog_id);
    CHECK(SG_LocalActionItemFromCatalog(catalog_id) == items[index]);
    CHECK(SG_LocalActionAmmoItemFromCatalog(catalog_id) == ammo[index]);
    CHECK(SG_LocalActionWeaponIndexFromCatalog(catalog_id) == weapons[index]);
    CHECK(SG_LocalActionItemFromWeaponIndex(weapons[index]) == items[index]);
    CHECK(SG_LocalActionWeaponIndexFromItem(items[index]) == weapons[index]);

    worr_local_action_observation_record_v1 observation{};
    worr_local_action_shadow_v1 shadow{};
    CHECK(make_observation(index, &observation));
    CHECK(SG_LocalActionBuildShadowFromObservation(&observation, &shadow));
    CHECK(Worr_LocalActionShadowValidateV1(&shadow));
    CHECK(shadow.catalog_id == value);
  }
  CHECK(SG_LocalActionCatalogFromItem(IT_WEAPON_GRAPPLE) ==
        WORR_LOCAL_ACTION_CATALOG_NONE);
  CHECK(SG_LocalActionCatalogFromWeaponIndex(Weapon::GrapplingHook) ==
        WORR_LOCAL_ACTION_CATALOG_NONE);
  CHECK(SG_LocalActionItemFromWeaponIndex(Weapon::GrapplingHook) == IT_NULL);
  CHECK(SG_LocalActionWeaponIndexFromItem(IT_WEAPON_GRAPPLE) == Weapon::None);

  worr_local_action_observation_record_v1 blasterObservation{};
  worr_local_action_observation_record_v1 grappleObservation{};
  worr_local_action_shadow_v1 grappleShadow{};
  CHECK(make_observation(0, &blasterObservation));
  auto grappleBefore = blasterObservation.state_before;
  auto grappleAfter = blasterObservation.state_after;
  grappleBefore.active_weapon_id = IT_WEAPON_GRAPPLE;
  grappleAfter.active_weapon_id = IT_WEAPON_GRAPPLE;
  CHECK(Worr_LocalActionObservationBuildV1(
      blasterObservation.client_index, &blasterObservation.command,
      &grappleBefore, &grappleAfter, &grappleObservation));
  CHECK(!SG_LocalActionBuildShadowFromObservation(&grappleObservation,
                                                  &grappleShadow));

  worr_local_action_observation_record_v1 rocketObservation{};
  worr_local_action_observation_record_v1 wrongAmmoObservation{};
  worr_local_action_shadow_v1 wrongAmmoShadow{};
  constexpr size_t rocketIndex = WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER - 1;
  CHECK(make_observation(rocketIndex, &rocketObservation));
  auto wrongAmmoBefore = rocketObservation.state_before;
  auto wrongAmmoAfter = rocketObservation.state_after;
  wrongAmmoBefore.active_ammo_item_id = IT_AMMO_CELLS;
  wrongAmmoAfter.active_ammo_item_id = IT_AMMO_CELLS;
  CHECK(Worr_LocalActionObservationBuildV1(
      rocketObservation.client_index, &rocketObservation.command,
      &wrongAmmoBefore, &wrongAmmoAfter, &wrongAmmoObservation));
  CHECK(!SG_LocalActionBuildShadowFromObservation(&wrongAmmoObservation,
                                                  &wrongAmmoShadow));

  itemList[IT_WEAPON_RLAUNCHER].ammo = IT_AMMO_CELLS;
  CHECK(!SG_LocalActionWeaponCatalogValidate());
  std::printf(
      "local_action_weapon_catalog_adapter entries=%u grapple=separate\n",
      WORR_LOCAL_ACTION_CATALOG_COUNT);
  return 0;
}
