#!/usr/bin/env python3
"""Contracts for the two-process canonical rail-damage runtime gate."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/networking/run_canonical_rail_damage_runtime_gate.py"
SPEC = importlib.util.spec_from_file_location("canonical_rail_damage_runtime_gate", SCRIPT)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)

PASS_LINE = (
    'sg_worr_rewind_canonical_rail_damage_status '
    '"pass:1:1:1:1:1:1:1:1:1:6:50000:0:2:2:1:1:0:63:0:1:1:6:50000:65000:70000:70000:50000:6:0:6:5:80:80:0:0:0:0:0:0:0"'
)


class CanonicalRailDamageRuntimeGateTests(unittest.TestCase):
    def test_server_is_dedicated_and_client_is_headless_input_free(self) -> None:
        server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960, Path("C:/runtime/server"),
        )
        client = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.SHOOTER_NAME,
            Path("C:/runtime/shooter"),
        )
        self.assertEqual(server[0], str(Path("C:/stage/worr_ded_x86_64.exe")))
        self.assertIn("rcon_password", server)
        self.assertEqual(server[server.index("fs_homepath") + 1], str(Path("C:/runtime/server")))
        self.assertEqual(server[server.index("sg_lag_compensation_interp_ms") + 1], "50")
        self.assertNotIn("addbot", server)
        self.assertNotIn("worr_rewind_canonical_rail_damage_arm", server)
        self.assertNotIn("+stuffall", server)
        self.assertIn('stuffall "cmd team free"', SCRIPT.read_text(encoding="utf-8"))
        self.assertIn('stuff {shooter_user_id} "+attack"', SCRIPT.read_text(encoding="utf-8"))
        self.assertIn('stuff {shooter_user_id} "-attack; +attack"', SCRIPT.read_text(encoding="utf-8"))
        self.assertIn('stuff {shooter_user_id} "-attack; +moveup; -moveup"', SCRIPT.read_text(encoding="utf-8"))
        self.assertIn("refresh_held_attack", SCRIPT.read_text(encoding="utf-8"))
        self.assertEqual(GATE.GATE_MODES["railgun"]["weapon_policy"], 5)
        self.assertEqual(GATE.GATE_MODES["machinegun"]["weapon_policy"], 1)
        self.assertEqual(GATE.GATE_MODES["chaingun"]["weapon_policy"], 2)
        self.assertEqual(GATE.GATE_MODES["super-shotgun"]["weapon_policy"], 4)
        self.assertEqual(GATE.GATE_MODES["disruptor"]["weapon_policy"], 6)
        self.assertEqual(GATE.GATE_MODES["plasma-beam"]["weapon_policy"], 7)
        self.assertEqual(GATE.GATE_MODES["plasma-beam-held"]["expected_damage"], 24)
        self.assertTrue(GATE.GATE_MODES["plasma-beam-held"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["plasma-beam-sustained"]["expected_damage"], 256)
        self.assertNotIn("refresh_held_attack", GATE.GATE_MODES["plasma-beam-sustained"])
        self.assertTrue(GATE.GATE_MODES["plasma-beam-sustained"]["require_sustained_hold"])
        self.assertTrue(GATE.GATE_MODES["plasma-beam-release"]["release_after_expected_damage"])
        self.assertEqual(GATE.GATE_MODES["plasma-beam-water-retrace"]["expected_damage"], 4)
        self.assertTrue(GATE.GATE_MODES["plasma-beam-water-retrace"]["require_water_retrace"])
        self.assertEqual(GATE.GATE_MODES["thunderbolt"]["weapon_policy"], 8)
        self.assertEqual(GATE.GATE_MODES["thunderbolt-held"]["expected_damage"], 24)
        self.assertTrue(GATE.GATE_MODES["thunderbolt-held"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["thunderbolt-sustained"]["expected_damage"], 256)
        self.assertNotIn("refresh_held_attack", GATE.GATE_MODES["thunderbolt-sustained"])
        self.assertTrue(GATE.GATE_MODES["thunderbolt-sustained"]["require_sustained_hold"])
        self.assertTrue(GATE.GATE_MODES["thunderbolt-release"]["release_after_expected_damage"])
        self.assertEqual(GATE.GATE_MODES["thunderbolt-water-retrace"]["expected_damage"], 4)
        self.assertTrue(GATE.GATE_MODES["thunderbolt-water-retrace"]["require_water_retrace"])
        self.assertEqual(GATE.GATE_MODES["thunderbolt-discharge"]["expected_damage"], 70)
        self.assertTrue(GATE.GATE_MODES["thunderbolt-discharge"]["require_thunderbolt_discharge"])
        self.assertTrue(GATE.GATE_MODES["thunderbolt-discharge"]["current_authority_discharge"])
        self.assertEqual(GATE.GATE_MODES["shotgun"]["weapon_policy"], 3)
        self.assertLess(
            SCRIPT.read_text(encoding="utf-8").index("worr_rewind_canonical_rail_damage_arm"),
            SCRIPT.read_text(encoding="utf-8").index("time.sleep(1.2)"),
        )
        self.assertIn("warmup_enabled", server)
        self.assertNotIn("worr_x86_64.exe", " ".join(server))
        self.assertEqual(client[0], str(Path("C:/stage/worr_x86_64.exe")))
        self.assertEqual(client[client.index("fs_homepath") + 1], str(Path("C:/runtime/shooter")))
        for name, value in (("win_headless", "1"), ("in_enable", "0"), ("in_grab", "0")):
            index = client.index(name)
            self.assertEqual(client[index + 1], value)
        self.assertEqual(client[client.index("cl_async") + 1], "1")
        self.assertNotIn("+attack", client)
        self.assertEqual(client[-3:], ["+set", "name", GATE.SHOOTER_NAME])
        self.assertIn("net_impair_enable", client)
        self.assertEqual(client[client.index("net_impair_latency_ms") + 1], "50")

    def test_status_parser_selects_the_two_admitted_real_clients(self) -> None:
        response = (
            "print\nnum score ping name\n"
            "  4     0    3 rail_shooter  10 127.0.0.1:50001\n"
            "  7     0    4 rail_target   8 127.0.0.1:50002\n"
        )
        self.assertEqual(GATE.admitted_fixture_user_ids(response), (4, 7))

    def test_parser_requires_every_canonical_weapon_proof(self) -> None:
        status = GATE.parse_status(PASS_LINE)
        self.assertEqual(GATE.validate_status(status), status)
        self.assertEqual(status["target_history_captures"], 6)
        self.assertEqual(status["applied_age_us"], 50_000)
        self.assertEqual(status["trace_current_time_us"], 70_000)
        self.assertEqual(status["context_snapshot_time_us"], 70_000)
        self.assertEqual(status["context_mapped_time_us"], 50_000)
        self.assertEqual(status["target_capture_prepares"], 6)
        self.assertEqual(status["capture_append_rejections"], 0)
        self.assertEqual(status["target_capture_callbacks"], 6)
        self.assertEqual(status["observation_weapon_policy"], 5)
        self.assertEqual(status["expected_damage"], 80)
        self.assertEqual(status["observed_damage"], 80)
        self.assertEqual(status["water_retrace_required"], 0)
        self.assertEqual(status["water_retrace_observed"], 0)
        self.assertEqual(status["thunderbolt_discharge_required"], 0)
        self.assertEqual(status["thunderbolt_discharge_ammo_drained"], 0)
        self.assertEqual(status["thunderbolt_discharge_observed"], 0)
        self.assertEqual(status["sustained_hold_required"], 0)
        self.assertEqual(status["sustained_hold_interrupted"], 0)

    def test_machinegun_mode_requires_its_own_policy_and_damage(self) -> None:
        mode = GATE.GATE_MODES["machinegun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":1:8:8:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_policy = dict(status)
        wrong_policy["observation_weapon_policy"] = 5
        with self.assertRaisesRegex(RuntimeError, "wrong weapon policy"):
            GATE.validate_status(wrong_policy, mode)

    def test_shotgun_mode_requires_all_pellet_damage(self) -> None:
        mode = GATE.GATE_MODES["shotgun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":3:48:48:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_pellet = dict(status)
        missing_pellet["observed_damage"] = 44
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_pellet, mode)

    def test_chaingun_mode_requires_all_three_burst_rounds(self) -> None:
        mode = GATE.GATE_MODES["chaingun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":2:18:18:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_round = dict(status)
        missing_round["observed_damage"] = 12
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_round, mode)

    def test_super_shotgun_mode_requires_both_full_pellet_barrels(self) -> None:
        mode = GATE.GATE_MODES["super-shotgun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":4:120:120:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_barrel = dict(status)
        missing_barrel["observed_damage"] = 60
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_barrel, mode)

    def test_disruptor_mode_requires_its_full_delayed_damage(self) -> None:
        mode = GATE.GATE_MODES["disruptor"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":6:45:45:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        incomplete_daemon = dict(status)
        incomplete_daemon["observed_damage"] = 36
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(incomplete_daemon, mode)

    def test_plasma_beam_mode_requires_its_first_held_command_tick(self) -> None:
        mode = GATE.GATE_MODES["plasma-beam"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":7:8:8:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_tick = dict(status)
        missing_tick["observed_damage"] = 0
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_tick, mode)

    def test_thunderbolt_mode_requires_one_deduplicated_footprint_damage(self) -> None:
        mode = GATE.GATE_MODES["thunderbolt"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":8:8:8:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        duplicate_ray_damage = dict(status)
        duplicate_ray_damage["observed_damage"] = 24
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(duplicate_ray_damage, mode)

    def test_plasma_beam_held_mode_requires_three_normal_ticks(self) -> None:
        mode = GATE.GATE_MODES["plasma-beam-held"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":7:24:24:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_tick = dict(status)
        missing_tick["observed_damage"] = 16
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_tick, mode)

    def test_thunderbolt_held_mode_requires_three_deduplicated_ticks(self) -> None:
        mode = GATE.GATE_MODES["thunderbolt-held"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":8:24:24:0:0:0:0:0:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        duplicate_ray_damage = dict(status)
        duplicate_ray_damage["observed_damage"] = 32
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(duplicate_ray_damage, mode)

    def test_sustained_beam_modes_require_all_thirty_two_normal_ticks(self) -> None:
        for mode_name, policy in (("plasma-beam-sustained", 7),
                                  ("thunderbolt-sustained", 8)):
            mode = GATE.GATE_MODES[mode_name]
            line = PASS_LINE.replace(
                "sg_worr_rewind_canonical_rail_damage_status",
                str(mode["status_cvar"]),
            ).replace(":5:80:80:0:0:0:0:0:0:0\"", f":{policy}:256:256:0:0:0:0:0:1:0\"")
            status = GATE.parse_status(line, str(mode["status_cvar"]))
            self.assertEqual(GATE.validate_status(status, mode), status)
            missing_tick = dict(status)
            missing_tick["observed_damage"] = 248
            with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
                GATE.validate_status(missing_tick, mode)
            interrupted = dict(status)
            interrupted["sustained_hold_interrupted"] = 1
            with self.assertRaisesRegex(RuntimeError, "sustained held attack"):
                GATE.validate_status(interrupted, mode)

    def test_water_retrace_modes_require_halved_damage_and_ordered_retrace(self) -> None:
        for mode_name, policy in (("plasma-beam-water-retrace", 7),
                                  ("thunderbolt-water-retrace", 8)):
            mode = GATE.GATE_MODES[mode_name]
            line = PASS_LINE.replace(
                "sg_worr_rewind_canonical_rail_damage_status",
                str(mode["status_cvar"]),
            ).replace(":5:80:80:0:0:0:0:0:0:0\"", f":{policy}:4:4:1:1:0:0:0:0:0\"")
            status = GATE.parse_status(line, str(mode["status_cvar"]))
            self.assertEqual(GATE.validate_status(status, mode), status)
            missing_retrace = dict(status)
            missing_retrace["water_retrace_observed"] = 0
            with self.assertRaisesRegex(RuntimeError, "water retrace"):
                GATE.validate_status(missing_retrace, mode)

    def test_thunderbolt_discharge_requires_self_damage_and_ammo_drain(self) -> None:
        mode = GATE.GATE_MODES["thunderbolt-discharge"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0\"", ":8:70:70:0:0:1:1:1:0:0\"")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        no_drain = dict(status)
        no_drain["thunderbolt_discharge_ammo_drained"] = 0
        with self.assertRaisesRegex(RuntimeError, "Thunderbolt discharge"):
            GATE.validate_status(no_drain, mode)
        no_observation = dict(status)
        no_observation["thunderbolt_discharge_observed"] = 0
        with self.assertRaisesRegex(RuntimeError, "Thunderbolt discharge"):
            GATE.validate_status(no_observation, mode)

    def test_parser_rejects_missing_history_or_current_time_selection(self) -> None:
        no_history = PASS_LINE.replace(":6:50000:0", ":5:50000:0")
        with self.assertRaisesRegex(RuntimeError, "pre-fire target history"):
            GATE.validate_status(GATE.parse_status(no_history))
        current_time = PASS_LINE.replace(":6:50000:0", ":6:0:0")
        with self.assertRaisesRegex(RuntimeError, "earlier authoritative instant"):
            GATE.validate_status(GATE.parse_status(current_time))

    def test_determinism_signature_ignores_runtime_clock_samples(self) -> None:
        baseline = GATE.parse_status(PASS_LINE)
        later = dict(baseline)
        later["applied_age_us"] = 64_000
        later["target_history_count"] = 128
        later["observation_applied_time_us"] = 1_024_000
        later["latest_capture_time_us"] = 1_088_000
        later["trace_current_time_us"] = 1_088_000
        later["context_snapshot_time_us"] = 1_088_000
        later["context_mapped_time_us"] = 1_024_000
        later["target_capture_prepares"] = 64
        later["target_capture_callbacks"] = 64
        self.assertEqual(
            GATE.determinism_signature(baseline),
            GATE.determinism_signature(later),
        )

    def test_runtime_launches_use_no_window_and_devnull(self) -> None:
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertIn("CREATE_NO_WINDOW", source)
        self.assertGreaterEqual(source.count("stdin=subprocess.DEVNULL"), 2)


if __name__ == "__main__":
    unittest.main()
