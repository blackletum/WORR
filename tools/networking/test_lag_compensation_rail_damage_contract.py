#!/usr/bin/env python3
"""Regression contract for FR-10-T11 live railgun damage evidence."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAG_COMPENSATION = (
    ROOT / "src/game/sgame/network/lag_compensation.cpp"
).read_text(encoding="utf-8")
WEAPON = (ROOT / "src/game/sgame/gameplay/g_weapon.cpp").read_text(
    encoding="utf-8"
)
SERVER_COMMANDS = (ROOT / "src/game/sgame/gameplay/g_svcmds.cpp").read_text(
    encoding="utf-8"
)
RUNNER = (ROOT / "tools/networking/run_rewind_rail_damage_runtime_gate.py").read_text(
    encoding="utf-8"
)


def function_body(source: str, signature: str, successor: str) -> str:
    start = source.index(signature)
    end = source.index(successor, start)
    return source[start:end]


class LagCompensationRailDamageContractTests(unittest.TestCase):
    def test_production_railgun_keeps_the_shared_rewind_query_and_damage_route(self) -> None:
        rail = function_body(WEAPON, "void fire_rail(", "static Vector3 bfg_laser_pos")
        self.assertIn("WORR_REWIND_WEAPON_RAILGUN", rail)
        self.assertIn("pierce_trace(start, end, self, args, mask)", rail)
        pierce = function_body(WEAPON, "void pierce_trace(", "struct fire_lead_pierce_t")
        self.assertIn("LagCompensation_TraceLine(", pierce)
        self.assertIn("Damage(tr.ent, self, self", WEAPON)

    def test_probe_rejects_unbounded_authority_before_proving_recorded_rewind_damage(self) -> None:
        probe = function_body(
            LAG_COMPENSATION,
            "bool LagCompensation_RunRailDamageRuntimeProbe(",
            "trace_t LagCompensation_TraceLine",
        )
        for requirement in (
            "NativeTrace(start, nullptr, nullptr, end, shooter",
            "shooter->client->cmd.serverFrame = currentFrame",
            "WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED",
            "HistoricalAcknowledgement",
            "fireHistorical(nearAcknowledgement, near)",
            "fireHistorical(boundedAcknowledgement, bounded)",
            "fireHistorical(cappedAcknowledgement, capped)",
            "fire_rail(shooter, start, Vector3{1.0f, 0.0f, 0.0f}",
            "WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT",
            "WORR_REWIND_WEAPON_RAILGUN",
            "WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED",
            "probe->damage_amount",
            "probe->geometry_unchanged",
            "probe->near_latency_hit",
            "probe->bounded_latency_hit",
            "probe->capped_latency_hit",
        ):
            self.assertIn(requirement, probe)
        self.assertLess(
            probe.index("shooter->client->cmd.serverFrame = currentFrame"),
            probe.index("fireHistorical(nearAcknowledgement, near)"),
        )

    def test_fixture_arm_uses_real_bots_and_only_prevents_floorless_map_fall(self) -> None:
        arm = function_body(
            LAG_COMPENSATION,
            "bool LagCompensation_ArmRailDamageRuntimeProbe()",
            "bool LagCompensation_RunRailDamageRuntimeProbe(",
        )
        for requirement in (
            "candidate->inUse",
            "candidate->client",
            "player->gravity = 0.0f",
            "player->velocity = {}",
            "gi.linkEntity(player)",
        ):
            self.assertIn(requirement, arm)

    def test_console_and_runner_stay_dedicated_and_explicitly_opt_in(self) -> None:
        self.assertIn("SVCmd_RewindRailDamageArm_f", SERVER_COMMANDS)
        self.assertIn("SVCmd_RewindRailDamageSelfTest_f", SERVER_COMMANDS)
        self.assertIn('"worr_rewind_rail_damage_arm"', SERVER_COMMANDS)
        self.assertIn('"worr_rewind_rail_damage_selftest"', SERVER_COMMANDS)
        for requirement in (
            '"g_lag_compensation", "1"',
            '"sg_lag_compensation_debug", "2"',
            '"worr_rewind_rail_damage_arm"',
            '"worr_rewind_rail_damage_selftest"',
            '"RewindRailShooter"',
            '"RewindRailTarget"',
        ):
            self.assertIn(requirement, RUNNER)


if __name__ == "__main__":
    unittest.main()
