/* Deterministic hostile checks for the FR-10-T08/T09 22-weapon catalog. */
#include "shared/local_action_catalog.h"

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

typedef struct expected_entry_s {
    uint32_t driver;
    uint32_t inventory;
} expected_entry;

static const expected_entry expected[WORR_LOCAL_ACTION_CATALOG_COUNT] = {
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
    {WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC,
     WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO},
};

static int test_complete_catalog(void)
{
    uint32_t driver_counts[4] = {0};
    uint32_t inventory_counts[4] = {0};
    uint64_t digest = 0;
    uint32_t catalog_id;

    CHECK(sizeof(worr_local_action_catalog_entry_v1) == 32);
    CHECK(!Worr_LocalActionCatalogIdValidV1(0));
    CHECK(!Worr_LocalActionCatalogIdValidV1(
        WORR_LOCAL_ACTION_CATALOG_COUNT + 1));
    for (catalog_id = 1; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
         ++catalog_id) {
        worr_local_action_catalog_entry_v1 entry;
        memset(&entry, 0, sizeof(entry));
        CHECK(Worr_LocalActionCatalogCopyEntryV1(catalog_id, &entry));
        CHECK(Worr_LocalActionCatalogEntryValidateV1(&entry));
        CHECK(entry.catalog_id == catalog_id);
        CHECK(entry.legacy_driver == expected[catalog_id - 1].driver);
        CHECK(entry.inventory_contract == expected[catalog_id - 1].inventory);
        CHECK(entry.promotion_state ==
              WORR_LOCAL_ACTION_CATALOG_PROMOTION_SHADOW_ONLY);
        ++driver_counts[entry.legacy_driver];
        ++inventory_counts[entry.inventory_contract];
    }
    CHECK(driver_counts[WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC] == 11);
    CHECK(driver_counts[WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING] == 8);
    CHECK(driver_counts[WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW] == 3);
    CHECK(inventory_counts[WORR_LOCAL_ACTION_CATALOG_INVENTORY_NO_AMMO] == 2);
    CHECK(inventory_counts[
              WORR_LOCAL_ACTION_CATALOG_INVENTORY_WEAPON_AND_AMMO] == 17);
    CHECK(inventory_counts[
              WORR_LOCAL_ACTION_CATALOG_INVENTORY_SELECTABLE_AMMO] == 3);
    CHECK(Worr_LocalActionCatalogDigestV1(&digest));
    CHECK(digest == UINT64_C(0xe857eec08cfa9c00));
    printf("local_action_catalog entries=%u generic=%u repeating=%u throw=%u "
           "digest=%016llx\n",
           WORR_LOCAL_ACTION_CATALOG_COUNT,
           driver_counts[WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC],
           driver_counts[WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING],
           driver_counts[WORR_LOCAL_ACTION_CATALOG_DRIVER_THROW],
           (unsigned long long)digest);
    return 0;
}

static int test_fail_closed(void)
{
    worr_local_action_catalog_entry_v1 entry;
    worr_local_action_catalog_entry_v1 before;
    uint64_t digest = 1;

    memset(&entry, 0x91, sizeof(entry));
    before = entry;
    CHECK(!Worr_LocalActionCatalogCopyEntryV1(
        WORR_LOCAL_ACTION_CATALOG_BLASTER, &entry));
    CHECK(memcmp(&entry, &before, sizeof(entry)) == 0);
    memset(&entry, 0, sizeof(entry));
    CHECK(!Worr_LocalActionCatalogCopyEntryV1(0, &entry));
    CHECK(entry.struct_size == 0);
    CHECK(Worr_LocalActionCatalogCopyEntryV1(
        WORR_LOCAL_ACTION_CATALOG_BLASTER, &entry));
    entry.legacy_driver = WORR_LOCAL_ACTION_CATALOG_DRIVER_REPEATING;
    CHECK(!Worr_LocalActionCatalogEntryValidateV1(&entry));
    memset(&entry, 0, sizeof(entry));
    CHECK(Worr_LocalActionCatalogCopyEntryV1(
        WORR_LOCAL_ACTION_CATALOG_BLASTER, &entry));
    entry.promotion_state = 99;
    CHECK(!Worr_LocalActionCatalogEntryValidateV1(&entry));
    CHECK(!Worr_LocalActionCatalogDigestV1(&digest));
    CHECK(digest == 1);
    return 0;
}

int main(void)
{
    int result = test_complete_catalog();
    return result ? result : test_fail_closed();
}
