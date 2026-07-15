#include "common/net/native_carrier_mixed.h"

#include <cstddef>
#include <type_traits>

using mixed_confirm_fn_v1 = worr_native_carrier_mixed_result_v1 (*)(
    worr_native_carrier_tx_gate_v1 *, worr_native_tx_session_v1 *,
    worr_native_tx_slot_v1 *, uint16_t, worr_native_carrier_dispatch_v1 *,
    worr_native_carrier_ack_ledger_v1 *,
    const worr_native_carrier_mixed_token_v1 *, uint64_t, const void *,
    size_t);

static_assert(std::is_standard_layout_v<worr_native_carrier_mixed_token_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_carrier_mixed_token_v1>);
static_assert(sizeof(worr_native_carrier_mixed_token_v1) == 168);
static_assert(offsetof(worr_native_carrier_mixed_token_v1,
                       connection_owner_id) == 24);
static_assert(offsetof(worr_native_carrier_mixed_token_v1, ack_token) == 48);
static_assert(std::is_same_v<decltype(&Worr_NativeCarrierMixedConfirmPacketV1),
                             mixed_confirm_fn_v1>);

int main()
{
    worr_native_carrier_mixed_token_v1 token{};
    return token.struct_size == 0 ? 0 : 1;
}
