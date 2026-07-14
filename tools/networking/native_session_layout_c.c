#include "common/net/native_session.h"

#include <stddef.h>

typedef bool (*binding_from_readiness_fn_v1)(
    worr_native_session_binding_v1 *,
    const worr_native_readiness_state_v1 *,
    uint64_t);

_Static_assert(
    _Generic(&Worr_NativeSessionBindingInitFromReadinessV1,
             binding_from_readiness_fn_v1: 1,
             default: 0),
    "readiness binding initializer signature");

_Static_assert(sizeof(worr_native_session_binding_v1) == 24,
               "binding layout");
_Static_assert(offsetof(worr_native_session_binding_v1,
                        connection_owner_id) == 16,
               "binding owner offset");
_Static_assert(sizeof(worr_native_ack_range_v1) == 32,
               "ack layout");
_Static_assert(offsetof(worr_native_ack_range_v1,
                        connection_owner_id) == 24,
               "ack owner offset");
_Static_assert(sizeof(worr_native_tx_slot_v1) == 64,
               "tx slot layout");
_Static_assert(offsetof(worr_native_tx_slot_v1, enqueue_dispatch) == 48,
               "tx dispatch offset");
_Static_assert(sizeof(worr_native_tx_send_ticket_v1) == 104,
               "tx send ticket layout");
_Static_assert(offsetof(worr_native_tx_send_ticket_v1, pre_send_slot) == 32,
               "tx send ticket slot offset");
_Static_assert(offsetof(worr_native_tx_send_ticket_v1,
                        connection_owner_id) == 96,
               "tx send ticket owner offset");
_Static_assert(sizeof(worr_native_tx_telemetry_v1) == 160,
               "tx telemetry layout");
_Static_assert(sizeof(worr_native_tx_session_v1) == 216,
               "tx session layout");
_Static_assert(offsetof(worr_native_tx_session_v1, telemetry) == 48,
               "tx telemetry offset");
_Static_assert(offsetof(worr_native_tx_session_v1,
                        connection_owner_id) == 208,
               "tx session owner offset");
_Static_assert(sizeof(worr_native_rx_slot_v1) == 88,
               "rx slot layout");
_Static_assert(sizeof(worr_native_receipt_history_entry_v1) == 32,
               "receipt identity layout");
_Static_assert(sizeof(worr_native_snapshot_identity_v1) == 32,
               "snapshot identity layout");
_Static_assert(sizeof(worr_native_rx_telemetry_v1) == 192,
               "rx telemetry layout");
_Static_assert(sizeof(worr_native_rx_session_v1) == 2824,
               "rx session layout");
_Static_assert(offsetof(worr_native_rx_session_v1, receipt_mask) == 48,
               "receipt mask offset");
_Static_assert(offsetof(worr_native_rx_session_v1, history) == 64,
               "receipt identities offset");
_Static_assert(offsetof(worr_native_rx_session_v1, snapshot_tombstones) == 2112,
               "snapshot identities offset");
_Static_assert(offsetof(worr_native_rx_session_v1, telemetry) == 2624,
               "rx telemetry offset");
_Static_assert(offsetof(worr_native_rx_session_v1,
                        connection_owner_id) == 2816,
               "rx session owner offset");
_Static_assert(sizeof(worr_native_rx_message_v1) == 56,
               "rx message layout");
_Static_assert(offsetof(worr_native_rx_message_v1,
                        connection_owner_id) == 48,
               "rx message owner offset");

int main(void)
{
    return 0;
}
