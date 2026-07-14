#include "common/net/snapshot_timeline.h"

#include <cstddef>
#include <cstdlib>
#include <type_traits>

static_assert(std::is_standard_layout_v<worr_snapshot_timeline_pair_v1>);
static_assert(std::is_standard_layout_v<
              worr_snapshot_timeline_entity_sample_v1>);
static_assert(sizeof(worr_snapshot_timeline_slot_v1) == 592);
static_assert(sizeof(worr_snapshot_timeline_entity_sample_v1) == 216);
static_assert(offsetof(worr_snapshot_timeline_entity_sample_v1, entity) ==
              72);
static_assert(offsetof(worr_snapshot_timeline_clock_state_v1,
                       fractional_q16) == 44);

int main()
{
    return EXIT_SUCCESS;
}
