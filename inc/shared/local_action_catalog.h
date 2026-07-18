/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stable, transport-neutral identity for the 22 ordinary Rerelease weapons.
 * The selected-weapon Grapple is deliberately excluded: Hook input and its
 * authoritative receipt use the independent local-interaction contract.
 */
#define WORR_LOCAL_ACTION_CATALOG_ABI_VERSION 1u
#define WORR_LOCAL_ACTION_CATALOG_MODEL_REVISION 1u
#define WORR_LOCAL_ACTION_CATALOG_COUNT 22u

typedef enum worr_local_action_catalog_id_v1_e {
    WORR_LOCAL_ACTION_CATALOG_NONE = 0,
    WORR_LOCAL_ACTION_CATALOG_BLASTER = 1,
    WORR_LOCAL_ACTION_CATALOG_CHAINFIST = 2,
    WORR_LOCAL_ACTION_CATALOG_SHOTGUN = 3,
    WORR_LOCAL_ACTION_CATALOG_SUPER_SHOTGUN = 4,
    WORR_LOCAL_ACTION_CATALOG_MACHINEGUN = 5,
    WORR_LOCAL_ACTION_CATALOG_ETF_RIFLE = 6,
    WORR_LOCAL_ACTION_CATALOG_CHAINGUN = 7,
    WORR_LOCAL_ACTION_CATALOG_HAND_GRENADES = 8,
    WORR_LOCAL_ACTION_CATALOG_TRAP = 9,
    WORR_LOCAL_ACTION_CATALOG_TESLA_MINE = 10,
    WORR_LOCAL_ACTION_CATALOG_GRENADE_LAUNCHER = 11,
    WORR_LOCAL_ACTION_CATALOG_PROX_LAUNCHER = 12,
    WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER = 13,
    WORR_LOCAL_ACTION_CATALOG_HYPERBLASTER = 14,
    WORR_LOCAL_ACTION_CATALOG_ION_RIPPER = 15,
    WORR_LOCAL_ACTION_CATALOG_PLASMA_GUN = 16,
    WORR_LOCAL_ACTION_CATALOG_PLASMA_BEAM = 17,
    WORR_LOCAL_ACTION_CATALOG_THUNDERBOLT = 18,
    WORR_LOCAL_ACTION_CATALOG_RAILGUN = 19,
    WORR_LOCAL_ACTION_CATALOG_PHALANX = 20,
    WORR_LOCAL_ACTION_CATALOG_BFG10K = 21,
    WORR_LOCAL_ACTION_CATALOG_DISRUPTOR = 22,
} worr_local_action_catalog_id_v1;

/* Exact legacy state-machine entry point used by p_weapon. */
typedef enum worr_local_action_catalog_driver_v1_e {
    WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC = 1,
    WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING = 2,
    WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW = 3,
} worr_local_action_catalog_driver_v1;

/* How ownership and active ammunition are represented in sgame inventory. */
typedef enum worr_local_action_catalog_inventory_v1_e {
    WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO = 1,
    WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO = 2,
    WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO = 3,
} worr_local_action_catalog_inventory_v1;

typedef enum worr_local_action_catalog_promotion_v1_e {
    /* Observation is allowed; gameplay or presentation authority is not. */
    WORR_LOCAL_ACTION_CATALOG_PROMOTION_SHADOW_ONLY = 1,
} worr_local_action_catalog_promotion_v1;

/* Pointer-free catalog fact used identically by cgame, sgame, and tooling. */
typedef struct worr_local_action_catalog_entry_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t catalog_id;
    uint32_t legacy_driver;
    uint32_t inventory_contract;
    uint32_t promotion_state;
    uint32_t reserved0;
} worr_local_action_catalog_entry_v1;

bool Worr_LocalActionCatalogIdValidV1(uint32_t catalog_id);
bool Worr_LocalActionCatalogEntryValidateV1(
    const worr_local_action_catalog_entry_v1 *entry);

/* output must be entirely zero; failures leave it byte-identical. */
bool Worr_LocalActionCatalogCopyEntryV1(
    uint32_t catalog_id, worr_local_action_catalog_entry_v1 *entry_out);

/*
 * Hashes the complete ordered catalog. `digest_out` must point to zero and is
 * untouched on failure. The digest freezes identity, driver, inventory, and
 * promotion semantics without hashing compiler padding or process pointers.
 */
bool Worr_LocalActionCatalogDigestV1(uint64_t *digest_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_CATALOG_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_CATALOG_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_LOCAL_ACTION_CATALOG_STATIC_ASSERT(
    sizeof(worr_local_action_catalog_entry_v1) == 32,
    "local-action catalog entry v1 layout changed");
WORR_LOCAL_ACTION_CATALOG_STATIC_ASSERT(
    offsetof(worr_local_action_catalog_entry_v1, catalog_id) == 12,
    "local-action catalog id offset changed");

#undef WORR_LOCAL_ACTION_CATALOG_STATIC_ASSERT
