/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/local_action_catalog.h"

#include <cstdint>

enum item_id_t : uint8_t;
enum class Weapon : uint8_t;

/*
 * Exact sgame adapter for the transport-neutral 22-weapon catalog. Grapple
 * intentionally maps to NONE because it is owned by local-interaction.
 */
worr_local_action_catalog_id_v1
SG_LocalActionCatalogFromItem(item_id_t item_id);
worr_local_action_catalog_id_v1
SG_LocalActionCatalogFromWeaponIndex(Weapon weapon_index);
item_id_t SG_LocalActionItemFromCatalog(
    worr_local_action_catalog_id_v1 catalog_id);
item_id_t SG_LocalActionAmmoItemFromCatalog(
    worr_local_action_catalog_id_v1 catalog_id);
Weapon SG_LocalActionWeaponIndexFromCatalog(
    worr_local_action_catalog_id_v1 catalog_id);

item_id_t SG_LocalActionItemFromWeaponIndex(Weapon weapon_index);
Weapon SG_LocalActionWeaponIndexFromItem(item_id_t item_id);

/* Verifies all mappings against the live itemList metadata. */
bool SG_LocalActionWeaponCatalogValidate();
