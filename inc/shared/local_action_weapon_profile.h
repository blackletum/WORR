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

#define WORR_LOCAL_ACTION_WEAPON_PROFILE_ABI_VERSION 1u
#define WORR_LOCAL_ACTION_WEAPON_PROFILE_MODEL_REVISION 1u
#define WORR_LOCAL_ACTION_WEAPON_PROFILE_MAX_PAUSE_FRAMES 4u
#define WORR_LOCAL_ACTION_WEAPON_PROFILE_MAX_FIRE_FRAMES 2u

enum {
    WORR_LOCAL_ACTION_WEAPON_PROFILE_ALTERNATE_FIRE_FRAMES = 1u << 0,
    WORR_LOCAL_ACTION_WEAPON_PROFILE_THROW_EXPLODES_IN_HAND = 1u << 1,
    WORR_LOCAL_ACTION_WEAPON_PROFILE_THROW_EXTRA_IDLE_FRAME = 1u << 2,
    WORR_LOCAL_ACTION_WEAPON_PROFILE_POST_DRIVER_SKIP_FRAME_ONE = 1u << 3,
    WORR_LOCAL_ACTION_WEAPON_PROFILE_CUSTOM_MIN_REFIRE = 1u << 4,
};

/*
 * Exact pointer-free frame skeleton passed to the current legacy driver.
 * Values that do not apply to a driver are -1. This does not describe damage,
 * traces, projectiles, assets, powerups, or full firing callback semantics.
 */
typedef struct worr_local_action_weapon_profile_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t model_revision;
    uint32_t catalog_id;
    uint32_t legacy_driver;
    uint32_t flags;
    int32_t activate_last;
    int32_t fire_last;
    int32_t idle_last;
    int32_t deactivate_last;
    uint32_t pause_frame_count;
    int32_t pause_frames[WORR_LOCAL_ACTION_WEAPON_PROFILE_MAX_PAUSE_FRAMES];
    uint32_t fire_frame_count;
    int32_t fire_frames[WORR_LOCAL_ACTION_WEAPON_PROFILE_MAX_FIRE_FRAMES];
    uint32_t alternate_fire_frame_count;
    int32_t alternate_fire_frames[
        WORR_LOCAL_ACTION_WEAPON_PROFILE_MAX_FIRE_FRAMES];
    int32_t throw_prime_sound_frame;
    int32_t throw_hold_frame;
    int32_t throw_fire_frame;
    uint32_t custom_min_refire_ms;
    uint32_t reserved0;
} worr_local_action_weapon_profile_v1;

bool Worr_LocalActionWeaponProfileValidateV1(
    const worr_local_action_weapon_profile_v1 *profile);

/* output must be entirely zero; failures leave it byte-identical. */
bool Worr_LocalActionWeaponProfileCopyV1(
    uint32_t catalog_id, worr_local_action_weapon_profile_v1 *profile_out);
bool Worr_LocalActionWeaponProfileDigestV1(uint64_t *digest_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_WEAPON_PROFILE_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_WEAPON_PROFILE_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_LOCAL_ACTION_WEAPON_PROFILE_STATIC_ASSERT(
    sizeof(worr_local_action_weapon_profile_v1) == 104,
    "local-action weapon profile v1 layout changed");
WORR_LOCAL_ACTION_WEAPON_PROFILE_STATIC_ASSERT(
    offsetof(worr_local_action_weapon_profile_v1, pause_frames) == 44,
    "local-action weapon profile pause-frame offset changed");
WORR_LOCAL_ACTION_WEAPON_PROFILE_STATIC_ASSERT(
    offsetof(worr_local_action_weapon_profile_v1, throw_prime_sound_frame) ==
        84,
    "local-action weapon profile throw-frame offset changed");

#undef WORR_LOCAL_ACTION_WEAPON_PROFILE_STATIC_ASSERT
