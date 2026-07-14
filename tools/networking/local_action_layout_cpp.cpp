#include "shared/local_action_abi.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<worr_local_action_state_v2>);
static_assert(std::is_trivially_copyable_v<worr_local_action_state_v2>);
static_assert(std::is_standard_layout_v<worr_local_action_weapon_rule_v2>);
static_assert(
    std::is_trivially_copyable_v<worr_local_action_weapon_rule_v2>);
static_assert(std::is_standard_layout_v<worr_local_action_intent_v2>);
static_assert(std::is_trivially_copyable_v<worr_local_action_intent_v2>);
static_assert(std::is_standard_layout_v<worr_local_action_event_v2>);
static_assert(std::is_trivially_copyable_v<worr_local_action_event_v2>);
static_assert(std::is_standard_layout_v<worr_local_action_transaction_v2>);
static_assert(
    std::is_trivially_copyable_v<worr_local_action_transaction_v2>);
static_assert(std::is_standard_layout_v<worr_local_action_correction_v2>);
static_assert(
    std::is_trivially_copyable_v<worr_local_action_correction_v2>);
static_assert(sizeof(worr_local_action_transaction_v2) == 1216);
static_assert(offsetof(worr_local_action_transaction_v2, events) == 424);

int main()
{
    return 0;
}
