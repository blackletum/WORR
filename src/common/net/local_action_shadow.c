/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/local_action_shadow.h"

#include <string.h>

#define LOCAL_ACTION_SHADOW_BASE_FLAGS                                         \
  (WORR_LOCAL_ACTION_SHADOW_DESCRIPTORS_COMPLETE |                             \
   WORR_LOCAL_ACTION_SHADOW_PROMOTION_SHADOW_ONLY |                            \
   WORR_LOCAL_ACTION_SHADOW_V2_BLOCKED)

static bool output_is_zero(const void *output, size_t size) {
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

static uint64_t hash_begin(void) { return UINT64_C(1469598103934665603); }

static uint64_t hash_u32(uint64_t hash, uint32_t value) {
  unsigned int index;
  for (index = 0; index != 4; ++index) {
    hash ^= (uint8_t)(value & UINT32_C(0xff));
    hash *= UINT64_C(1099511628211);
    value >>= 8;
  }
  return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value) {
  unsigned int index;
  for (index = 0; index != 8; ++index) {
    hash ^= (uint8_t)(value & UINT64_C(0xff));
    hash *= UINT64_C(1099511628211);
    value >>= 8;
  }
  return hash;
}

static bool inventory_contract_matches(
    const worr_local_action_catalog_entry_v1 *catalog,
    const worr_local_action_observation_state_v1 *state) {
  switch (catalog->inventory_contract) {
  case WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO:
    return state->active_ammo_item_id == 0 && state->active_ammo_units == 0;
  case WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO:
    return state->active_ammo_item_id != 0 &&
           state->active_ammo_item_id != state->active_weapon_id;
  case WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO:
    return state->active_ammo_item_id == state->active_weapon_id;
  default:
    return false;
  }
}

static uint32_t
observation_flags(const worr_local_action_observation_record_v1 *observation) {
  uint32_t flags = LOCAL_ACTION_SHADOW_BASE_FLAGS;
  const worr_local_action_observation_state_v1 *before =
      &observation->state_before;
  const worr_local_action_observation_state_v1 *after =
      &observation->state_after;

  if ((before->flags & WORR_LOCAL_ACTION_OBSERVATION_ATTACK_HELD) != 0)
    flags |= WORR_LOCAL_ACTION_SHADOW_ATTACK_HELD_BEFORE;
  if ((after->flags & WORR_LOCAL_ACTION_OBSERVATION_ATTACK_HELD) != 0)
    flags |= WORR_LOCAL_ACTION_SHADOW_ATTACK_HELD_AFTER;
  if (before->active_weapon_id == after->active_weapon_id) {
    flags |= WORR_LOCAL_ACTION_SHADOW_ACTIVE_WEAPON_STABLE;
    if (before->active_ammo_item_id == after->active_ammo_item_id)
      flags |= WORR_LOCAL_ACTION_SHADOW_AMMO_COMPARABLE;
  }
  return flags;
}

static uint64_t descriptor_hash(const worr_local_action_shadow_v1 *shadow) {
  uint64_t hash = hash_begin();

  hash = hash_u32(hash, UINT32_C(0x4c534431)); /* LSD1 */
  hash = hash_u32(hash, shadow->catalog_id);
  hash = hash_u64(hash, shadow->catalog_digest);
  hash = hash_u64(hash, shadow->profile_digest);
  return hash_u64(hash, shadow->semantics_digest);
}

static uint64_t record_hash(const worr_local_action_shadow_v1 *shadow) {
  uint64_t hash = hash_begin();

  hash = hash_u32(hash, UINT32_C(0x4c415331)); /* LAS1 */
  hash = hash_u32(hash, shadow->model_revision);
  hash = hash_u32(hash, shadow->catalog_id);
  hash = hash_u32(hash, shadow->flags);
  hash = hash_u32(hash, (uint32_t)shadow->observed_ammo_delta);
  hash = hash_u64(hash, shadow->descriptor_hash);
  return hash_u64(hash, shadow->observation.semantic_hash);
}

bool Worr_LocalActionShadowBuildV1(
    uint32_t catalog_id,
    const worr_local_action_observation_record_v1 *observation,
    worr_local_action_shadow_v1 *shadow_out) {
  worr_local_action_shadow_v1 candidate;

  if (!output_is_zero(shadow_out, sizeof(*shadow_out)) ||
      !Worr_LocalActionCatalogIdValidV1(catalog_id) ||
      !Worr_LocalActionObservationRecordValidateV1(observation) ||
      observation->state_before.active_weapon_id == 0) {
    return false;
  }

  memset(&candidate, 0, sizeof(candidate));
  candidate.struct_size = sizeof(candidate);
  candidate.schema_version = WORR_LOCAL_ACTION_SHADOW_ABI_VERSION;
  candidate.model_revision = WORR_LOCAL_ACTION_SHADOW_MODEL_REVISION;
  candidate.catalog_id = catalog_id;
  if (!Worr_LocalActionCatalogCopyEntryV1(catalog_id, &candidate.catalog) ||
      !Worr_LocalActionWeaponProfileCopyV1(catalog_id, &candidate.profile) ||
      !Worr_LocalActionWeaponSemanticsCopyV1(catalog_id,
                                             &candidate.semantics) ||
      !inventory_contract_matches(&candidate.catalog,
                                  &observation->state_before) ||
      !Worr_LocalActionCatalogDigestV1(&candidate.catalog_digest) ||
      !Worr_LocalActionWeaponProfileDigestV1(&candidate.profile_digest) ||
      !Worr_LocalActionWeaponSemanticsDigestV1(&candidate.semantics_digest) ||
      candidate.catalog.promotion_state !=
          WORR_LOCAL_ACTION_CATALOG_PROMOTION_SHADOW_ONLY ||
      candidate.semantics.v2_blockers == 0) {
    return false;
  }

  candidate.flags = observation_flags(observation);
  if ((candidate.flags & WORR_LOCAL_ACTION_SHADOW_AMMO_COMPARABLE) != 0) {
    candidate.observed_ammo_delta = observation->state_after.active_ammo_units -
                                    observation->state_before.active_ammo_units;
  }
  candidate.observation = *observation;
  candidate.descriptor_hash = descriptor_hash(&candidate);
  candidate.record_hash = record_hash(&candidate);
  *shadow_out = candidate;
  return true;
}

bool Worr_LocalActionShadowValidateV1(
    const worr_local_action_shadow_v1 *shadow) {
  worr_local_action_shadow_v1 expected;

  if (!shadow || shadow->struct_size != sizeof(*shadow) ||
      shadow->schema_version != WORR_LOCAL_ACTION_SHADOW_ABI_VERSION ||
      shadow->model_revision != WORR_LOCAL_ACTION_SHADOW_MODEL_REVISION ||
      shadow->reserved0 != 0 || shadow->reserved1 != 0) {
    return false;
  }
  memset(&expected, 0, sizeof(expected));
  return Worr_LocalActionShadowBuildV1(shadow->catalog_id, &shadow->observation,
                                       &expected) &&
         memcmp(shadow, &expected, sizeof(expected)) == 0;
}

bool Worr_LocalActionShadowAuthorityReceiptBuildV1(
    const worr_local_action_shadow_v1 *shadow,
    worr_local_action_shadow_authority_receipt_v1 *receipt_out) {
  worr_local_action_shadow_authority_receipt_v1 candidate;
  uint64_t command_hash;

  if (!output_is_zero(receipt_out, sizeof(*receipt_out)) ||
      !Worr_LocalActionShadowValidateV1(shadow) ||
      !Worr_CommandRecordInputHashV1(&shadow->observation.command,
                                     WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
                                     &command_hash)) {
    return false;
  }

  memset(&candidate, 0, sizeof(candidate));
  candidate.struct_size = sizeof(candidate);
  candidate.schema_version = WORR_LOCAL_ACTION_SHADOW_ABI_VERSION;
  candidate.model_revision = WORR_LOCAL_ACTION_SHADOW_MODEL_REVISION;
  candidate.command_id = shadow->observation.command.command_id;
  candidate.catalog_id = shadow->catalog_id;
  candidate.flags = shadow->flags;
  candidate.v2_blockers = shadow->semantics.v2_blockers;
  candidate.command_hash = command_hash;
  candidate.descriptor_hash = shadow->descriptor_hash;
  candidate.record_hash = shadow->record_hash;
  if (!Worr_LocalActionShadowAuthorityReceiptValidateV1(&candidate))
    return false;
  *receipt_out = candidate;
  return true;
}

bool Worr_LocalActionShadowAuthorityReceiptValidateV1(
    const worr_local_action_shadow_authority_receipt_v1 *receipt) {
  worr_local_action_shadow_v1 descriptor;
  worr_local_action_weapon_semantics_v1 semantics;
  const uint32_t known_flags = WORR_LOCAL_ACTION_SHADOW_DESCRIPTORS_COMPLETE |
                               WORR_LOCAL_ACTION_SHADOW_PROMOTION_SHADOW_ONLY |
                               WORR_LOCAL_ACTION_SHADOW_V2_BLOCKED |
                               WORR_LOCAL_ACTION_SHADOW_ATTACK_HELD_BEFORE |
                               WORR_LOCAL_ACTION_SHADOW_ATTACK_HELD_AFTER |
                               WORR_LOCAL_ACTION_SHADOW_ACTIVE_WEAPON_STABLE |
                               WORR_LOCAL_ACTION_SHADOW_AMMO_COMPARABLE;

  if (!receipt || receipt->struct_size != sizeof(*receipt) ||
      receipt->schema_version != WORR_LOCAL_ACTION_SHADOW_ABI_VERSION ||
      receipt->model_revision != WORR_LOCAL_ACTION_SHADOW_MODEL_REVISION ||
      receipt->reserved0 != 0 || receipt->reserved1 != 0 ||
      !Worr_CommandIdValidV1(receipt->command_id, false) ||
      !Worr_LocalActionCatalogIdValidV1(receipt->catalog_id) ||
      (receipt->flags & ~known_flags) != 0 ||
      (receipt->flags & LOCAL_ACTION_SHADOW_BASE_FLAGS) !=
          LOCAL_ACTION_SHADOW_BASE_FLAGS ||
      receipt->command_hash == 0 || receipt->descriptor_hash == 0 ||
      receipt->record_hash == 0) {
    return false;
  }

  memset(&descriptor, 0, sizeof(descriptor));
  memset(&semantics, 0, sizeof(semantics));
  descriptor.catalog_id = receipt->catalog_id;
  if (!Worr_LocalActionCatalogDigestV1(&descriptor.catalog_digest) ||
      !Worr_LocalActionWeaponProfileDigestV1(&descriptor.profile_digest) ||
      !Worr_LocalActionWeaponSemanticsDigestV1(
          &descriptor.semantics_digest) ||
      !Worr_LocalActionWeaponSemanticsCopyV1(receipt->catalog_id,
                                              &semantics)) {
    return false;
  }
  return receipt->descriptor_hash == descriptor_hash(&descriptor) &&
         receipt->v2_blockers == semantics.v2_blockers;
}
