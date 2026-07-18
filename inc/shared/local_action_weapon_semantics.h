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

#include "shared/local_action_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_LOCAL_ACTION_WEAPON_SEMANTICS_ABI_VERSION 1u
#define WORR_LOCAL_ACTION_WEAPON_SEMANTICS_MODEL_REVISION 1u

/* Trigger ownership exercised by the production legacy fire callback. */
typedef enum worr_local_action_weapon_trigger_v1_e {
  WORR_LOCAL_ACTION_WEAPON_TRIGGER_SINGLE_PRESS = 1,
  WORR_LOCAL_ACTION_WEAPON_TRIGGER_HELD_REPEAT = 2,
  WORR_LOCAL_ACTION_WEAPON_TRIGGER_SPIN_REPEAT = 3,
  WORR_LOCAL_ACTION_WEAPON_TRIGGER_THROW_HOLD_RELEASE = 4,
  WORR_LOCAL_ACTION_WEAPON_TRIGGER_STAGED_MULTI_FRAME = 5,
  WORR_LOCAL_ACTION_WEAPON_TRIGGER_WINDUP_COMMIT = 6,
} worr_local_action_weapon_trigger_v1;

enum {
  WORR_LOCAL_ACTION_WEAPON_EMISSION_MELEE_TRACE = 1u << 0,
  WORR_LOCAL_ACTION_WEAPON_EMISSION_HITSCAN = 1u << 1,
  WORR_LOCAL_ACTION_WEAPON_EMISSION_STRAIGHT_PROJECTILE = 1u << 2,
  WORR_LOCAL_ACTION_WEAPON_EMISSION_BALLISTIC_PROJECTILE = 1u << 3,
  WORR_LOCAL_ACTION_WEAPON_EMISSION_DEPLOYABLE = 1u << 4,
  WORR_LOCAL_ACTION_WEAPON_EMISSION_CONTINUOUS_BEAM = 1u << 5,
  WORR_LOCAL_ACTION_WEAPON_EMISSION_PIERCING_BEAM = 1u << 6,
  WORR_LOCAL_ACTION_WEAPON_EMISSION_TARGETED_PROJECTILE = 1u << 7,
  WORR_LOCAL_ACTION_WEAPON_EMISSION_AREA_DISCHARGE = 1u << 8,
};

/* Inputs read by the callback itself, beyond the shared legacy driver. */
enum {
  WORR_LOCAL_ACTION_WEAPON_DEP_WORLD_QUERY = 1u << 0,
  WORR_LOCAL_ACTION_WEAPON_DEP_SERVER_ENTITY = 1u << 1,
  WORR_LOCAL_ACTION_WEAPON_DEP_LAG_COMPENSATION = 1u << 2,
  WORR_LOCAL_ACTION_WEAPON_DEP_RANDOM_STREAM = 1u << 3,
  WORR_LOCAL_ACTION_WEAPON_DEP_RULESET = 1u << 4,
  WORR_LOCAL_ACTION_WEAPON_DEP_GAME_MODE = 1u << 5,
  WORR_LOCAL_ACTION_WEAPON_DEP_POWERUP_STATE = 1u << 6,
  WORR_LOCAL_ACTION_WEAPON_DEP_INFINITE_AMMO_POLICY = 1u << 7,
  WORR_LOCAL_ACTION_WEAPON_DEP_CLIENT_FRAME_STATE = 1u << 8,
  WORR_LOCAL_ACTION_WEAPON_DEP_HOLD_TIME = 1u << 9,
  WORR_LOCAL_ACTION_WEAPON_DEP_PLAYER_HEALTH = 1u << 10,
  WORR_LOCAL_ACTION_WEAPON_DEP_WATER_CONTENTS = 1u << 11,
  WORR_LOCAL_ACTION_WEAPON_DEP_CVAR_POLICY = 1u << 12,
  WORR_LOCAL_ACTION_WEAPON_DEP_SERVER_TIME = 1u << 13,
  WORR_LOCAL_ACTION_WEAPON_DEP_OBJECTIVE_STATE = 1u << 14,
  WORR_LOCAL_ACTION_WEAPON_DEP_INVENTORY = 1u << 15,
};

