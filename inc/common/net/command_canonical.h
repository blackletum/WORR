/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_NET_USERCMD_MOVE_MIN (-512)
#define WORR_NET_USERCMD_MOVE_MAX 511

/* Canonical signed 16-bit short-angle and integer movement representations. */
bool NetUsercmd_CanonicalizeAngle(float input, float *output);
bool NetUsercmd_CanonicalizeMove(float input, float *output);

#ifdef __cplusplus
}
#endif
