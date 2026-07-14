#include "common/net/consumed_cursor_sideband.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<
              worr_consumed_cursor_setting_pair_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_consumed_cursor_setting_pair_v1>);
static_assert(std::is_standard_layout_v<worr_consumed_cursor_sideband_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_consumed_cursor_sideband_v1>);
static_assert(std::is_standard_layout_v<
              worr_consumed_cursor_sideband_parser_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_consumed_cursor_sideband_parser_v1>);
static_assert(sizeof(worr_consumed_cursor_sideband_parser_v1) == 200);

int main()
{
    return 0;
}
