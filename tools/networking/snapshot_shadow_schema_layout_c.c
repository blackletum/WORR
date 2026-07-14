/* C11 guards for the FR-10-T06 Stage C client shadow contract. */

#include "client/snapshot_shadow.h"

#include <stddef.h>

_Static_assert(sizeof(cl_snapshot_shadow_status_v1) == 296,
               "client snapshot shadow status v1 size");
_Static_assert(offsetof(cl_snapshot_shadow_status_v1, connection_resets) ==
                   64,
               "client snapshot shadow counter offset");
_Static_assert(offsetof(cl_snapshot_shadow_status_v1,
                        last_legacy_observed_parity_hash) == 288,
               "client snapshot shadow observed parity offset");
_Static_assert(CL_SNAPSHOT_SHADOW_NO_CONTROLLED_ENTITY == UINT32_MAX,
               "client snapshot shadow absent controlled identity");
_Static_assert((CL_SNAPSHOT_SHADOW_ACCEPT_DELIVER_CONSUMER &
                CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY) == 0,
               "client snapshot shadow accept flags overlap");
_Static_assert((CL_SNAPSHOT_SHADOW_PARITY_METADATA &
                CL_SNAPSHOT_SHADOW_PARITY_HASH_BUILD) == 0,
               "client snapshot shadow parity flags overlap");

int main(void)
{
    return 0;
}
