/* Standalone FR-10-T10 immutable rewind-collision ABI C layout check. */

#include "shared/rewind_collision.h"

#include <stddef.h>

int main(void)
{
    worr_rewind_collision_map_v1 map = {0};
    worr_rewind_collision_asset_v1 asset = {0};
    worr_rewind_collision_trace_request_v1 request = {0};
    worr_rewind_collision_import_v1 import = {0};

    return sizeof(map) == 24 && sizeof(asset) == 64 &&
                   sizeof(request) == 104 &&
                   sizeof(import) ==
                       offsetof(worr_rewind_collision_import_v1,
                                TraceTransformed) +
                           sizeof(import.TraceTransformed) &&
                   offsetof(worr_rewind_collision_asset_v1, local_mins) ==
                       40 &&
                   offsetof(worr_rewind_collision_trace_request_v1,
                            angles) == 92 &&
                   offsetof(worr_rewind_collision_import_v1,
                            ResolveInlineBrush) ==
                       offsetof(worr_rewind_collision_import_v1,
                                GetMapIdentity) +
                           sizeof(import.GetMapIdentity)
               ? 0
               : 1;
}
