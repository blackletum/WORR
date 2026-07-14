/* C++20 consumer/layout proof for the pointer-free WTC1 carrier ABI. */
#include "common/net/native_carrier.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<worr_native_carrier_entry_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_carrier_entry_v1>);
static_assert(std::is_standard_layout_v<worr_native_carrier_view_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_carrier_view_v1>);
static_assert(sizeof(worr_native_carrier_entry_v1) == 28);
static_assert(offsetof(worr_native_carrier_entry_v1, data_offset) == 8);
static_assert(offsetof(worr_native_carrier_entry_v1,
                       first_message_sequence) == 16);
static_assert(sizeof(worr_native_carrier_view_v1) == 256);
static_assert(offsetof(worr_native_carrier_view_v1, transport_epoch) == 8);
static_assert(offsetof(worr_native_carrier_view_v1, entries) == 32);
static_assert(WORR_NATIVE_CARRIER_MAX_PACKET_BYTES == 1200);
static_assert(WORR_NATIVE_CARRIER_MAX_ENTRIES == 8);
static_assert(WORR_NATIVE_CARRIER_WIRE_FOOTER_BYTES == 32);

int main()
{
    return 0;
}
