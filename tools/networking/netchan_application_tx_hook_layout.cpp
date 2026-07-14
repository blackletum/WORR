/* C++20 consumer and fixed-record layout proof for the dormant TX hook. */
#include "shared/shared.h"
#include "common/net/chan.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<netchan_app_tx_prepare_info_v1_t>);
static_assert(std::is_trivially_copyable_v<netchan_app_tx_prepare_info_v1_t>);
static_assert(sizeof(netchan_app_tx_prepare_info_v1_t) == 32);
static_assert(offsetof(netchan_app_tx_prepare_info_v1_t,
                       outgoing_sequence) == 8);
static_assert(offsetof(netchan_app_tx_prepare_info_v1_t,
                       reliable_bytes) == 16);
static_assert(offsetof(netchan_app_tx_prepare_info_v1_t,
                       packet_copies) == 28);

static_assert(std::is_standard_layout_v<netchan_app_tx_prepare_output_v1_t>);
static_assert(std::is_trivially_copyable_v<netchan_app_tx_prepare_output_v1_t>);
static_assert(sizeof(netchan_app_tx_prepare_output_v1_t) == 24);
static_assert(offsetof(netchan_app_tx_prepare_output_v1_t,
                       application_bytes) == 8);
static_assert(offsetof(netchan_app_tx_prepare_output_v1_t, token) == 16);

static_assert(std::is_standard_layout_v<netchan_app_tx_completion_info_v1_t>);
static_assert(std::is_trivially_copyable_v<netchan_app_tx_completion_info_v1_t>);
static_assert(sizeof(netchan_app_tx_completion_info_v1_t) == 32);
static_assert(offsetof(netchan_app_tx_completion_info_v1_t, result) == 8);
static_assert(offsetof(netchan_app_tx_completion_info_v1_t,
                       accepted_copies) == 16);
static_assert(offsetof(netchan_app_tx_completion_info_v1_t, token) == 24);

static_assert(NETCHAN_APP_TX_PREPARE_BYPASS == 0);
static_assert(NETCHAN_APP_TX_PREPARE_PREPARED == 1);
static_assert(NETCHAN_APP_TX_COMPLETION_ACCEPTED == 1);
static_assert(NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED == 2);
static_assert(NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID == 3);

int main()
{
    netchan_app_tx_prepare_info_v1_t prepare{};
    netchan_app_tx_prepare_output_v1_t output{};
    netchan_app_tx_completion_info_v1_t completion{};
    return prepare.struct_size == 0 && output.struct_size == 0 &&
                   completion.struct_size == 0
               ? 0
               : 1;
}
