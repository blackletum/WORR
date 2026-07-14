#include "common/net/native_session.h"

#include <cstddef>
#include <type_traits>

using binding_from_readiness_fn_v1 = bool (*)(
    worr_native_session_binding_v1 *,
    const worr_native_readiness_state_v1 *,
    uint64_t);

static_assert(std::is_same_v<
              decltype(&Worr_NativeSessionBindingInitFromReadinessV1),
              binding_from_readiness_fn_v1>);
static_assert(std::is_standard_layout_v<worr_native_session_binding_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_session_binding_v1>);
static_assert(sizeof(worr_native_session_binding_v1) == 24);
static_assert(offsetof(worr_native_session_binding_v1,
                       connection_owner_id) == 16);
static_assert(sizeof(worr_native_ack_range_v1) == 32);
static_assert(offsetof(worr_native_ack_range_v1,
                       connection_owner_id) == 24);
static_assert(sizeof(worr_native_tx_slot_v1) == 64);
static_assert(offsetof(worr_native_tx_slot_v1, enqueue_dispatch) == 48);
static_assert(sizeof(worr_native_tx_send_ticket_v1) == 104);
static_assert(offsetof(worr_native_tx_send_ticket_v1, pre_send_slot) == 32);
static_assert(offsetof(worr_native_tx_send_ticket_v1,
                       connection_owner_id) == 96);
static_assert(std::is_standard_layout_v<worr_native_tx_send_ticket_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_tx_send_ticket_v1>);
static_assert(sizeof(worr_native_tx_telemetry_v1) == 160);
static_assert(sizeof(worr_native_tx_session_v1) == 216);
static_assert(offsetof(worr_native_tx_session_v1, telemetry) == 48);
static_assert(offsetof(worr_native_tx_session_v1,
                       connection_owner_id) == 208);
static_assert(std::is_standard_layout_v<worr_native_tx_session_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_tx_session_v1>);
static_assert(sizeof(worr_native_rx_slot_v1) == 88);
static_assert(sizeof(worr_native_receipt_history_entry_v1) == 32);
static_assert(sizeof(worr_native_snapshot_identity_v1) == 32);
static_assert(sizeof(worr_native_rx_telemetry_v1) == 192);
static_assert(sizeof(worr_native_rx_session_v1) == 2824);
static_assert(offsetof(worr_native_rx_session_v1, receipt_mask) == 48);
static_assert(offsetof(worr_native_rx_session_v1, history) == 64);
static_assert(offsetof(worr_native_rx_session_v1, snapshot_tombstones) == 2112);
static_assert(offsetof(worr_native_rx_session_v1, telemetry) == 2624);
static_assert(offsetof(worr_native_rx_session_v1,
                       connection_owner_id) == 2816);
static_assert(std::is_standard_layout_v<worr_native_rx_session_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_rx_session_v1>);
static_assert(sizeof(worr_native_rx_message_v1) == 56);
static_assert(offsetof(worr_native_rx_message_v1,
                       connection_owner_id) == 48);

int main()
{
    return 0;
}
