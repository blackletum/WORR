// C++20 guards for the FR-10-T06 Stage C client shadow contract.

#include "client/snapshot_shadow.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<cl_snapshot_shadow_status_v1>);
static_assert(std::is_trivially_copyable_v<cl_snapshot_shadow_status_v1>);
static_assert(sizeof(cl_snapshot_shadow_status_v1) == 296);
static_assert(offsetof(cl_snapshot_shadow_status_v1, connection_resets) ==
              64);
static_assert(offsetof(cl_snapshot_shadow_status_v1,
                       last_legacy_observed_parity_hash) == 288);
static_assert(CL_SNAPSHOT_SHADOW_CAPTURE_DELTA_CAPACITY !=
              CL_SNAPSHOT_SHADOW_CAPTURE_MISSING_TERMINATOR);
static_assert(CL_SNAPSHOT_SHADOW_LIFECYCLE_ACTIVE !=
              CL_SNAPSHOT_SHADOW_LIFECYCLE_DISABLED);

int main()
{
    return 0;
}
