/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/command_canonical.h"

#include <math.h>
#include <stdint.h>

bool NetUsercmd_CanonicalizeAngle(float input, float *output)
{
    if (!output || !isfinite(input))
        return false;

    /* Match ANGLE2SHORT after first bounding accumulated multi-turn input. */
    const float wrapped = fmodf(input, 360.0f);
    const int32_t encoded =
        (int32_t)((wrapped * 65536.0f) / 360.0f);
    const uint32_t bits = (uint32_t)encoded & UINT32_C(0xffff);
    const int32_t signed_value =
        bits >= UINT32_C(0x8000) ? (int32_t)bits - 65536 : (int32_t)bits;
    *output = (float)signed_value * (360.0f / 65536.0f);
    return true;
}

bool NetUsercmd_CanonicalizeMove(float input, float *output)
{
    if (!output || !isfinite(input))
        return false;

    const float integral = truncf(input);
    if (integral < (float)WORR_NET_USERCMD_MOVE_MIN ||
        integral > (float)WORR_NET_USERCMD_MOVE_MAX) {
        return false;
    }
    *output = integral == 0.0f ? 0.0f : integral;
    return true;
}
