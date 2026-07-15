#!/usr/bin/env python3
"""Source contract for the production-owned canonical rail acceptance seam."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAG = (ROOT / "src/game/sgame/network/lag_compensation.cpp").read_text(encoding="utf-8")
WEAPON = (ROOT / "src/game/sgame/gameplay/g_weapon.cpp").read_text(encoding="utf-8")
CLIENT_THINK = (ROOT / "src/game/sgame/client/client_session_service_impl.cpp").read_text(encoding="utf-8")
SVCMDS = (ROOT / "src/game/sgame/gameplay/g_svcmds.cpp").read_text(encoding="utf-8")
GAME_IMPORT_C = (ROOT / "inc/shared/game.h").read_text(encoding="utf-8")
GAME_IMPORT_CPP = (ROOT / "src/game/bgame/game.hpp").read_text(encoding="utf-8")
SERVER_GAME = (ROOT / "src/server/game.c").read_text(encoding="utf-8")


class CanonicalRailDamageContractTests(unittest.TestCase):
    def test_pose_history_uses_the_engine_snapshot_time_domain(self) -> None:
        self.assertIn("uint64_t (*ServerSimulationTimeUs)(void);", GAME_IMPORT_C)
        self.assertIn("uint64_t\t(*ServerSimulationTimeUs)();", GAME_IMPORT_CPP)
        provider = SERVER_GAME[
            SERVER_GAME.index("static uint64_t PF_ServerSimulationTimeUs(void)") :
            SERVER_GAME.index("static void PF_SendToClipboard")
        ]
        self.assertIn("return sv.worr_server_time_us;", provider)
        self.assertIn(".ServerSimulationTimeUs = PF_ServerSimulationTimeUs,", SERVER_GAME)

        current_clock = LAG[
            LAG.index("[[nodiscard]] bool CurrentAuthoritativeTimeUs") :
            LAG.index("[[nodiscard]] bool CompletedAuthoritativeFrameTimeUs")
        ]
        self.assertIn("if (!gi.ServerSimulationTimeUs)", current_clock)
        self.assertIn("timeUs = gi.ServerSimulationTimeUs();", current_clock)
        self.assertNotIn("level.time", current_clock)

    def test_end_frame_pose_capture_timestamps_completed_engine_frames(self) -> None:
        completed_clock = LAG[
            LAG.index("[[nodiscard]] bool CompletedAuthoritativeFrameTimeUs") :
            LAG.index("[[nodiscard]] Vector3 ToVector")
        ]
        self.assertIn("CurrentAuthoritativeTimeUs(currentTimeUs)", completed_clock)
        self.assertIn("static_cast<uint64_t>(gi.frameTimeMs) * UINT64_C(1000)", completed_clock)
        self.assertIn("timeUs = currentTimeUs + frameTimeUs;", completed_clock)

        player_capture = LAG[
            LAG.index("void LagCompensation_RecordFrame") :
            LAG.index("void LagCompensation_RecordMovers")
        ]
        self.assertIn("CompletedAuthoritativeFrameTimeUs(timeUs)", player_capture)
        self.assertIn("!track.initialized &&", player_capture)
        self.assertIn("InitTrack(track, static_cast<uint32_t>(index + 1u))", player_capture)

        mover_capture = LAG[
            LAG.index("void LagCompensation_RecordMovers") :
            LAG.index("bool LagCompensation_RunHistoricalBrushRuntimeProbe")
        ]
        self.assertIn("CompletedAuthoritativeFrameTimeUs(timeUs)", mover_capture)

    def test_fixture_requires_real_active_command_scope_and_attack_bit(self) -> None:
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalRailProbeArm()")]
        for required in (
            "WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID",
            "commandContextImport->GetCurrent",
            "context.client_index != ClientIndex(entity)",
            "(command->buttons & BUTTON_ATTACK) == 0",
            "canonicalRailProbe.command_id = context.command.command_id",
            "entity->client->buttons = BUTTON_NONE",
        ):
            self.assertIn(required, prepare)
        self.assertIn("entity->s.modelIndex = MODELINDEX_PLAYER", LAG)
        self.assertIn("entity->client->pers.healthBonus = 0", LAG)
        self.assertNotIn("fire_rail(", prepare)

    def test_normal_weapon_callback_remains_the_only_fixture_fire_route(self) -> None:
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalRailProbeArm()")]
        self.assertIn("weapon->weaponThink", prepare)
        self.assertIn("LagCompensation_PrepareCanonicalWeaponDamageCommand(ent, ucmd)", CLIENT_THINK)
        self.assertLess(
            CLIENT_THINK.index("LagCompensation_PrepareCanonicalWeaponDamageCommand(ent, ucmd)"),
            CLIENT_THINK.index("cl->buttons = ucmd->buttons"),
        )
        self.assertIn("worr_rewind_canonical_rail_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_machinegun_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_chaingun_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_super_shotgun_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_disruptor_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_held_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_sustained_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_release_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_water_retrace_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_held_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_sustained_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_release_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_water_retrace_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_discharge_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_shotgun_damage_arm", SVCMDS)

    def test_terminal_proof_binds_trace_to_the_same_command_and_live_damage(self) -> None:
        observe = LAG[LAG.rindex("void CanonicalRailProbeObserveTrace"):LAG.rindex("void CanonicalRailProbePrepareCommand")]
        for required in (
            "SameCommand(observation.command_id, canonicalRailProbe.command_id)",
            "WORR_REWIND_OBSERVATION_PATH_CANONICAL",
            "WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT",
            "WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED",
            "current_geometry_unchanged",
            "observation.weapon_policy != canonicalRailProbe.weapon_policy",
        ):
            self.assertIn(required, observe)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("healthBefore - healthAfter", capture)
        self.assertIn("canonicalRailProbe.expected_damage", capture)
        prepare_frame = LAG[
            LAG.rindex("void CanonicalRailProbePrepareFrameCapture"):
            LAG.rindex("void CanonicalRailProbeCaptureFrame")
        ]
        self.assertIn("CanonicalRailProbeStage::WaitingForCanonicalAttack", prepare_frame)

    def test_machinegun_fixture_uses_the_normal_weapon_callback_and_policy(self) -> None:
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalHitscanProbeArm")]
        for required in (
            "GetItemByIndex(canonicalRailProbe.weapon_item)",
            "entity->client->pers.weapon = weapon",
            "entity->client->ps.gunFrame = canonicalRailProbe.weapon_idle_frame",
            "canonicalRailProbe.weapon_policy == WORR_REWIND_WEAPON_UNSPECIFIED",
        ):
            self.assertIn(required, prepare)
        arm = LAG[LAG.rindex("bool CanonicalMachinegunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_MACHINEGUN", arm)
        self.assertIn("IT_WEAPON_MACHINEGUN", arm)
        self.assertIn("kCanonicalMachinegunProbeExpectedDamage", arm)

    def test_chaingun_fixture_requires_the_full_normal_three_round_burst(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalChaingunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_CHAINGUN", arm)
        self.assertIn("IT_WEAPON_CHAINGUN", arm)
        self.assertIn("kCanonicalChaingunProbeExpectedDamage", arm)
        self.assertIn("kCanonicalChaingunProbeExpectedDamage, 14", arm)

    def test_super_shotgun_fixture_requires_both_full_pellet_barrels(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalSuperShotgunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_SUPER_SHOTGUN", arm)
        self.assertIn("IT_WEAPON_SSHOTGUN", arm)
        self.assertIn("kCanonicalSuperShotgunProbeExpectedDamage", arm)
        self.assertIn("18, 64.0f", arm)

    def test_disruptor_fixture_waits_for_normal_projectile_and_daemon_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalDisruptorProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE", arm)
        self.assertIn("IT_WEAPON_DISRUPTOR", arm)
        self.assertIn("kCanonicalDisruptorProbeExpectedDamage", arm)
        self.assertIn("UINT64_C(1500000)", arm)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("damage_settle_deadline_us", capture)
        self.assertIn("if (!damageSettled)", capture)

    def test_plasma_beam_fixture_proves_one_normal_held_command_tick(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_BEAM", arm)
        self.assertIn("IT_WEAPON_PLASMABEAM", arm)
        self.assertIn("kCanonicalPlasmaBeamProbeExpectedDamage", arm)
        self.assertIn("13, 112.0f, 0", arm)
        self.assertIn("first ordinary deathmatch beam tick only", arm)

    def test_plasma_beam_held_fixture_waits_for_three_normal_ticks(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamHeldProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_BEAM", arm)
        self.assertIn("IT_WEAPON_PLASMABEAM", arm)
        self.assertIn("kCanonicalPlasmaBeamHeldProbeExpectedDamage", arm)
        self.assertIn("UINT64_C(1500000)", arm)
        self.assertIn("{32.0f, 0.0f, 0.0f}", arm)
        self.assertIn("later commands query their newer history", arm)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("damageApplied || deadlineReached", capture)
        self.assertIn("defer_damage_evaluation_until_deadline", capture)

    def test_plasma_beam_sustained_fixture_requires_the_full_cell_budget(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamSustainedProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalPlasmaBeamSustainedProbeExpectedDamage", arm)
        self.assertIn("kCanonicalBeamSustainedProbeCells", arm)
        self.assertIn("kCanonicalBeamSustainedProbeTimeoutUs", arm)
        self.assertIn("32 normal 8-damage ticks", arm)
        self.assertIn("requires two Cells to begin a tick", arm)
        self.assertIn("kCanonicalBeamSustainedProbeCells = 33", LAG)
        self.assertIn("kCanonicalBeamSustainedProbeAngleLockDuration = 6_sec", LAG)
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalHitscanProbeArm")]
        self.assertIn("sustained_hold_interrupted", prepare)
        self.assertIn("CanonicalRailProbeFail(20)", prepare)
        self.assertIn("kCanonicalBeamSustainedProbeAngleLockDuration", prepare)

    def test_sustained_beam_fixture_pins_only_knockback_displacement(self) -> None:
        pin = LAG[LAG.index("void CanonicalRailProbePinTarget"):LAG.index("[[nodiscard]] bool CanonicalRailProbeSelectPlayers")]
        self.assertIn("entity->velocity = {};", pin)
        self.assertNotIn("entity->health", pin)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("canonicalRailProbe.sustained_hold_required", capture)
        self.assertIn("CanonicalRailProbePinTarget", capture)

    def test_plasma_beam_release_fixture_requires_real_no_attack_and_stable_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamReleaseProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_BEAM", arm)
        self.assertIn("kCanonicalPlasmaBeamReleaseProbeExpectedDamage", arm)
        self.assertIn("true);", arm)
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalHitscanProbeArm")]
        self.assertIn("release_required", prepare)
        self.assertIn("(command->buttons & BUTTON_ATTACK) == 0", prepare)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("release_damage_stable", capture)
        self.assertIn("release_grace_deadline_us", capture)

    def test_plasma_beam_water_retrace_requires_the_production_halved_damage_proof(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamWaterRetraceProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalPlasmaBeamWaterRetraceProbeExpectedDamage", arm)
        self.assertIn("224.0f", arm)
        self.assertIn("{112.0f, 0.0f, 0.0f}", arm)
        observe = LAG[LAG.rindex("void CanonicalRailProbeObserveTrace"):LAG.rindex("void CanonicalRailProbePrepareCommand")]
        self.assertIn("historicalWaterHit", observe)
        self.assertIn("water_trace_observation_sequence", observe)
        self.assertIn("water_retrace_observed", observe)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("fire_beams/fire_thunderbolt halve damage only after", capture)

    def test_thunderbolt_fixture_proves_one_normal_deduplicated_footprint(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_THUNDERBOLT", arm)
        self.assertIn("IT_WEAPON_THUNDERBOLT", arm)
        self.assertIn("kCanonicalThunderboltProbeExpectedDamage", arm)
        self.assertIn("3, 112.0f, 0", arm)
        self.assertIn("main/side-ray footprint and target de-duplication", arm)

    def test_thunderbolt_held_fixture_waits_for_three_deduplicated_ticks(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltHeldProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_THUNDERBOLT", arm)
        self.assertIn("IT_WEAPON_THUNDERBOLT", arm)
        self.assertIn("kCanonicalThunderboltHeldProbeExpectedDamage", arm)
        self.assertIn("UINT64_C(1500000)", arm)
        self.assertIn("{32.0f, 0.0f, 0.0f}", arm)

    def test_thunderbolt_sustained_fixture_requires_the_full_cell_budget(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltSustainedProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalThunderboltSustainedProbeExpectedDamage", arm)
        self.assertIn("kCanonicalBeamSustainedProbeCells", arm)
        self.assertIn("kCanonicalBeamSustainedProbeTimeoutUs", arm)
        self.assertIn("target de-duplication", arm)

    def test_thunderbolt_release_fixture_requires_real_no_attack_and_stable_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltReleaseProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_THUNDERBOLT", arm)
        self.assertIn("kCanonicalThunderboltReleaseProbeExpectedDamage", arm)
        self.assertIn("true);", arm)

    def test_thunderbolt_water_retrace_retains_normal_target_deduplication(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltWaterRetraceProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalThunderboltWaterRetraceProbeExpectedDamage", arm)
        self.assertIn("4 damage", arm)
        self.assertIn("de-duplication", arm)

    def test_thunderbolt_discharge_is_explicit_current_authority_and_drains_cells(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltDischargeProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalThunderboltDischargeProbeExpectedDamage", arm)
        self.assertIn("current-authority radius policy", arm)
        self.assertIn("{0.0f, 400.0f, 0.0f}", arm)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("thunderbolt_discharge_ammo_drained", capture)
        self.assertIn("thunderboltDischargeProof", capture)
        self.assertIn("shooter_health_before", capture)
        self.assertIn("LagCompensation_ObserveThunderboltUnderwaterDischarge", LAG)
        self.assertIn("LagCompensation_ObserveThunderboltUnderwaterDischarge", WEAPON)

    def test_shotgun_fixture_requires_the_full_normal_pellet_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalShotgunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_SHOTGUN", arm)
        self.assertIn("IT_WEAPON_SHOTGUN", arm)
        self.assertIn("kCanonicalShotgunProbeExpectedDamage", arm)


if __name__ == "__main__":
    unittest.main()
