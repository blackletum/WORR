/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/local_action_catalog.h"

#include <string.h>

#define ENTRY(id, driver, inventory)                                      \
    {sizeof(worr_local_action_catalog_entry_v1),                           \
     WORR_LOCAL_ACTION_CATALOG_ABI_VERSION,                                \
     WORR_LOCAL_ACTION_CATALOG_MODEL_REVISION, (id), (driver), (inventory),\
     WORR_LOCAL_ACTION_CATALOG_PROMOTION_SHADOW_ONLY, 0}

static const worr_local_action_catalog_entry_v1 catalog[] = {
    ENTRY(WORR_LOCAL_ACTION_CATALOG_BLASTER,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_CHAINFIST,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_SHOTGUN,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_SUPER_SHOTGUN,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_MACHINEGUN,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_ETF_RIFLE,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_CHAINGUN,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_HAND_GRENADES,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_TRAP,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_TESLA_MINE,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_GRENADE_LAUNCHER,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_PROX_LAUNCHER,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_HYPERBLASTER,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_ION_RIPPER,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_PLASMA_GUN,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_PLASMA_BEAM,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_THUNDERBOLT,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_RAILGUN,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_PHALANX,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_BFG10K,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
    ENTRY(WORR_LOCAL_ACTION_CATALOG_DISRUPTOR,
          WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
          WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO),
};

#undef ENTRY

_Static_assert(sizeof(catalog) / sizeof(catalog[0]) ==
                   WORR_LOCAL_ACTION_CATALOG_COUNT,
               "complete local-action catalog required");

static bool output_is_zero(const void *output, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)output;
    size_t index;

    if (!output)
        return false;
    for (index = 0; index != size; ++index) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

bool Worr_LocalActionCatalogIdValidV1(uint32_t catalog_id)
{
    return catalog_id >= WORR_LOCAL_ACTION_CATALOG_BLASTER &&
           catalog_id <= WORR_LOCAL_ACTION_CATALOG_DISRUPTOR;
}

bool Worr_LocalActionCatalogEntryValidateV1(
    const worr_local_action_catalog_entry_v1 *entry)
{
    const worr_local_action_catalog_entry_v1 *expected;

    if (!entry || entry->struct_size != sizeof(*entry) ||
        entry->schema_version != WORR_LOCAL_ACTION_CATALOG_ABI_VERSION ||
        entry->model_revision != WORR_LOCAL_ACTION_CATALOG_MODEL_REVISION ||
        !Worr_LocalActionCatalogIdValidV1(entry->catalog_id) ||
        entry->legacy_driver < WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC ||
        entry->legacy_driver > WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW ||
        entry->inventory_contract <
            WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO ||
        entry->inventory_contract >
            WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO ||
        entry->promotion_state !=
            WORR_LOCAL_ACTION_CATALOG_PROMOTION_SHADOW_ONLY ||
        entry->reserved0 != 0) {
        return false;
    }
    expected = &catalog[entry->catalog_id - 1];
    return entry->legacy_driver == expected->legacy_driver &&
           entry->inventory_contract == expected->inventory_contract &&
           entry->promotion_state == expected->promotion_state;
}

bool Worr_LocalActionCatalogCopyEntryV1(
    uint32_t catalog_id, worr_local_action_catalog_entry_v1 *entry_out)
{
    const worr_local_action_catalog_entry_v1 *entry;

    if (!output_is_zero(entry_out, sizeof(*entry_out)) ||
        !Worr_LocalActionCatalogIdValidV1(catalog_id)) {
        return false;
    }
    entry = &catalog[catalog_id - 1];
    if (entry->catalog_id != catalog_id ||
        !Worr_LocalActionCatalogEntryValidateV1(entry)) {
        return false;
    }
    *entry_out = *entry;
    return true;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int index;
    for (index = 0; index != 4; ++index) {
        hash ^= (uint8_t)(value & UINT32_C(0xff));
        hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

bool Worr_LocalActionCatalogDigestV1(uint64_t *digest_out)
{
    uint64_t digest = UINT64_C(1469598103934665603);
    uint32_t catalog_id;

    if (!digest_out || *digest_out != 0)
        return false;
    digest = hash_u32(digest, UINT32_C(0x4c414331)); /* LAC1 */
    digest = hash_u32(digest, WORR_LOCAL_ACTION_CATALOG_ABI_VERSION);
    digest = hash_u32(digest, WORR_LOCAL_ACTION_CATALOG_MODEL_REVISION);
    digest = hash_u32(digest, WORR_LOCAL_ACTION_CATALOG_COUNT);
    for (catalog_id = 1; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
         ++catalog_id) {
        const worr_local_action_catalog_entry_v1 *entry =
            &catalog[catalog_id - 1];
        if (!Worr_LocalActionCatalogEntryValidateV1(entry) ||
            entry->catalog_id != catalog_id) {
            return false;
        }
        digest = hash_u32(digest, entry->catalog_id);
        digest = hash_u32(digest, entry->legacy_driver);
        digest = hash_u32(digest, entry->inventory_contract);
        digest = hash_u32(digest, entry->promotion_state);
    }
    *digest_out = digest;
    return true;
}
