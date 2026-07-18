/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/local_action_weapon_profile.h"

#include <string.h>

#define G(id, act, fire, idle, deact, pc, p0, p1, p2, p3, fc, f0, f1, flags,  \
          altc, a0, a1, refire)                                             \
    {sizeof(worr_local_action_weapon_profile_v1),                            \
     WORR_LOCAL_ACTION_WEAPON_PROFILE_ABI_VERSION,                           \
     WORR_LOCAL_ACTION_WEAPON_PROFILE_MODEL_REVISION, (id),                  \
     WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC, (flags), (act), (fire),       \
     (idle), (deact), (pc), {(p0), (p1), (p2), (p3)}, (fc), {(f0), (f1)},   \
     (altc), {(a0), (a1)}, -1, -1, -1, (refire), 0}

#define R(id, act, fire, idle, deact, pc, p0, p1, p2, p3)                   \
    {sizeof(worr_local_action_weapon_profile_v1),                            \
     WORR_LOCAL_ACTION_WEAPON_PROFILE_ABI_VERSION,                           \
     WORR_LOCAL_ACTION_WEAPON_PROFILE_MODEL_REVISION, (id),                  \
     WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING, 0, (act), (fire), (idle),   \
     (deact), (pc), {(p0), (p1), (p2), (p3)}, 0, {0, 0}, 0, {0, 0}, -1,    \
     -1, -1, 0, 0}

#define T(id, fire, idle, prime, hold, release, pc, p0, p1, p2, p3, flags)  \
    {sizeof(worr_local_action_weapon_profile_v1),                            \
     WORR_LOCAL_ACTION_WEAPON_PROFILE_ABI_VERSION,                           \
     WORR_LOCAL_ACTION_WEAPON_PROFILE_MODEL_REVISION, (id),                  \
     WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW, (flags), -1, (fire), (idle),    \
     -1, (pc), {(p0), (p1), (p2), (p3)}, 0, {0, 0}, 0, {0, 0}, (prime),    \
     (hold), (release), 0, 0}

