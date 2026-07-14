#include "shared/cgame_prediction.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<
              worr_cgame_prediction_input_command_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_cgame_prediction_input_range_v1>);
static_assert(sizeof(worr_cgame_prediction_input_range_v1) == 6248);
static_assert(offsetof(worr_cgame_prediction_input_range_v1, commands) == 104);

int main()
{
    return 0;
}
