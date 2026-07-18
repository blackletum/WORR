/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/cgame_prediction.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CL_CGAME_PREDICTION_INPUT_DIAGNOSTICS_VERSION 1u

typedef struct cl_cgame_prediction_input_diagnostics_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t v1_requests;
    uint64_t v2_requests;
    uint64_t canonical_successes;
    uint64_t legacy_fallbacks;
    uint64_t failures;
    uint32_t last_result;
    uint32_t last_source;
    uint32_t last_flags;
    uint32_t last_authoritative_legacy_sequence;
    uint32_t last_current_legacy_sequence;
    uint32_t last_command_count;
} cl_cgame_prediction_input_diagnostics_v1;

const worr_cgame_prediction_input_import_v1 *
CL_GetCGamePredictionInputImportV1(void);

const worr_cgame_prediction_input_import_v2 *
CL_GetCGamePredictionInputImportV2(void);

void CL_ResetCGamePredictionInputDiagnostics(void);
bool CL_GetCGamePredictionInputDiagnostics(
    cl_cgame_prediction_input_diagnostics_v1 *diagnostics_out);

#ifdef __cplusplus
}
#endif