enum {
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_CALLBACK_CAN_ABORT = 1u << 0,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_RANDOMIZED_EMISSION = 1u << 1,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_CONDITIONAL_AMMO_DEBIT = 1u << 2,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_MULTI_EMISSION = 1u << 3,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_MULTI_FIRE_FRAME = 1u << 4,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_PRESENTATION_WINDUP = 1u << 5,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_HOLD_RELEASE = 1u << 6,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_COOK_IN_HAND = 1u << 7,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_LOOPING_FRAME_MACHINE = 1u << 8,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_CONTINUOUS_PRESENTATION = 1u << 9,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_MODE_OVERRIDE = 1u << 10,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_ALTERNATING_MUZZLE = 1u << 11,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_HOLD_SCALED_TRAJECTORY = 1u << 12,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_WORLD_TARGET_SELECTION = 1u << 13,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_DISCHARGE_BRANCH = 1u << 14,
  WORR_LOCAL_ACTION_WEAPON_BEHAVIOR_CUSTOM_REFIRE = 1u << 15,
};

/* Exact reasons the current local-action v2 rule is not promotable. */
enum {
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_COMMAND_TIME_OWNERSHIP = 1u << 0,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_LEGACY_FRAME_TIMELINE = 1u << 1,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_WORLD_AUTHORITY = 1u << 2,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_ASSET_EVENT_MAPPING = 1u << 3,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_HELD_REPEAT = 1u << 4,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_HOLD_RELEASE = 1u << 5,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_VARIABLE_AMMO_OR_EMISSION = 1u << 6,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_STAGED_FIRE = 1u << 7,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_RULESET_BRANCH = 1u << 8,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_EXTERNAL_CLIENT_STATE = 1u << 9,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_RANDOM_STREAM = 1u << 10,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_MODE_OVERRIDE = 1u << 11,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_SERVER_ENTITY_LIFECYCLE = 1u << 12,
  WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_HISTORICAL_QUERY = 1u << 13,
};

/*
 * Pointer-free facts for one production fire callback. Ammo and emission
 * ranges are per callback invocation and include its ordinary early-return
 * paths. The frame profile separately defines how many callbacks an accepted
 * trigger can schedule (for example, Phalanx and BFG each have two frames).
 */
typedef struct worr_local_action_weapon_semantics_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t model_revision;
  uint32_t catalog_id;
  uint32_t trigger_model;
  uint32_t emission_flags;
  uint32_t dependency_flags;
  uint32_t behavior_flags;
  uint32_t nominal_ammo_debit_min;
  uint32_t nominal_ammo_debit_max;
  uint32_t emission_count_min;
  uint32_t emission_count_max;
  uint32_t v2_blockers;
  uint32_t required_catalog_revision;
  uint32_t required_profile_revision;
  uint32_t reserved0;
} worr_local_action_weapon_semantics_v1;

bool Worr_LocalActionWeaponSemanticsValidateV1(
    const worr_local_action_weapon_semantics_v1 *semantics);

/* output must be entirely zero; failures leave it byte-identical. */
bool Worr_LocalActionWeaponSemanticsCopyV1(
    uint32_t catalog_id, worr_local_action_weapon_semantics_v1 *semantics_out);
bool Worr_LocalActionWeaponSemanticsDigestV1(uint64_t *digest_out);

/*
 * A successful audit copies the exact blocker mask. `blockers_out` must point
 * to zero and is untouched if the descriptor is invalid. A zero mask would be
 * the necessary (not sufficient) condition for local-action v2 promotion.
 */
bool Worr_LocalActionWeaponSemanticsAuditV2V1(
    const worr_local_action_weapon_semantics_v1 *semantics,
    uint32_t *blockers_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_WEAPON_SEMANTICS_STATIC_ASSERT(condition, message)   \
  static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_WEAPON_SEMANTICS_STATIC_ASSERT(condition, message)   \
  _Static_assert((condition), message)
#endif

WORR_LOCAL_ACTION_WEAPON_SEMANTICS_STATIC_ASSERT(
    sizeof(worr_local_action_weapon_semantics_v1) == 64,
    "local-action weapon semantics v1 layout changed");
WORR_LOCAL_ACTION_WEAPON_SEMANTICS_STATIC_ASSERT(
    offsetof(worr_local_action_weapon_semantics_v1, nominal_ammo_debit_min) ==
        32,
    "local-action weapon semantics ammo offset changed");
WORR_LOCAL_ACTION_WEAPON_SEMANTICS_STATIC_ASSERT(
    offsetof(worr_local_action_weapon_semantics_v1, v2_blockers) == 48,
    "local-action weapon semantics blocker offset changed");

#undef WORR_LOCAL_ACTION_WEAPON_SEMANTICS_STATIC_ASSERT
