/* C++20 consumer and fixed-record layout proof for the dormant RX hook. */
#include "shared/shared.h"
#include "common/net/chan.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<netchan_app_rx_info_v1_t>);
static_assert(std::is_trivially_copyable_v<netchan_app_rx_info_v1_t>);
static_assert(sizeof(netchan_app_rx_info_v1_t) == 32);
static_assert(offsetof(netchan_app_rx_info_v1_t, incoming_sequence) == 8);
static_assert(offsetof(netchan_app_rx_info_v1_t, message_bytes) == 12);
static_assert(offsetof(netchan_app_rx_info_v1_t, application_offset) == 16);
static_assert(offsetof(netchan_app_rx_info_v1_t, application_bytes) == 20);
static_assert(offsetof(netchan_app_rx_info_v1_t, flags) == 24);
static_assert(offsetof(netchan_app_rx_info_v1_t, reserved0) == 28);

static_assert(std::is_standard_layout_v<netchan_app_rx_output_v1_t>);
static_assert(std::is_trivially_copyable_v<netchan_app_rx_output_v1_t>);
static_assert(sizeof(netchan_app_rx_output_v1_t) == 16);
static_assert(offsetof(netchan_app_rx_output_v1_t, legacy_bytes) == 8);
static_assert(offsetof(netchan_app_rx_output_v1_t, reserved0) == 12);

static_assert(NETCHAN_APP_RX_HOOK_ABI_V1 == 1);
static_assert(NETCHAN_APP_RX_FLAG_RELIABLE == (1u << 0));
static_assert(NETCHAN_APP_RX_FLAG_REASSEMBLED == (1u << 1));
static_assert(NETCHAN_APP_RX_BYPASS == 0);
static_assert(NETCHAN_APP_RX_EXPOSE_LEGACY == 1);
static_assert(NETCHAN_APP_RX_REJECT == 2);
static_assert(NETCHAN_PROCESS_NO_APPLICATION == 0);
static_assert(NETCHAN_PROCESS_APPLICATION_READY == 1);
static_assert(NETCHAN_PROCESS_APPLICATION_REJECTED == 2);

int main()
{
    netchan_app_rx_info_v1_t info{};
    netchan_app_rx_output_v1_t output{};
    return info.struct_size == 0 && output.struct_size == 0 ? 0 : 1;
}
