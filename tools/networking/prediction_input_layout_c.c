#include "shared/cgame_prediction.h"

#include <stddef.h>

_Static_assert(sizeof(worr_cgame_prediction_input_command_v1) == 48,
               "C command layout drift");
_Static_assert(sizeof(worr_cgame_prediction_input_range_v1) == 6248,
               "C range layout drift");
_Static_assert(offsetof(worr_cgame_prediction_input_range_v1, commands) == 104,
               "C command-array offset drift");

int main(void)
{
    return 0;
}
