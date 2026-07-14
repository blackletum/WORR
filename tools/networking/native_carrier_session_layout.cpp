/* C++20 consumer/layout proof for the native carrier/session adapter ABI. */
#include "common/net/native_carrier_session.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<worr_native_carrier_tx_gate_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_carrier_tx_gate_v1>);
static_assert(sizeof(worr_native_carrier_tx_gate_v1) == 80);
static_assert(offsetof(worr_native_carrier_tx_gate_v1, next_token_id) == 16);
static_assert(offsetof(worr_native_carrier_tx_gate_v1,
                       connection_owner_id) == 72);

static_assert(std::is_standard_layout_v<worr_native_carrier_dispatch_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_carrier_dispatch_v1>);
static_assert(sizeof(worr_native_carrier_dispatch_v1) == 248);
static_assert(offsetof(worr_native_carrier_dispatch_v1, token_id) == 16);
static_assert(offsetof(worr_native_carrier_dispatch_v1, send_ticket) == 48);
static_assert(offsetof(worr_native_carrier_dispatch_v1,
                       pending_fragmenter) ==
              offsetof(worr_native_carrier_dispatch_v1, fragmenter) +
                  sizeof(worr_native_envelope_fragmenter_v1));

int main()
{
    worr_native_carrier_tx_gate_v1 gate{};
    worr_native_carrier_dispatch_v1 dispatch{};
    return gate.struct_size == 0 && dispatch.struct_size == 0 ? 0 : 1;
}
