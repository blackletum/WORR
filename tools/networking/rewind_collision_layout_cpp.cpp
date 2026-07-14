/* Standalone FR-10-T10 immutable rewind-collision ABI C++ layout check. */

#include "shared/rewind_collision.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<
              worr_rewind_collision_asset_handle_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_rewind_collision_asset_handle_v1>);
static_assert(std::is_standard_layout_v<worr_rewind_collision_asset_v1>);
static_assert(std::is_trivially_copyable_v<worr_rewind_collision_asset_v1>);
static_assert(std::is_standard_layout_v<
              worr_rewind_collision_trace_request_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_rewind_collision_trace_request_v1>);
static_assert(sizeof(worr_rewind_collision_import_v1) ==
              offsetof(worr_rewind_collision_import_v1, TraceTransformed) +
                  sizeof(worr_rewind_collision_import_v1::TraceTransformed));
static_assert(offsetof(worr_rewind_collision_import_v1,
                       TraceTransformed) ==
              offsetof(worr_rewind_collision_import_v1,
                       ResolveInlineBrush) +
                  sizeof(worr_rewind_collision_import_v1::ResolveInlineBrush));

int main()
{
    return 0;
}
