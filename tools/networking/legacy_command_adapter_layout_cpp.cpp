#include "common/net/legacy_command_adapter.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<worr_legacy_command_setting_pair_v1>);
static_assert(std::is_standard_layout_v<worr_legacy_command_range_v1>);
static_assert(std::is_standard_layout_v<
              worr_legacy_command_sideband_parser_v1>);
static_assert(std::is_standard_layout_v<
              worr_legacy_command_adapter_report_v1>);

int main()
{
    return sizeof(worr_legacy_command_sideband_parser_v1) == 216 ? 0 : 1;
}
