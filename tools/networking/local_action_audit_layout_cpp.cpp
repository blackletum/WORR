/* Compile the local-action audit declarations and layouts as C++20. */
#include "common/net/local_action_audit.h"

#include <cstddef>
#include <type_traits>

template <typename T>
constexpr bool value_record =
    std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

static_assert(value_record<worr_local_action_audit_slot_v1>);
static_assert(value_record<worr_local_action_audit_telemetry_v1>);
static_assert(value_record<worr_local_action_audit_v1>);
static_assert(sizeof(worr_local_action_audit_slot_v1) == 2528);
static_assert(sizeof(worr_local_action_audit_telemetry_v1) == 248);
static_assert(offsetof(worr_local_action_audit_slot_v1, command_id) == 16);
static_assert(offsetof(worr_local_action_audit_slot_v1, first_arrival_serial) ==
              24);
static_assert(offsetof(worr_local_action_audit_slot_v1, predicted) == 32);
static_assert(offsetof(worr_local_action_audit_slot_v1, authoritative) == 1248);
static_assert(offsetof(worr_local_action_audit_slot_v1, correction) == 2464);
static_assert(std::is_pointer_v<decltype(worr_local_action_audit_v1::slots)>);
static_assert(
    !std::is_pointer_v<decltype(worr_local_action_audit_slot_v1::predicted)>);
static_assert(
    std::is_same_v<decltype(&Worr_LocalActionAuditValidateOperationalV2),
                   bool (*)(const worr_local_action_audit_v1 *)>);
static_assert(std::is_same_v<decltype(&Worr_LocalActionAuditValidateDeepV2),
                             bool (*)(const worr_local_action_audit_v1 *)>);

int main() {
  return WORR_LOCAL_ACTION_AUDIT_VERSION == 1 &&
                 WORR_LOCAL_ACTION_AUDIT_OPERATIONAL_API_VERSION == 2 &&
                 WORR_LOCAL_ACTION_AUDIT_MAX_CAPACITY == 512
             ? 0
             : 1;
}
