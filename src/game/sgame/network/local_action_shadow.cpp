/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "local_action_shadow.hpp"

#include "local_action_weapon_catalog.hpp"

#include "../g_local.hpp"

bool SG_LocalActionBuildShadowFromObservation(
    const worr_local_action_observation_record_v1 *observation,
    worr_local_action_shadow_v1 *shadow_out) {
  worr_local_action_shadow_v1 candidate{};

  if (!observation || !shadow_out ||
      !Worr_LocalActionObservationRecordValidateV1(observation) ||
      observation->state_before.active_weapon_id >=
          static_cast<uint32_t>(IT_TOTAL)) {
    return false;
  }

  const auto activeItem =
      static_cast<item_id_t>(observation->state_before.active_weapon_id);
  const auto catalogId = SG_LocalActionCatalogFromItem(activeItem);
  if (catalogId == WORR_LOCAL_ACTION_CATALOG_NONE ||
      SG_LocalActionItemFromCatalog(catalogId) != activeItem ||
      static_cast<uint32_t>(SG_LocalActionAmmoItemFromCatalog(catalogId)) !=
          observation->state_before.active_ammo_item_id ||
      !Worr_LocalActionShadowBuildV1(catalogId, observation, &candidate)) {
    return false;
  }
  *shadow_out = candidate;
  return true;
}
