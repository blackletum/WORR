/* Hostile checks for the FR-10-T08/T09 22-weapon frame profiles. */
#include "shared/local_action_weapon_profile.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                  \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static int test_complete_profiles(void)
{
    uint32_t drivers[4] = {0};
    uint64_t digest = 0;
    uint32_t catalog_id;

    CHECK(sizeof(worr_local_action_weapon_profile_v1) == 104);
    for (catalog_id = 1; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
         ++catalog_id) {
        worr_local_action_weapon_profile_v1 profile;
        memset(&profile, 0, sizeof(profile));
        CHECK(Worr_LocalActionWeaponProfileCopyV1(catalog_id, &profile));
        CHECK(Worr_LocalActionWeaponProfileValidateV1(&profile));
        CHECK(profile.catalog_id == catalog_id);
        CHECK(profile.legacy_driver >=
              WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC);
        CHECK(profile.legacy_driver <=
              WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW);
        ++drivers[profile.legacy_driver];
    }
    CHECK(drivers[WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC] == 11);
    CHECK(drivers[WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING] == 8);
    CHECK(drivers[WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW] == 3);
    CHECK(Worr_LocalActionWeaponProfileDigestV1(&digest));
    CHECK(digest == UINT64_C(0x4f723f6fddf5bf52));
    printf("local_action_weapon_profiles entries=%u generic=%u repeating=%u "
           "throw=%u digest=%016llx\n",
           WORR_LOCAL_ACTION_CATALOG_COUNT,
           drivers[WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC],
           drivers[WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING],
           drivers[WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW],
           (unsigned long long)digest);
    return 0;
}

static int test_special_profiles(void)
{
    worr_local_action_weapon_profile_v1 hand;
    worr_local_action_weapon_profile_v1 tesla;
    worr_local_action_weapon_profile_v1 ion;
    worr_local_action_weapon_profile_v1 bfg;

    memset(&hand, 0, sizeof(hand));
    memset(&tesla, 0, sizeof(tesla));
    memset(&ion, 0, sizeof(ion));
    memset(&bfg, 0, sizeof(bfg));
    CHECK(Worr_LocalActionWeaponProfileCopyV1(
        WORR_LOCAL_ACTION_CATALOG_HAND_GRENADES, &hand));
    CHECK(hand.throw_prime_sound_frame == 5 && hand.throw_hold_frame == 11 &&
          hand.throw_fire_frame == 12 && hand.fire_last == 15 &&
          hand.idle_last == 48 && hand.pause_frame_count == 4);
    CHECK((hand.flags &
           (WORR_LOCAL_ACTION_WEAPON_PROFILE_THROW_EXPLODES_IN_HAND |
            WORR_LOCAL_ACTION_WEAPON_PROFILE_THROW_EXTRA_IDLE_FRAME |
            WORR_LOCAL_ACTION_WEAPON_PROFILE_POST_DRIVER_SKIP_FRAME_ONE)) !=
          0);
    CHECK(Worr_LocalActionWeaponProfileCopyV1(
        WORR_LOCAL_ACTION_CATALOG_TESLA_MINE, &tesla));
    CHECK(tesla.throw_prime_sound_frame == -1 && tesla.throw_hold_frame == 1 &&
          tesla.throw_fire_frame == 2);
    CHECK(Worr_LocalActionWeaponProfileCopyV1(
        WORR_LOCAL_ACTION_CATALOG_ION_RIPPER, &ion));
    CHECK(ion.custom_min_refire_ms == 1000);
    CHECK(ion.flags & WORR_LOCAL_ACTION_WEAPON_PROFILE_CUSTOM_MIN_REFIRE);
    CHECK(Worr_LocalActionWeaponProfileCopyV1(
        WORR_LOCAL_ACTION_CATALOG_BFG10K, &bfg));
    CHECK(bfg.fire_frame_count == 2 && bfg.fire_frames[0] == 9 &&
          bfg.fire_frames[1] == 17 && bfg.alternate_fire_frame_count == 2 &&
          bfg.alternate_fire_frames[0] == 15 &&
          bfg.alternate_fire_frames[1] == 17);
    return 0;
}

static int test_fail_closed(void)
{
    worr_local_action_weapon_profile_v1 profile;
    worr_local_action_weapon_profile_v1 before;
    uint64_t digest = 1;

    memset(&profile, 0x91, sizeof(profile));
    before = profile;
    CHECK(!Worr_LocalActionWeaponProfileCopyV1(
        WORR_LOCAL_ACTION_CATALOG_BLASTER, &profile));
    CHECK(memcmp(&profile, &before, sizeof(profile)) == 0);
    memset(&profile, 0, sizeof(profile));
    CHECK(!Worr_LocalActionWeaponProfileCopyV1(0, &profile));
    CHECK(profile.struct_size == 0);
    CHECK(Worr_LocalActionWeaponProfileCopyV1(
        WORR_LOCAL_ACTION_CATALOG_BLASTER, &profile));
    profile.fire_frames[0] = 6;
    CHECK(!Worr_LocalActionWeaponProfileValidateV1(&profile));
    CHECK(!Worr_LocalActionWeaponProfileDigestV1(&digest));
    CHECK(digest == 1);
    return 0;
}

int main(void)
{
    int result = test_complete_profiles();
    if (result)
        return result;
    result = test_special_profiles();
    return result ? result : test_fail_closed();
}
