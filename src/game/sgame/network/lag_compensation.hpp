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

// Captures map-owned brush movers after all end-frame player state is final.
// The resulting poses populate immutable rewind scenes; historical brush
// tracing remains deliberately separate from this capture boundary.
void LagCompensation_RecordMovers();

// Console-scoped headless acceptance probe for the packaged inline-BSP fixture.
// It captures a brush at its original transform, moves only its live collider,
// then proves the sealed trace blocks at the captured transform without
// mutating the authoritative collision state. It is never run by gameplay.
struct LagCompensationHistoricalBrushRuntimeProbe {
  bool setup_ready = false;
  bool scene_ready = false;
  bool rotation_applied = false;
  bool rider_setup = false;
  bool rider_frame_continuity = false;
  bool rider_frame_scene_sealed = false;
  bool rider_provenance_sealed = false;
  bool fixture_reference_blocked = false;
  bool rotation_control_unblocked = false;
  bool baseline_unblocked = false;
  bool historical_dispatched = false;
  bool historical_blocked = false;
  bool authority_unchanged = false;
  uint32_t candidate_count = 0;
  uint32_t rider_continuity_samples = 0;
  float baseline_fraction = 1.0f;
  float historical_fraction = 1.0f;
};

bool LagCompensation_RunHistoricalBrushRuntimeProbe(
    LagCompensationHistoricalBrushRuntimeProbe *probe);

// Arms the packaged moving-brush fixture with a real engine bot before a
// deliberate series of ordinary server frames. This console-only diagnostic
// seam is consumed by the subsequent runtime probe; it is never gameplay.
bool LagCompensation_ArmHistoricalBrushRuntimeProbe();

// Console-scoped headless acceptance probe for the live railgun path. It uses
// two dedicated-server bots solely to supply real player entities and ordinary
// frame history. The probe first verifies that an invalid acknowledgement stays
// uncompensated, then fires the production railgun through a bounded recorded
// acknowledgement classes (near, in-budget, and capped) and proves
// current-authority damage is applied only after each historical collision
// query succeeds. It is never run by gameplay.
struct LagCompensationRailDamageRuntimeProbe {
  bool setup_ready = false;
  bool history_ready = false;
  bool current_world_miss = false;
  bool rejected_current_fallback = false;
  bool rejected_no_damage = false;
  bool legacy_rewind_selected = false;
  bool rail_policy_observed = false;
  bool near_latency_hit = false;
  bool bounded_latency_hit = false;
  bool capped_latency_hit = false;
  bool damage_applied = false;
  bool geometry_unchanged = false;
  bool query_authority_unchanged = false;
  uint32_t candidate_count = 0;
  uint32_t damage_amount = 0;
  float current_fraction = 1.0f;
  float near_latency_fraction = 1.0f;
  float bounded_latency_fraction = 1.0f;
  float capped_latency_fraction = 1.0f;
};

// Holds two real dedicated-server bots in a bounded no-gravity fixture state
// long enough for ordinary end-frame history capture. It is only meaningful
// immediately before LagCompensation_RunRailDamageRuntimeProbe().
bool LagCompensation_ArmRailDamageRuntimeProbe();

bool LagCompensation_RunRailDamageRuntimeProbe(
    LagCompensationRailDamageRuntimeProbe *probe);

// Arms a two-process, headless-only canonical-command Railgun fixture. Unlike
// the console rail probe above, this seam does not create a command context or
// call fire_rail itself. A real non-bot client must reach ClientThink through
// the negotiated command-sideband carrier with BUTTON_ATTACK held; the normal
// Railgun weapon callback then owns the trace and damage.
bool LagCompensation_ArmCanonicalRailDamageRuntimeProbe();

// Arms the equivalent real-command machinegun fixture. It only stages the
// isolated player geometry; normal Item::weaponThink remains the sole weapon
// dispatch path and must consume the received BUTTON_ATTACK command.
bool LagCompensation_ArmCanonicalMachinegunDamageRuntimeProbe();

// Arms the real-command Chaingun fixture. It starts the normal Chaingun
// callback at its three-round firing stage; the fixture supplies neither a
// trace nor a damage result.
bool LagCompensation_ArmCanonicalChaingunDamageRuntimeProbe();

