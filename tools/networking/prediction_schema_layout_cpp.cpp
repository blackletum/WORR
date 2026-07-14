/* Compile the public prediction schema as C++20. */
#include "shared/prediction_abi.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<worr_prediction_state_v1>);
static_assert(std::is_standard_layout_v<worr_prediction_command_v1>);
static_assert(std::is_standard_layout_v<worr_prediction_config_v1>);
static_assert(std::is_standard_layout_v<worr_prediction_trace_v1>);
static_assert(std::is_standard_layout_v<worr_prediction_step_v1>);

int main()
{
    return WORR_PREDICTION_ABI_VERSION == 1 &&
                   WORR_PREDICTION_MODEL_REVISION == 1
               ? 0
               : 1;
}
