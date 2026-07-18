// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "cg_local.h"
#include "cg_prediction_config.hpp"

extern "C" void CG_GetPredictionConfigV1(
    worr_prediction_config_v1 *config)
{
    if (!config)
        return;

    *config = {};
    config->struct_size = sizeof(*config);
    config->schema_version = WORR_PREDICTION_ABI_VERSION;
    config->movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    config->air_acceleration = pm_config.airAccel;
    if (pm_config.n64Physics)
        config->flags |= WORR_PREDICTION_CONFIG_N64_PHYSICS;
    if (pm_config.q3Overbounce)
        config->flags |= WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE;
}
