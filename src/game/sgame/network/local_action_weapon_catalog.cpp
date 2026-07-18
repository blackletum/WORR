/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "local_action_weapon_catalog.hpp"

#include "../g_local.hpp"

#include <array>

namespace {

struct binding_t {
  worr_local_action_catalog_id_v1 catalog_id;
  Weapon weapon_index;
  item_id_t item_id;
  item_id_t ammo_item_id;
};

constexpr std::array<binding_t, WORR_LOCAL_ACTION_CATALOG_COUNT> bindings{{
    {WORR_LOCAL_ACTION_CATALOG_BLASTER, Weapon::Blaster,
     IT_WEAPON_BLASTER, IT_NULL},
    {WORR_LOCAL_ACTION_CATALOG_CHAINFIST, Weapon::Chainfist,
     IT_WEAPON_CHAINFIST, IT_NULL},
    {WORR_LOCAL_ACTION_CATALOG_SHOTGUN, Weapon::Shotgun,
     IT_WEAPON_SHOTGUN, IT_AMMO_SHELLS},
    {WORR_LOCAL_ACTION_CATALOG_SUPER_SHOTGUN, Weapon::SuperShotgun,
     IT_WEAPON_SSHOTGUN, IT_AMMO_SHELLS},
    {WORR_LOCAL_ACTION_CATALOG_MACHINEGUN, Weapon::Machinegun,
     IT_WEAPON_MACHINEGUN, IT_AMMO_BULLETS},
    {WORR_LOCAL_ACTION_CATALOG_ETF_RIFLE, Weapon::ETFRifle,
     IT_WEAPON_ETF_RIFLE, IT_AMMO_FLECHETTES},
    {WORR_LOCAL_ACTION_CATALOG_CHAINGUN, Weapon::Chaingun,
     IT_WEAPON_CHAINGUN, IT_AMMO_BULLETS},
    {WORR_LOCAL_ACTION_CATALOG_HAND_GRENADES, Weapon::HandGrenades,
     IT_AMMO_GRENADES, IT_AMMO_GRENADES},
    {WORR_LOCAL_ACTION_CATALOG_TRAP, Weapon::Trap, IT_AMMO_TRAP,
     IT_AMMO_TRAP},
    {WORR_LOCAL_ACTION_CATALOG_TESLA_MINE, Weapon::TeslaMine,
     IT_AMMO_TESLA, IT_AMMO_TESLA},
    {WORR_LOCAL_ACTION_CATALOG_GRENADE_LAUNCHER, Weapon::GrenadeLauncher,
     IT_WEAPON_GLAUNCHER, IT_AMMO_GRENADES},
    {WORR_LOCAL_ACTION_CATALOG_PROX_LAUNCHER, Weapon::ProxLauncher,
     IT_WEAPON_PROXLAUNCHER, IT_AMMO_PROX},
    {WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER, Weapon::RocketLauncher,
     IT_WEAPON_RLAUNCHER, IT_AMMO_ROCKETS},
    {WORR_LOCAL_ACTION_CATALOG_HYPERBLASTER, Weapon::HyperBlaster,
     IT_WEAPON_HYPERBLASTER, IT_AMMO_CELLS},
    {WORR_LOCAL_ACTION_CATALOG_ION_RIPPER, Weapon::IonRipper,
     IT_WEAPON_IONRIPPER, IT_AMMO_CELLS},
    {WORR_LOCAL_ACTION_CATALOG_PLASMA_GUN, Weapon::PlasmaGun,
     IT_WEAPON_PLASMAGUN, IT_AMMO_CELLS},
    {WORR_LOCAL_ACTION_CATALOG_PLASMA_BEAM, Weapon::PlasmaBeam,
     IT_WEAPON_PLASMABEAM, IT_AMMO_CELLS},
    {WORR_LOCAL_ACTION_CATALOG_THUNDERBOLT, Weapon::Thunderbolt,
     IT_WEAPON_THUNDERBOLT, IT_AMMO_CELLS},
    {WORR_LOCAL_ACTION_CATALOG_RAILGUN, Weapon::Railgun,
     IT_WEAPON_RAILGUN, IT_AMMO_SLUGS},
    {WORR_LOCAL_ACTION_CATALOG_PHALANX, Weapon::Phalanx,
     IT_WEAPON_PHALANX, IT_AMMO_MAGSLUG},
    {WORR_LOCAL_ACTION_CATALOG_BFG10K, Weapon::BFG10K, IT_WEAPON_BFG,
     IT_AMMO_CELLS},
    {WORR_LOCAL_ACTION_CATALOG_DISRUPTOR, Weapon::Disruptor,
     IT_WEAPON_DISRUPTOR, IT_AMMO_ROUNDS},
}};

static_assert(bindings.size() == WORR_LOCAL_ACTION_CATALOG_COUNT);

const binding_t *binding_for_catalog(uint32_t catalog_id)
{
  if (!Worr_LocalActionCatalogIdValidV1(catalog_id))
    return nullptr;
  const binding_t &binding = bindings[catalog_id - 1];
  return static_cast<uint32_t>(binding.catalog_id) == catalog_id ? &binding
                                                                 : nullptr;
}

} // namespace

