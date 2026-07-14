// Copyright (c) 2026 The WORR Project
// Licensed under the GNU General Public License 2.0.

#pragma once

#include "../g_local.hpp"
#include "common/net/rewind_observation.h"

// Initializes the bounded history and its sgame-owned policy cvars.  The
// legacy g_lag_compensation switch remains the compatibility master switch.
void LagCompensation_Init();
void LagCompensation_Shutdown();
// Clears map-local pose/scene and per-connection policy progression.  Command
// session epochs are server-owned and need not be adjacent across a new map.
// Call once before the new map's entities are populated.
void LagCompensation_ResetMap();
// Clears one connection slot's policy, history, and frozen-scene state.  A
// globally allocated command-session epoch is not assumed adjacent to the
// previous occupant's epoch.
void LagCompensation_ResetClient(gentity_t *entity);
// Starts a distinct player life after ClientSpawn has selected a valid spawn.
// Historical poses and cached collision state from the prior life cannot be
// resolved through the new generation.
void LagCompensation_BeginClientLife(gentity_t *entity);

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
                                  contents_t content_mask,
                                  gentity_t *const *ignored_entities = nullptr,
                                  size_t ignored_entity_count = 0,
                                  uint32_t weapon_policy =
                                      WORR_REWIND_WEAPON_UNSPECIFIED);
trace_t LagCompensation_Trace(gentity_t *from_player,
                              const Vector3 &start,
                              const Vector3 &mins,
                              const Vector3 &maxs,
                              const Vector3 &end,
                              gentity_t *pass_entity,
                              contents_t content_mask,
                              uint32_t weapon_policy =
                                  WORR_REWIND_WEAPON_UNSPECIFIED);

// Copies the oldest-to-newest immutable trace observations currently retained
// by the opt-in debug journal.  Passing null records with zero capacity queries
// the available count.  The journal is disabled when
// sg_lag_compensation_debug is below 2.
bool LagCompensation_CopyObservations(
    worr_rewind_observation_v1 *records, uint32_t records_capacity,
    uint32_t *record_count);
bool LagCompensation_GetObservationTelemetry(
    worr_rewind_observation_telemetry_v1 *telemetry);
