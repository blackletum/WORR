/* C11 guards for FR-10-T06 Stage B runtime envelopes and fixed records. */

#include "common/net/snapshot_q2proto.h"

#include <stddef.h>

_Static_assert(sizeof(worr_snapshot_projection_view_v2) == 64,
               "projection view v2 size");
_Static_assert(sizeof(worr_snapshot_projection_hashes_v2) == 56,
               "projection hashes v2 size");
_Static_assert(sizeof(worr_snapshot_q2proto_profile_v2) == 44,
               "q2proto profile v2 size");
_Static_assert(sizeof(worr_snapshot_q2proto_lineage_v2) == 8,
               "q2proto lineage v2 size");
_Static_assert(sizeof(worr_snapshot_q2proto_slot_v2) == 648,
               "q2proto slot v2 size");
_Static_assert(sizeof(worr_snapshot_q2proto_storage_v2) == 144,
               "q2proto storage v2 size");
_Static_assert(sizeof(worr_snapshot_q2proto_context_v2) == 248,
               "q2proto context v2 size");
_Static_assert(sizeof(worr_snapshot_q2proto_frame_input_v2) == 80,
               "q2proto frame input v2 size");
_Static_assert(offsetof(worr_snapshot_q2proto_frame_input_v2,
                        consumed_command) == 64,
               "q2proto frame input consumed-command offset");
_Static_assert(offsetof(worr_snapshot_q2proto_slot_v2, hashes) == 544,
               "q2proto slot hash offset");
_Static_assert(offsetof(worr_snapshot_q2proto_context_v2, storage) == 56,
               "q2proto context storage offset");

int main(void)
{
    return 0;
}
