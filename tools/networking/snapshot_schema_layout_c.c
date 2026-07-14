/* C11 compile-time guard for the pointer-free FR-10-T06 snapshot records. */

#include "common/net/snapshot_store.h"

#include <stddef.h>

_Static_assert(sizeof(worr_snapshot_id_v2) == 8, "snapshot ID size");
_Static_assert(sizeof(worr_snapshot_ref_v2) == 8, "snapshot ref size");
_Static_assert(sizeof(worr_snapshot_serial_range_v2) == 16,
               "snapshot range size");
_Static_assert(sizeof(worr_snapshot_discontinuity_v2) == 24,
               "snapshot discontinuity size");
_Static_assert(sizeof(worr_snapshot_entity_generation_v2) == 16,
               "snapshot generation size");
_Static_assert(sizeof(worr_snapshot_event_ref_v2) == 32,
               "snapshot event ref size");
_Static_assert(sizeof(worr_snapshot_event_range_v2) == 40,
               "snapshot event range size");
_Static_assert(sizeof(worr_snapshot_entity_v2) == 144,
               "snapshot entity size");
_Static_assert(sizeof(worr_snapshot_player_v2) == 328,
               "snapshot player size");
_Static_assert(sizeof(worr_snapshot_v2) == 216, "snapshot size");
_Static_assert(sizeof(worr_snapshot_store_slot_v2) == 576,
               "snapshot slot size");
_Static_assert(sizeof(worr_snapshot_store_stats_v2) == 64,
               "snapshot stats size");
_Static_assert(offsetof(worr_snapshot_entity_v2, component_mask) == 32,
               "snapshot entity mask offset");
_Static_assert(offsetof(worr_snapshot_entity_v2, instance_bits) == 136,
               "snapshot entity instance offset");
_Static_assert(offsetof(worr_snapshot_player_v2, component_mask) == 32,
               "snapshot player mask offset");
_Static_assert(offsetof(worr_snapshot_player_v2, fov) == 196,
               "snapshot player FOV offset");
_Static_assert(offsetof(worr_snapshot_v2, discontinuity) == 80,
               "snapshot discontinuity offset");
_Static_assert(offsetof(worr_snapshot_v2, event_range) == 136,
               "snapshot event range offset");

int main(void)
{
    return 0;
}
