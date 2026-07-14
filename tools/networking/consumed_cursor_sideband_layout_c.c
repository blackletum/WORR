#include "common/net/consumed_cursor_sideband.h"

#include <stddef.h>

int main(void)
{
    return sizeof(worr_consumed_cursor_setting_pair_v1) == 8 &&
                   sizeof(worr_consumed_cursor_sideband_v1) == 24 &&
                   offsetof(worr_consumed_cursor_sideband_v1,
                            consumed_cursor) == 8 &&
                   sizeof(worr_consumed_cursor_sideband_parser_v1) == 200 &&
                   offsetof(worr_consumed_cursor_sideband_parser_v1,
                            telemetry) == 48
               ? 0
               : 1;
}