worr_local_action_catalog_id_v1
SG_LocalActionCatalogFromItem(item_id_t item_id)
{
  for (const binding_t &binding : bindings) {
    if (binding.item_id == item_id)
      return binding.catalog_id;
  }
  return WORR_LOCAL_ACTION_CATALOG_NONE;
}

worr_local_action_catalog_id_v1
SG_LocalActionCatalogFromWeaponIndex(Weapon weapon_index)
{
  for (const binding_t &binding : bindings) {
    if (binding.weapon_index == weapon_index)
      return binding.catalog_id;
  }
  return WORR_LOCAL_ACTION_CATALOG_NONE;
}

item_id_t SG_LocalActionItemFromCatalog(
    worr_local_action_catalog_id_v1 catalog_id)
{
  const binding_t *binding = binding_for_catalog(catalog_id);
  return binding ? binding->item_id : IT_NULL;
}

item_id_t SG_LocalActionAmmoItemFromCatalog(
    worr_local_action_catalog_id_v1 catalog_id)
{
  const binding_t *binding = binding_for_catalog(catalog_id);
  return binding ? binding->ammo_item_id : IT_NULL;
}

Weapon SG_LocalActionWeaponIndexFromCatalog(
    worr_local_action_catalog_id_v1 catalog_id)
{
  const binding_t *binding = binding_for_catalog(catalog_id);
  return binding ? binding->weapon_index : Weapon::None;
}

item_id_t SG_LocalActionItemFromWeaponIndex(Weapon weapon_index)
{
  return SG_LocalActionItemFromCatalog(
      SG_LocalActionCatalogFromWeaponIndex(weapon_index));
}

Weapon SG_LocalActionWeaponIndexFromItem(item_id_t item_id)
{
  return SG_LocalActionWeaponIndexFromCatalog(
      SG_LocalActionCatalogFromItem(item_id));
}

bool SG_LocalActionWeaponCatalogValidate()
{
  std::array<bool, WORR_LOCAL_ACTION_CATALOG_COUNT + 1> seen_catalog{};
  std::array<bool, static_cast<size_t>(IT_TOTAL)> seen_items{};

  for (const binding_t &binding : bindings) {
    worr_local_action_catalog_entry_v1 entry{};
    const uint32_t catalog_id = static_cast<uint32_t>(binding.catalog_id);
    const size_t item_index = static_cast<size_t>(binding.item_id);
    const size_t weapon_index = static_cast<size_t>(binding.weapon_index);

    if (catalog_id == 0 || catalog_id > WORR_LOCAL_ACTION_CATALOG_COUNT ||
        seen_catalog[catalog_id] || item_index == 0 ||
        item_index >= itemList.size() || seen_items[item_index] ||
        weapon_index == 0 ||
        weapon_index >= static_cast<size_t>(Weapon::Total) ||
        !Worr_LocalActionCatalogCopyEntryV1(catalog_id, &entry)) {
      return false;
    }
    seen_catalog[catalog_id] = true;
    seen_items[item_index] = true;

    const Item &item = itemList[item_index];
    if (item.id != binding.item_id || item.weaponThink == nullptr ||
        !(item.flags & IF_WEAPON) || item.ammo != binding.ammo_item_id ||
        SG_LocalActionCatalogFromItem(binding.item_id) != binding.catalog_id ||
        SG_LocalActionCatalogFromWeaponIndex(binding.weapon_index) !=
            binding.catalog_id ||
        SG_LocalActionItemFromCatalog(binding.catalog_id) != binding.item_id ||
        SG_LocalActionAmmoItemFromCatalog(binding.catalog_id) !=
            binding.ammo_item_id ||
        SG_LocalActionWeaponIndexFromCatalog(binding.catalog_id) !=
            binding.weapon_index) {
      return false;
    }

    switch (entry.inventory_contract) {
    case WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO:
      if (binding.ammo_item_id != IT_NULL || (item.flags & IF_AMMO))
        return false;
      break;
    case WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO:
      if (binding.ammo_item_id == IT_NULL ||
          binding.ammo_item_id == binding.item_id || (item.flags & IF_AMMO))
        return false;
      break;
    case WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO:
      if (binding.ammo_item_id != binding.item_id || !(item.flags & IF_AMMO))
        return false;
      break;
    default:
      return false;
    }
  }

  for (uint32_t catalog_id = 1;
       catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT; ++catalog_id) {
    if (!seen_catalog[catalog_id])
      return false;
  }
  return !seen_items[IT_WEAPON_GRAPPLE] &&
         SG_LocalActionCatalogFromItem(IT_WEAPON_GRAPPLE) ==
             WORR_LOCAL_ACTION_CATALOG_NONE &&
         SG_LocalActionCatalogFromWeaponIndex(Weapon::GrapplingHook) ==
             WORR_LOCAL_ACTION_CATALOG_NONE;
}
