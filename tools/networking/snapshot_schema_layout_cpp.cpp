// C++20 compile-time guard for the pointer-free FR-10-T06 snapshot records.

#include "common/net/snapshot_store.h"

#include <cstddef>
#include <type_traits>

template <typename T>
constexpr bool canonical_record =
    std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

static_assert(canonical_record<worr_snapshot_id_v2>);
static_assert(canonical_record<worr_snapshot_serial_range_v2>);
static_assert(canonical_record<worr_snapshot_discontinuity_v2>);
static_assert(canonical_record<worr_snapshot_entity_generation_v2>);
static_assert(canonical_record<worr_snapshot_event_ref_v2>);
static_assert(canonical_record<worr_snapshot_event_range_v2>);
static_assert(canonical_record<worr_snapshot_entity_v2>);
static_assert(canonical_record<worr_snapshot_player_v2>);
static_assert(canonical_record<worr_snapshot_v2>);
static_assert(canonical_record<worr_snapshot_store_slot_v2>);
static_assert(sizeof(worr_snapshot_entity_v2) == 144);
static_assert(sizeof(worr_snapshot_player_v2) == 328);
static_assert(sizeof(worr_snapshot_v2) == 216);
static_assert(sizeof(worr_snapshot_store_slot_v2) == 576);
static_assert(offsetof(worr_snapshot_entity_v2, owner) == 124);
static_assert(offsetof(worr_snapshot_player_v2, movement) == 40);
static_assert(offsetof(worr_snapshot_v2, snapshot_hash) == 208);

int main()
{
    return 0;
}
