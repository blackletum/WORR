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

#define WORR_PREDICTION_INPUT_RESOLVER_VERSION 1u

enum {
    WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_CAPABILITY = UINT32_C(1) << 0,
    WORR_PREDICTION_INPUT_RESOLVE_CANONICAL_ESTABLISHED = UINT32_C(1) << 1,
};

/* Engine-private request for the pure range resolver.  history is a borrowed
 * candidate set for this call; the result never retains it. */
typedef struct worr_prediction_input_resolve_request_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t identity_initial_epoch;
    uint32_t identity_baseline_legacy_sequence;
    uint32_t current_legacy_sequence;
    uint32_t legacy_acknowledged_sequence;
    uint32_t history_count;
    worr_snapshot_consumed_command_v2 consumed_command;
    uint32_t pending_present;
    uint32_t reserved0;
    worr_cgame_prediction_input_command_v1 pending_command;
    const worr_cgame_prediction_input_command_v1 *history;
} worr_prediction_input_resolve_request_v1;

uint32_t Worr_PredictionInputResolveV1(
    const worr_prediction_input_resolve_request_v1 *request,
    worr_cgame_prediction_input_range_v1 *range_out);

#ifdef __cplusplus
}
#endif