static const worr_local_action_weapon_profile_v1 profiles[] = {
    G(WORR_LOCAL_ACTION_CATALOG_BLASTER, 4, 8, 52, 55, 2, 19, 32, 0, 0, 1,
      5, 0, 0, 0, 0, 0, 0),
    R(WORR_LOCAL_ACTION_CATALOG_CHAINFIST, 4, 32, 57, 60, 0, 0, 0, 0, 0),
    G(WORR_LOCAL_ACTION_CATALOG_SHOTGUN, 7, 18, 36, 39, 3, 22, 28, 34, 0, 1,
      8, 0, 0, 0, 0, 0, 0),
    G(WORR_LOCAL_ACTION_CATALOG_SUPER_SHOTGUN, 6, 17, 57, 61, 3, 29, 42, 57,
      0, 1, 7, 0, 0, 0, 0, 0, 0),
    R(WORR_LOCAL_ACTION_CATALOG_MACHINEGUN, 3, 5, 45, 49, 2, 23, 45, 0, 0),
    R(WORR_LOCAL_ACTION_CATALOG_ETF_RIFLE, 4, 7, 37, 41, 2, 18, 28, 0, 0),
    R(WORR_LOCAL_ACTION_CATALOG_CHAINGUN, 4, 31, 61, 64, 4, 38, 43, 51, 61),
    T(WORR_LOCAL_ACTION_CATALOG_HAND_GRENADES, 15, 48, 5, 11, 12, 4, 29, 34,
      39, 48, WORR_LOCAL_ACTION_WEAPON_PROFILE_THROW_EXPLODES_IN_HAND |
                  WORR_LOCAL_ACTION_WEAPON_PROFILE_THROW_EXTRA_IDLE_FRAME |
                  WORR_LOCAL_ACTION_WEAPON_PROFILE_POST_DRIVER_SKIP_FRAME_ONE),
    T(WORR_LOCAL_ACTION_CATALOG_TRAP, 15, 48, 5, 11, 12, 4, 29, 34, 39, 48,
      0),
    T(WORR_LOCAL_ACTION_CATALOG_TESLA_MINE, 8, 32, -1, 1, 2, 1, 21, 0, 0, 0,
      0),
    G(WORR_LOCAL_ACTION_CATALOG_GRENADE_LAUNCHER, 5, 16, 59, 64, 3, 34, 51,
      59, 0, 1, 6, 0, 0, 0, 0, 0, 0),
    G(WORR_LOCAL_ACTION_CATALOG_PROX_LAUNCHER, 5, 16, 59, 64, 3, 34, 51, 59,
      0, 1, 6, 0, 0, 0, 0, 0, 0),
    G(WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER, 4, 12, 50, 54, 4, 25, 33,
      42, 50, 1, 5, 0, 0, 0, 0, 0, 0),
    R(WORR_LOCAL_ACTION_CATALOG_HYPERBLASTER, 5, 20, 49, 53, 0, 0, 0, 0, 0),
    G(WORR_LOCAL_ACTION_CATALOG_ION_RIPPER, 5, 7, 36, 39, 1, 36, 0, 0, 0, 1,
      6, 0, WORR_LOCAL_ACTION_WEAPON_PROFILE_CUSTOM_MIN_REFIRE, 0, 0, 0,
      1000),
    R(WORR_LOCAL_ACTION_CATALOG_PLASMA_GUN, 8, 42, 49, 51, 0, 0, 0, 0, 0),
    R(WORR_LOCAL_ACTION_CATALOG_PLASMA_BEAM, 8, 12, 42, 47, 1, 35, 0, 0, 0),
    R(WORR_LOCAL_ACTION_CATALOG_THUNDERBOLT, 0, 2, 3, 4, 0, 0, 0, 0, 0),
    G(WORR_LOCAL_ACTION_CATALOG_RAILGUN, 3, 18, 56, 61, 1, 56, 0, 0, 0, 1,
      4, 0, 0, 0, 0, 0, 0),
    G(WORR_LOCAL_ACTION_CATALOG_PHALANX, 5, 20, 58, 63, 3, 29, 42, 55, 0, 2,
      7, 8, 0, 0, 0, 0, 0),
    G(WORR_LOCAL_ACTION_CATALOG_BFG10K, 8, 32, 54, 58, 4, 39, 45, 50, 55, 2,
      9, 17, WORR_LOCAL_ACTION_WEAPON_PROFILE_ALTERNATE_FIRE_FRAMES, 2, 15,
      17, 0),
    G(WORR_LOCAL_ACTION_CATALOG_DISRUPTOR, 4, 9, 29, 34, 3, 14, 19, 23, 0, 1,
      5, 0, 0, 0, 0, 0, 0),
};

#undef G
#undef R
#undef T

_Static_assert(sizeof(profiles) / sizeof(profiles[0]) ==
                   WORR_LOCAL_ACTION_CATALOG_COUNT,
               "complete local-action weapon profile catalog required");

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

static bool frames_valid(const int32_t *frames, uint32_t count,
                         uint32_t capacity)
{
    uint32_t index;
    if (count > capacity)
        return false;
    for (index = 0; index != capacity; ++index) {
        if (index < count) {
            if (frames[index] <= 0 ||
                (index != 0 && frames[index] <= frames[index - 1])) {
                return false;
            }
        } else if (frames[index] != 0) {
            return false;
        }
    }
    return true;
}

