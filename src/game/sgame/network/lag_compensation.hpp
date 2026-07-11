// Copyright (c) 2026 The WORR Project
// Licensed under the GNU General Public License 2.0.

#pragma once

#include "../g_local.hpp"

// Initializes the bounded history and its sgame-owned policy cvars.  The
// legacy g_lag_compensation switch remains the compatibility master switch.
void LagCompensation_Init();
void LagCompensation_Shutdown();

// Captures one authoritative full collision pose after the client end-frame
// has finalized origin, stance, bounds, and linkage.
void LagCompensation_RecordFrame(gentity_t *entity);

// Historical collision helpers.  They restore the authoritative world before
// returning.  The implementation clips against unlinked historical player
// proxies, so live world state is never rewound or relinked and callers may
// safely apply damage, knockback, death, effects, and piercing state.
trace_t LagCompensation_TraceLine(gentity_t *from_player,
                                  const Vector3 &start,
                                  const Vector3 &end,
                                  gentity_t *pass_entity,
                                  contents_t content_mask);
trace_t LagCompensation_Trace(gentity_t *from_player,
                              const Vector3 &start,
                              const Vector3 &mins,
                              const Vector3 &maxs,
                              const Vector3 &end,
                              gentity_t *pass_entity,
                              contents_t content_mask);
