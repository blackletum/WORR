/* C++20 consumer/layout proof for retained WTC1 acknowledgement state. */
#include "common/net/native_carrier_ack.h"

#include <cstddef>
#include <type_traits>

using ack_peek_due_fn_v1 = worr_native_carrier_ack_result_v1 (*)(
    const worr_native_carrier_ack_ledger_v1 *, uint64_t, uint32_t);
static_assert(std::is_same_v<
              decltype(&Worr_NativeCarrierAckPeekDueV1),
              ack_peek_due_fn_v1>);

static_assert(
    std::is_standard_layout_v<worr_native_carrier_ack_receipt_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_carrier_ack_receipt_v1>);
static_assert(sizeof(worr_native_carrier_ack_receipt_v1) == 24);
static_assert(offsetof(worr_native_carrier_ack_receipt_v1,
                       last_handoff_tick) == 8);

static_assert(
    std::is_standard_layout_v<worr_native_carrier_ack_telemetry_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_carrier_ack_telemetry_v1>);
static_assert(sizeof(worr_native_carrier_ack_telemetry_v1) == 64);

static_assert(
    std::is_standard_layout_v<worr_native_carrier_ack_token_range_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_carrier_ack_token_range_v1>);
static_assert(sizeof(worr_native_carrier_ack_token_range_v1) == 8);

static_assert(
    std::is_standard_layout_v<worr_native_carrier_ack_emit_token_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_carrier_ack_emit_token_v1>);
static_assert(sizeof(worr_native_carrier_ack_emit_token_v1) == 120);
static_assert(offsetof(worr_native_carrier_ack_emit_token_v1,
                       connection_owner_id) == 16);
static_assert(offsetof(worr_native_carrier_ack_emit_token_v1,
                       packet_crc32) == 48);
static_assert(offsetof(worr_native_carrier_ack_emit_token_v1, ranges) == 56);

static_assert(std::is_standard_layout_v<worr_native_carrier_ack_ledger_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_native_carrier_ack_ledger_v1>);
static_assert(sizeof(worr_native_carrier_ack_ledger_v1) == 2152);
static_assert(offsetof(worr_native_carrier_ack_ledger_v1,
                       connection_owner_id) == 16);
static_assert(offsetof(worr_native_carrier_ack_ledger_v1, telemetry) == 48);
static_assert(offsetof(worr_native_carrier_ack_ledger_v1,
                       active_token) == 112);
static_assert(offsetof(worr_native_carrier_ack_ledger_v1, receipts) == 232);

int main()
{
    worr_native_carrier_ack_ledger_v1 ledger{};
    return ledger.struct_size == 0 ? 0 : 1;
}