bool Worr_LocalActionWeaponProfileValidateV1(
    const worr_local_action_weapon_profile_v1 *profile)
{
    const worr_local_action_weapon_profile_v1 *expected;
    worr_local_action_catalog_entry_v1 catalog_entry;
    const uint32_t known_flags =
        WORR_LOCAL_ACTION_WEAPON_PROFILE_ALTERNATE_FIRE_FRAMES |
        WORR_LOCAL_ACTION_WEAPON_PROFILE_THROW_EXPLODES_IN_HAND |
        WORR_LOCAL_ACTION_WEAPON_PROFILE_THROW_EXTRA_IDLE_FRAME |
        WORR_LOCAL_ACTION_WEAPON_PROFILE_POST_DRIVER_SKIP_FRAME_ONE |
        WORR_LOCAL_ACTION_WEAPON_PROFILE_CUSTOM_MIN_REFIRE;

    if (!profile || profile->struct_size != sizeof(*profile) ||
        profile->schema_version !=
            WORR_LOCAL_ACTION_WEAPON_PROFILE_ABI_VERSION ||
        profile->model_revision !=
            WORR_LOCAL_ACTION_WEAPON_PROFILE_MODEL_REVISION ||
        !Worr_LocalActionCatalogIdValidV1(profile->catalog_id) ||
        (profile->flags & ~known_flags) != 0 || profile->reserved0 != 0 ||
        !frames_valid(profile->pause_frames, profile->pause_frame_count,
                      WORR_LOCAL_ACTION_WEAPON_PROFILE_MAX_PAUSE_FRAMES) ||
        !frames_valid(profile->fire_frames, profile->fire_frame_count,
                      WORR_LOCAL_ACTION_WEAPON_PROFILE_MAX_FIRE_FRAMES) ||
        !frames_valid(profile->alternate_fire_frames,
                      profile->alternate_fire_frame_count,
                      WORR_LOCAL_ACTION_WEAPON_PROFILE_MAX_FIRE_FRAMES)) {
        return false;
    }
    memset(&catalog_entry, 0, sizeof(catalog_entry));
    if (!Worr_LocalActionCatalogCopyEntryV1(profile->catalog_id,
                                             &catalog_entry) ||
        profile->legacy_driver != catalog_entry.legacy_driver) {
        return false;
    }
    expected = &profiles[profile->catalog_id - 1];
    return memcmp(profile, expected, sizeof(*profile)) == 0;
}

bool Worr_LocalActionWeaponProfileCopyV1(
    uint32_t catalog_id, worr_local_action_weapon_profile_v1 *profile_out)
{
    const worr_local_action_weapon_profile_v1 *profile;

    if (!output_is_zero(profile_out, sizeof(*profile_out)) ||
        !Worr_LocalActionCatalogIdValidV1(catalog_id)) {
        return false;
    }
    profile = &profiles[catalog_id - 1];
    if (profile->catalog_id != catalog_id ||
        !Worr_LocalActionWeaponProfileValidateV1(profile)) {
        return false;
    }
    *profile_out = *profile;
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

bool Worr_LocalActionWeaponProfileDigestV1(uint64_t *digest_out)
{
    uint64_t digest = UINT64_C(1469598103934665603);
    uint32_t catalog_id;

    if (!digest_out || *digest_out != 0)
        return false;
    digest = hash_u32(digest, UINT32_C(0x4c415031)); /* LAP1 */
    digest = hash_u32(digest, WORR_LOCAL_ACTION_WEAPON_PROFILE_ABI_VERSION);
    digest = hash_u32(digest,
                      WORR_LOCAL_ACTION_WEAPON_PROFILE_MODEL_REVISION);
    digest = hash_u32(digest, WORR_LOCAL_ACTION_CATALOG_COUNT);
    for (catalog_id = 1; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
         ++catalog_id) {
        const uint8_t *bytes = (const uint8_t *)&profiles[catalog_id - 1];
        size_t index;
        if (!Worr_LocalActionWeaponProfileValidateV1(
                &profiles[catalog_id - 1])) {
            return false;
        }
        for (index = 3; index < sizeof(profiles[0]) / sizeof(uint32_t);
             ++index) {
            uint32_t word;
            memcpy(&word, bytes + index * sizeof(word), sizeof(word));
            digest = hash_u32(digest, word);
        }
    }
    *digest_out = digest;
    return true;
}
