/* Compile the public prediction schema as strict C11. */
#include "shared/prediction_abi.h"

int main(void)
{
    worr_prediction_state_v1 state = {0};
    worr_prediction_command_v1 command = {0};
    worr_prediction_config_v1 config = {0};
    worr_prediction_step_v1 step = {0};

    return sizeof(state) == 56 && sizeof(command) == 32 &&
                   sizeof(config) == 20 &&
                   step.touch_entity_ids[WORR_PREDICTION_MAX_TOUCH - 1] == 0
               ? 0
               : 1;
}
