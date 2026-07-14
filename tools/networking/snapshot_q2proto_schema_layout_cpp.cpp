// C++20 guards for FR-10-T06 Stage B records and runtime envelopes.

#include "common/net/snapshot_q2proto.h"

#include <cstddef>
#include <type_traits>

template <typename T>
constexpr bool stable_record =
    std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

static_assert(stable_record<worr_snapshot_projection_view_v2>);
static_assert(stable_record<worr_snapshot_projection_hashes_v2>);
static_assert(stable_record<worr_snapshot_q2proto_profile_v2>);
static_assert(stable_record<worr_snapshot_q2proto_lineage_v2>);
static_assert(stable_record<worr_snapshot_q2proto_slot_v2>);
static_assert(stable_record<worr_snapshot_q2proto_storage_v2>);
static_assert(stable_record<worr_snapshot_q2proto_context_v2>);
static_assert(stable_record<worr_snapshot_q2proto_frame_input_v2>);
static_assert(sizeof(worr_snapshot_q2proto_slot_v2) == 648);
static_assert(sizeof(worr_snapshot_q2proto_context_v2) == 248);
static_assert(offsetof(worr_snapshot_q2proto_frame_input_v2,
                       server_time_us) == 56);
static_assert(sizeof(worr_snapshot_q2proto_frame_input_v2) == 80);
static_assert(offsetof(worr_snapshot_q2proto_frame_input_v2,
                       consumed_command) == 64);

int main()
{
    return 0;
}
