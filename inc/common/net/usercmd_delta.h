/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/command_canonical.h"
#include "q2proto/q2proto.h"
#include "shared/prediction_abi.h"
#include "shared/shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonicalize in place to the movement representation shared by prediction
 * and every supported live command codec.  Invalid input is rejected without
 * mutating the command.
 */
bool NetUsercmd_Canonicalize(usercmd_t *command);
bool NetUsercmd_CanonicalizeForTransport(usercmd_t *command,
                                         bool has_upmove);

/*
 * Convert to the canonical prediction ABI record.  Invalid input is rejected
 * without mutating the output, and the source and output may alias.
 */
bool NetUsercmd_ToPredictionCommandV1(
    const usercmd_t *command, worr_prediction_command_v1 *prediction_out);

/* Build/apply the engine-owned mapping around q2proto's wire delta record. */
bool NetUsercmd_BuildDelta(q2proto_clc_move_delta_t *delta_move,
                           const usercmd_t *from, const usercmd_t *command,
                           uint8_t lightlevel, bool has_upmove);
bool NetUsercmd_ApplyDelta(const q2proto_clc_move_delta_t *move_delta,
                           const usercmd_t *from, usercmd_t *command,
                           bool has_upmove);

#ifdef __cplusplus
}
#endif