// Arms the real-command Super Shotgun fixture. Normal Item::weaponThink owns
// both ten-pellet barrels and their current-authority damage application.
bool LagCompensation_ArmCanonicalSuperShotgunDamageRuntimeProbe();

// Arms the real-command Disruptor convergence fixture. The normal weapon owns
// target selection, projectile flight, and its delayed damage daemon.
bool LagCompensation_ArmCanonicalDisruptorDamageRuntimeProbe();

// Arms one real held-command Plasma Beam tick. The normal repeating weapon
// state and fire_beams callback remain responsible for the canonical trace and
// current-authority damage; sustained/release lifecycle coverage is separate.
bool LagCompensation_ArmCanonicalPlasmaBeamDamageRuntimeProbe();

// Arms a held-command Plasma Beam cadence fixture. The normal repeating state
// must reach three 8-damage ticks before a bounded timeout; release and water
// lifecycle coverage is separate.
bool LagCompensation_ArmCanonicalPlasmaBeamHeldDamageRuntimeProbe();

// Arms a bounded long-held Plasma Beam fixture. The real held client command
// must sustain the normal repeating weapon cadence through all supplied Cells.
bool LagCompensation_ArmCanonicalPlasmaBeamSustainedDamageRuntimeProbe();

// Arms a held-command Plasma Beam release fixture. After three normal ticks,
// a real no-attack command must stop further damage through a bounded grace
// interval; water and long-duration behavior remain separate.
bool LagCompensation_ArmCanonicalPlasmaBeamReleaseDamageRuntimeProbe();

// Arms a real-command Plasma Beam crossing of the fixture func_water. The
// normal beam path must observe water, retrace without it, and apply its own
// halved current-authority damage to the historical target.
bool LagCompensation_ArmCanonicalPlasmaBeamWaterRetraceDamageRuntimeProbe();

// Arms one real held-command dry-world Thunderbolt tick. The normal
// fire_thunderbolt implementation owns the complete main/side-ray footprint
// and target de-duplication; discharge and held/release coverage is separate.
bool LagCompensation_ArmCanonicalThunderboltDamageRuntimeProbe();

// Arms a held-command dry-world Thunderbolt cadence fixture. The production
// main/side-ray footprint must reach three de-duplicated ticks before timeout;
// discharge, release, and water lifecycle coverage is separate.
bool LagCompensation_ArmCanonicalThunderboltHeldDamageRuntimeProbe();

// Arms a bounded long-held dry-world Thunderbolt fixture. The real held client
// command must sustain the normal repeating cadence and target de-duplication.
bool LagCompensation_ArmCanonicalThunderboltSustainedDamageRuntimeProbe();

// Arms the dry-world Thunderbolt release fixture. Its production footprint
// must stop after a real no-attack command and bounded grace interval;
// discharge, water, and long-duration behavior remain separate.
bool LagCompensation_ArmCanonicalThunderboltReleaseDamageRuntimeProbe();

// Arms a dry-shooter Thunderbolt crossing of the fixture func_water. The
// normal path must observe water, retrace, and retain side-ray de-duplication.
bool LagCompensation_ArmCanonicalThunderboltWaterRetraceDamageRuntimeProbe();

// Arms an underwater Thunderbolt discharge. The radius/self-damage policy is
// current-authority; a real command must drain cells and apply normal self damage.
bool LagCompensation_ArmCanonicalThunderboltDischargeDamageRuntimeProbe();

// Observes the real production underwater Thunderbolt discharge after its
// authoritative self damage has been applied. It is a no-op unless the
// console-only canonical discharge fixture is awaiting that same shooter.
void LagCompensation_ObserveThunderboltUnderwaterDischarge(gentity_t *shooter);

// Arms the real-command shotgun fixture. The fixture cannot inject pellet
// results: normal Item::weaponThink and fire_shotgun own every trace and
// current-authority damage application.
bool LagCompensation_ArmCanonicalShotgunDamageRuntimeProbe();

// Called at the beginning of the production ClientThink path.  It is a no-op
// unless the console-only fixture is armed.  It never mutates a user command,
// accepts a client hit result, or begins a command-context scope.
void LagCompensation_PrepareCanonicalWeaponDamageCommand(gentity_t *entity,
                                                          usercmd_t *command);

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
