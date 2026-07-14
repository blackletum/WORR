#include "common/net/snapshot_recovery.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<worr_snapshot_recovery_config_v1>);
static_assert(std::is_trivially_copyable_v<worr_snapshot_recovery_config_v1>);
static_assert(std::is_standard_layout_v<worr_snapshot_recovery_observation_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_snapshot_recovery_observation_v1>);
static_assert(std::is_standard_layout_v<worr_snapshot_recovery_decision_v1>);
static_assert(std::is_trivially_copyable_v<worr_snapshot_recovery_decision_v1>);
static_assert(std::is_standard_layout_v<worr_snapshot_recovery_state_v1>);
static_assert(std::is_trivially_copyable_v<worr_snapshot_recovery_state_v1>);
static_assert(offsetof(worr_snapshot_recovery_state_v1, observations) == 48);
static_assert(offsetof(worr_snapshot_recovery_state_v1, recoveries) == 112);

int main()
{
    return 0;
}
