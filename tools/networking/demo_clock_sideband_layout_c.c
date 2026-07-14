#include "common/net/demo_clock_sideband.h"

_Static_assert(sizeof(worr_demo_clock_setting_pair_v1) == 8,
               "demo clock pair C layout");
_Static_assert(sizeof(worr_demo_clock_anchor_v1) == 32,
               "demo clock anchor C layout");
_Static_assert(offsetof(worr_demo_clock_anchor_v1, server_time_us) == 16,
               "demo clock time C offset");
_Static_assert(sizeof(worr_demo_clock_sideband_parser_v1) == 48,
               "demo clock parser C layout");

int main(void)
{
    return 0;
}
