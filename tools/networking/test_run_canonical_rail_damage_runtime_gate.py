#!/usr/bin/env python3
"""Contracts for the two-process canonical rail-damage runtime gate."""

from __future__ import annotations

import importlib.util
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/networking/run_canonical_rail_damage_runtime_gate.py"
SPEC = importlib.util.spec_from_file_location("canonical_rail_damage_runtime_gate", SCRIPT)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)

PASS_LINE = (
    'sg_worr_rewind_canonical_rail_damage_status '
    '"pass:1:1:1:1:1:1:1:1:1:6:50000:0:2:2:1:1:0:63:0:1:1:6:50000:65000:70000:70000:50000:6:0:6:5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:1:1:8:1:0:0:4:2:0:1:9:1:1:1:1:1:1:7:4367:1"'
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
        self.assertEqual(
            server[server.index("sg_lag_compensation_projectile_forward_ms") + 1],
            "100",
        )
        self.assertEqual(
            server[server.index("sg_lag_compensation_melee_max_displacement") + 1],
            "64",
        )
        self.assertNotIn("addbot", server)
        self.assertNotIn("worr_rewind_canonical_rail_damage_arm", server)
        self.assertNotIn("+stuffall", server)
        self.assertIn('stuffall "cmd team free"', SCRIPT.read_text(encoding="utf-8"))
        self.assertIn(
            'input_command = str(mode.get("input_command", "+attack"))',
            SCRIPT.read_text(encoding="utf-8"),
        )
        self.assertIn(
            'stuff {shooter_user_id} "-attack; +attack; +moveup; -moveup"',
            SCRIPT.read_text(encoding="utf-8"),
        )
        self.assertIn('stuff {shooter_user_id} "-attack; +moveup; -moveup"', SCRIPT.read_text(encoding="utf-8"))
        self.assertIn("refresh_held_attack", SCRIPT.read_text(encoding="utf-8"))
        self.assertEqual(GATE.GATE_MODES["railgun"]["weapon_policy"], 5)
        self.assertEqual(
            GATE.GATE_MODES["railgun-mover-occlusion"]["weapon_policy"], 5
        )
        self.assertEqual(
            GATE.GATE_MODES["railgun-mover-occlusion"]["status_cvar"],
            "sg_worr_rewind_canonical_rail_mover_occlusion_status",
        )
        self.assertFalse(
            GATE.GATE_MODES["railgun-mover-occlusion"]["require_damage"]
        )
        self.assertTrue(
            GATE.GATE_MODES["railgun-mover-occlusion"][
                "require_historical_mover_occlusion"
            ]
        )
        self.assertEqual(GATE.GATE_MODES["machinegun"]["weapon_policy"], 1)
        self.assertEqual(GATE.GATE_MODES["chaingun"]["weapon_policy"], 2)
        self.assertEqual(GATE.GATE_MODES["super-shotgun"]["weapon_policy"], 4)
        self.assertEqual(GATE.GATE_MODES["disruptor"]["weapon_policy"], 6)
        self.assertTrue(GATE.GATE_MODES["disruptor"]["require_projectile_forward"])
        self.assertEqual(GATE.GATE_MODES["rocket"]["weapon_policy"], 9)
        self.assertEqual(GATE.GATE_MODES["rocket"]["expected_damage"], 100)
        self.assertTrue(GATE.GATE_MODES["rocket"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["rocket"]["current_authority_projectile"])
        self.assertEqual(GATE.GATE_MODES["plasma-gun-splash"]["weapon_policy"], 10)
        self.assertEqual(GATE.GATE_MODES["plasma-gun-splash"]["expected_damage"], 7)
        self.assertEqual(
            GATE.GATE_MODES["plasma-gun-splash"]["status_cvar"],
            "sg_worr_rewind_canonical_plasma_gun_splash_damage_status",
        )
        self.assertTrue(
            GATE.GATE_MODES["plasma-gun-splash"]["require_projectile_forward"]
        )
        self.assertTrue(
            GATE.GATE_MODES["plasma-gun-splash"]["current_authority_projectile"]
        )
        self.assertTrue(
            GATE.GATE_MODES["plasma-gun-splash"]["require_current_authority_splash"]
        )
        self.assertTrue(
            GATE.GATE_MODES["plasma-gun-splash"]["require_reduced_splash"]
        )
        self.assertEqual(GATE.GATE_MODES["bfg"]["weapon_policy"], 18)
        self.assertEqual(GATE.GATE_MODES["bfg"]["expected_damage"], 200)
        self.assertEqual(
            GATE.GATE_MODES["bfg"]["status_cvar"],
            "sg_worr_rewind_canonical_bfg_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["bfg"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["bfg"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["bfg"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["ion-ripper"]["weapon_policy"], 19)
        self.assertEqual(GATE.GATE_MODES["ion-ripper"]["expected_damage"], 10)
        self.assertEqual(
            GATE.GATE_MODES["ion-ripper"]["status_cvar"],
            "sg_worr_rewind_canonical_ion_ripper_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["ion-ripper"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["ion-ripper"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["ion-ripper"]["require_damage"])
        self.assertEqual(
            GATE.GATE_MODES["ion-ripper"]["expected_projectile_forward_launches"],
            15,
        )
        self.assertEqual(GATE.GATE_MODES["tesla-mine"]["weapon_policy"], 20)
        self.assertEqual(GATE.GATE_MODES["tesla-mine"]["expected_damage"], 3)
        self.assertEqual(
            GATE.GATE_MODES["tesla-mine"]["status_cvar"],
            "sg_worr_rewind_canonical_tesla_mine_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["tesla-mine"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["tesla-mine"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["tesla-mine"]["require_damage"])
        self.assertTrue(
            GATE.GATE_MODES["tesla-mine"][
                "release_held_attack_after_attack_received"
            ]
        )
        self.assertEqual(GATE.GATE_MODES["trap"]["weapon_policy"], 21)
        self.assertEqual(GATE.GATE_MODES["trap"]["expected_damage"], 20)
        self.assertEqual(
            GATE.GATE_MODES["trap"]["status_cvar"],
            "sg_worr_rewind_canonical_trap_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["trap"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["trap"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["trap"]["require_damage"])
        self.assertTrue(
            GATE.GATE_MODES["trap"][
                "release_held_attack_after_attack_received"
            ]
        )
        self.assertEqual(GATE.GATE_MODES["grapple"]["weapon_policy"], 22)
        self.assertEqual(GATE.GATE_MODES["grapple"]["expected_damage"], 1)
        self.assertEqual(
            GATE.GATE_MODES["grapple"]["status_cvar"],
            "sg_worr_rewind_canonical_grapple_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["grapple"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["grapple"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["grapple"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["offhand-hook"]["weapon_policy"], 24)
        self.assertEqual(GATE.GATE_MODES["offhand-hook"]["input_command"], "+hook")
        self.assertTrue(GATE.GATE_MODES["offhand-hook"]["enable_offhand_hook"])
        self.assertTrue(
            GATE.GATE_MODES["offhand-hook"]["require_projectile_forward"]
        )
        self.assertTrue(
            GATE.GATE_MODES["offhand-hook"]["current_authority_projectile"]
        )
        self.assertFalse(GATE.GATE_MODES["offhand-hook"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["proball-throw"]["weapon_policy"], 23)
        self.assertEqual(GATE.GATE_MODES["proball-throw"]["expected_damage"], 1)
        self.assertEqual(
            GATE.GATE_MODES["proball-throw"]["status_cvar"],
            "sg_worr_rewind_canonical_proball_throw_status",
        )
        self.assertTrue(
            GATE.GATE_MODES["proball-throw"]["require_projectile_forward"]
        )
        self.assertTrue(
            GATE.GATE_MODES["proball-throw"]["current_authority_projectile"]
        )
        self.assertFalse(GATE.GATE_MODES["proball-throw"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["proball-throw"]["gametype"], 17)
        self.assertTrue(GATE.GATE_MODES["proball-throw"]["team_game"])
        self.assertEqual(GATE.GATE_MODES["grenade-launcher"]["weapon_policy"], 15)
        self.assertEqual(GATE.GATE_MODES["grenade-launcher"]["expected_damage"], 60)
        self.assertEqual(GATE.GATE_MODES["grenade-launcher"]["minimum_damage"], 57)
        self.assertTrue(GATE.GATE_MODES["grenade-launcher"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["grenade-launcher"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["grenade-launcher"]["require_current_authority_splash"])
        self.assertTrue(GATE.GATE_MODES["grenade-launcher"]["require_reduced_splash"])
        self.assertEqual(GATE.GATE_MODES["hand-grenade"]["weapon_policy"], 16)
        self.assertEqual(GATE.GATE_MODES["hand-grenade"]["expected_damage"], 60)
        self.assertEqual(GATE.GATE_MODES["hand-grenade"]["minimum_damage"], 57)
        self.assertTrue(GATE.GATE_MODES["hand-grenade"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["hand-grenade"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["hand-grenade"]["require_damage"])
        self.assertFalse(GATE.GATE_MODES["hand-grenade"]["release_held_attack_flush"])
        self.assertTrue(
            GATE.GATE_MODES["hand-grenade"][
                "release_held_attack_after_attack_received"
            ]
        )
        self.assertGreater(
            float(GATE.GATE_MODES["hand-grenade"]["release_held_attack_after_seconds"]),
            1.0,
        )
        self.assertEqual(GATE.GATE_MODES["hand-grenade-splash"]["weapon_policy"], 16)
        self.assertTrue(
            GATE.GATE_MODES["hand-grenade-splash"]["require_current_authority_splash"]
        )
        self.assertGreater(
            float(
                GATE.GATE_MODES["hand-grenade-splash"][
                    "release_held_attack_after_seconds"
                ]
            ),
            1.0,
        )
        self.assertEqual(GATE.GATE_MODES["prox-launcher"]["weapon_policy"], 17)
        self.assertEqual(GATE.GATE_MODES["prox-launcher"]["expected_damage"], 90)
        self.assertTrue(GATE.GATE_MODES["prox-launcher"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["prox-launcher"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["prox-launcher"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["prox-launcher-lifecycle"]["weapon_policy"], 17)
        self.assertEqual(
            GATE.GATE_MODES["prox-launcher-lifecycle"]["expected_damage"], 61,
        )
        self.assertTrue(
            GATE.GATE_MODES["prox-launcher-lifecycle"]["require_projectile_forward"],
        )
        self.assertTrue(
            GATE.GATE_MODES["prox-launcher-lifecycle"]["current_authority_projectile"],
        )
        self.assertTrue(
            GATE.GATE_MODES["prox-launcher-lifecycle"]["require_prox_lifecycle"],
        )
        self.assertEqual(GATE.GATE_MODES["rocket-splash"]["weapon_policy"], 9)
        self.assertEqual(GATE.GATE_MODES["rocket-splash"]["expected_damage"], 58)
        self.assertTrue(GATE.GATE_MODES["rocket-splash"]["require_current_authority_splash"])
        self.assertTrue(GATE.GATE_MODES["rocket-splash"]["require_reduced_splash"])
        self.assertEqual(GATE.GATE_MODES["plasma-gun"]["weapon_policy"], 10)
        self.assertEqual(GATE.GATE_MODES["plasma-gun"]["expected_damage"], 20)
        self.assertTrue(GATE.GATE_MODES["plasma-gun"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["plasma-gun"]["current_authority_projectile"])
        self.assertEqual(GATE.GATE_MODES["blaster"]["weapon_policy"], 11)
        self.assertEqual(GATE.GATE_MODES["blaster"]["expected_damage"], 15)
        self.assertTrue(GATE.GATE_MODES["blaster"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["blaster"]["current_authority_projectile"])
        combined_mode = GATE.GATE_MODES["blaster-local-action-lease-combined"]
        self.assertTrue(combined_mode["require_local_action_authority_receipt"])
        self.assertTrue(combined_mode["require_in_session_reconnect"])
        self.assertTrue(combined_mode["require_combined_native_shadow"])
        presentation_mode = GATE.GATE_MODES[
            "blaster-native-snapshot-presentation"
        ]
        self.assertTrue(presentation_mode["require_native_snapshot_shadow"])
        self.assertTrue(presentation_mode["require_native_snapshot_presentation"])
        self.assertEqual(GATE.GATE_MODES["hyperblaster"]["weapon_policy"], 11)
        self.assertEqual(GATE.GATE_MODES["hyperblaster"]["expected_damage"], 15)
        self.assertTrue(GATE.GATE_MODES["hyperblaster"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["hyperblaster"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["hyperblaster"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["chainfist"]["weapon_policy"], 12)
        self.assertEqual(GATE.GATE_MODES["chainfist"]["expected_damage"], 15)
        self.assertTrue(GATE.GATE_MODES["chainfist"]["require_hybrid_melee"])
        self.assertEqual(GATE.GATE_MODES["etf-rifle"]["weapon_policy"], 13)
        self.assertEqual(GATE.GATE_MODES["etf-rifle"]["expected_damage"], 10)
        self.assertTrue(GATE.GATE_MODES["etf-rifle"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["etf-rifle"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["etf-rifle"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["phalanx"]["weapon_policy"], 14)
        self.assertEqual(GATE.GATE_MODES["phalanx"]["expected_damage"], 80)
        self.assertTrue(GATE.GATE_MODES["phalanx"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["phalanx"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["phalanx"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["phalanx-splash"]["weapon_policy"], 14)
        self.assertEqual(GATE.GATE_MODES["phalanx-splash"]["expected_damage"], 93)
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["require_current_authority_splash"])
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["require_reduced_splash"])
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["refresh_held_attack"])
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
            SCRIPT.read_text(encoding="utf-8").index("_fixture_ready_status, fixture_ready_responses"),
        )
        self.assertIn("warmup_enabled", server)
        self.assertNotIn("worr_x86_64.exe", " ".join(server))
        self.assertEqual(client[0], str(Path("C:/stage/worr_x86_64.exe")))
        self.assertEqual(client[client.index("fs_homepath") + 1], str(Path("C:/runtime/shooter")))
        for name, value in (("win_headless", "1"), ("cl_headless", "1"),
                            ("in_enable", "0"), ("in_grab", "0")):
            index = client.index(name)
            self.assertEqual(client[index + 1], value)
        self.assertEqual(client[client.index("cl_async") + 1], "1")
        self.assertNotIn("+attack", client)
        self.assertEqual(client[-3:], ["+set", "name", GATE.SHOOTER_NAME])
        self.assertIn("net_impair_enable", client)
        self.assertEqual(client[client.index("net_impair_latency_ms") + 1], "50")

        reconnect_server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960,
            Path("C:/runtime/server"),
            enable_reconnect_minplayer_bypass=True,
        )
        self.assertEqual(
            reconnect_server[reconnect_server.index("cheats") + 1], "1",
        )
        self.assertIn('port, shooter_user_id, "cmd dev_ready", shooter',
                      SCRIPT.read_text(encoding="utf-8"))
        self.assertIn("dev_ready: warmup bypass enabled",
                      SCRIPT.read_text(encoding="utf-8"))

        combined_server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960,
            Path("C:/runtime/server"),
            enable_local_action_authority_receipt=True,
            enable_combined_snapshot_shadow=True,
        )
        combined_client = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.SHOOTER_NAME,
            Path("C:/runtime/shooter"),
            enable_local_action_authority_receipt=True,
            enable_combined_snapshot_shadow=True,
        )
        self.assertEqual(
            combined_server[combined_server.index(
                "sv_worr_native_snapshot_shadow"
            ) + 1],
            "1",
        )
        self.assertEqual(
            combined_client[combined_client.index(
                "cl_worr_native_snapshot_shadow"
            ) + 1],
            "1",
        )
        presentation_client = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.TARGET_NAME,
            Path("C:/runtime/target"),
            enable_local_action_authority_receipt=True,
            enable_combined_snapshot_shadow=True,
            enable_native_snapshot_presentation=True,
        )
        headless_values = [
            presentation_client[index + 1]
            for index, value in enumerate(presentation_client[:-1])
            if value == "cl_headless"
        ]
        self.assertEqual(headless_values, ["1", "0"])
        self.assertEqual(
            presentation_client[presentation_client.index(
                "cg_snapshot_timeline_render"
            ) + 1],
            "3",
        )
        presentation_server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960,
            Path("C:/runtime/server"),
            enable_native_snapshot_shadow=True,
            enable_native_snapshot_presentation=True,
        )
        self.assertEqual(
            presentation_server[presentation_server.index("sv_novis") + 1],
            "1",
        )
        presentation_shooter = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.SHOOTER_NAME,
            Path("C:/runtime/shooter"),
            enable_native_snapshot_shadow=True,
            enable_network_impairment=False,
        )
        self.assertNotIn("net_impair_enable", presentation_shooter)

    def test_fixture_readiness_retries_real_team_choice_after_reconnect(self) -> None:
        pending = PASS_LINE.replace(
            '"pass:1:1:1:', '"pending:1:0:0:', 1,
        )
        ready = PASS_LINE.replace('"pass:', '"pending:', 1)
        status_responses = iter((pending, ready))
        commands: list[str] = []

        def fake_rcon(_port: int, command: str, _timeout: float) -> str:
            commands.append(command)
            if command.startswith("cvarlist "):
                return next(status_responses)
            return "print\n"

        with (
            mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
            mock.patch.object(GATE.time, "sleep"),
        ):
            status, responses = GATE.wait_for_fixture_ready(
                27960, 1.0, GATE.GATE_MODES["railgun"], 4, 7,
            )

        self.assertEqual(status["players_ready"], 1)
        self.assertEqual(status["history_ready"], 1)
        self.assertIn('stuff 4 "cmd team free"', commands)
        self.assertIn('stuff 7 "cmd team free"', commands)
        self.assertGreaterEqual(len(responses), 4)

    def test_status_parser_selects_the_two_admitted_real_clients(self) -> None:
        response = (
            "print\nnum score ping name\n"
            "  4     0    3 rail_shooter  10 127.0.0.1:50001\n"
            "  7     0    4 rail_target   8 127.0.0.1:50002\n"
        )
        self.assertEqual(GATE.admitted_fixture_user_ids(response), (4, 7))

    def test_windows_teardown_kills_the_complete_isolated_process_tree(self) -> None:
        process = mock.Mock()
        process.pid = 4242
        process.poll.return_value = None
        with (
            mock.patch.object(GATE.os, "name", "nt"),
            mock.patch.object(GATE.subprocess, "run") as taskkill,
        ):
            self.assertTrue(GATE.terminate(process))
        taskkill.assert_called_once_with(
            ["taskkill", "/PID", "4242", "/T", "/F"],
            stdin=GATE.subprocess.DEVNULL,
            stdout=GATE.subprocess.DEVNULL,
            stderr=GATE.subprocess.DEVNULL,
            check=False,
            timeout=5,
            creationflags=GATE.creation_flags(),
        )
        process.wait.assert_called_once_with(timeout=5)
        process.terminate.assert_not_called()

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
        self.assertEqual(status["projectile_forward_required"], 0)
        self.assertEqual(status["projectile_forward_authenticated"], 0)
        self.assertEqual(status["projectile_forward_advanced"], 0)
        self.assertEqual(status["projectile_forward_blocked"], 0)
        self.assertEqual(status["melee_selection_required"], 0)
        self.assertEqual(status["melee_selection_authenticated"], 0)
        self.assertEqual(status["melee_historical_eligible"], 0)
        self.assertEqual(status["melee_current_displacement_accepted"], 0)
        self.assertEqual(status["melee_current_displacement_units"], 0)
        self.assertEqual(status["prox_lifecycle_required"], 0)
        self.assertEqual(status["prox_mine_landed"], 0)
        self.assertEqual(status["prox_mine_triggered"], 0)
        self.assertEqual(status["prox_mine_exploded"], 0)
        self.assertEqual(status["historical_mover_occlusion_required"], 0)
        self.assertEqual(status["historical_mover_relocated"], 0)
        self.assertEqual(status["historical_mover_baseline_clear"], 0)
        self.assertEqual(status["historical_mover_occlusion_observed"], 0)
        self.assertEqual(status["historical_mover_target_undamaged"], 0)
        self.assertEqual(status["historical_mover_history_count"], 0)

    def test_railgun_mover_mode_requires_historical_occlusion_and_zero_damage(
        self,
    ) -> None:
        mode = GATE.GATE_MODES["railgun-mover-occlusion"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "damage_applied": 0,
            "observed_damage": 0,
            "historical_mover_occlusion_required": 1,
            "historical_mover_relocated": 1,
            "historical_mover_baseline_clear": 1,
            "historical_mover_occlusion_observed": 1,
            "historical_mover_target_undamaged": 1,
            "historical_mover_history_count": 12,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        damaged = dict(status)
        damaged["observed_damage"] = 80
        with self.assertRaisesRegex(RuntimeError, "damaged through"):
            GATE.validate_status(damaged, mode)

    def test_chainfist_mode_requires_bounded_hybrid_melee_proof(self) -> None:
        mode = GATE.GATE_MODES["chainfist"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "observation_weapon_policy": 12,
            "expected_damage": 15,
            "observed_damage": 15,
            "melee_selection_required": 1,
            "melee_selection_authenticated": 1,
            "melee_historical_eligible": 1,
            "melee_current_displacement_accepted": 1,
            "melee_current_displacement_units": 64,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        bad_displacement = dict(status)
        bad_displacement["melee_current_displacement_units"] = 65
        with self.assertRaisesRegex(RuntimeError, "bounded hybrid melee"):
            GATE.validate_status(bad_displacement, mode)

    def test_etf_rifle_mode_requires_current_world_spawn_forward(self) -> None:
        mode = GATE.GATE_MODES["etf-rifle"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 13,
            "expected_damage": 10,
            "observed_damage": 10,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_impact = dict(status)
        wrong_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(wrong_impact, mode)

    def test_grenade_launcher_mode_requires_clear_current_world_ballistic_forward(self) -> None:
        mode = GATE.GATE_MODES["grenade-launcher"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 15,
            "expected_damage": 60,
            "observed_damage": 57,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_hand_grenade_mode_requires_release_time_current_world_forward(self) -> None:
        mode = GATE.GATE_MODES["hand-grenade"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 16,
            "expected_damage": 60,
            "observed_damage": 57,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_impact = dict(status)
        wrong_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(wrong_impact, mode)

    def test_prox_launcher_mode_requires_clear_current_world_ballistic_forward(self) -> None:
        mode = GATE.GATE_MODES["prox-launcher"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 17,
            "expected_damage": 90,
            "observed_damage": 0,
            "damage_applied": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_prox_launcher_lifecycle_mode_requires_normal_landing_trigger_and_explosion(self) -> None:
        mode = GATE.GATE_MODES["prox-launcher-lifecycle"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 17,
            "expected_damage": 61,
            "observed_damage": 61,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "prox_lifecycle_required": 1,
            "prox_mine_landed": 1,
            "prox_mine_triggered": 1,
            "prox_mine_exploded": 1,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_trigger = dict(status)
        missing_trigger["prox_mine_triggered"] = 0
        with self.assertRaisesRegex(RuntimeError, "normal lifecycle"):
            GATE.validate_status(missing_trigger, mode)

    def test_phalanx_mode_requires_current_world_spawn_forward(self) -> None:
        mode = GATE.GATE_MODES["phalanx"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 14,
            "expected_damage": 80,
            "observed_damage": 80,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_impact = dict(status)
        wrong_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(wrong_impact, mode)

    def test_hyperblaster_mode_requires_current_world_spawn_forward(self) -> None:
        mode = GATE.GATE_MODES["hyperblaster"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 11,
            "expected_damage": 15,
            "observed_damage": 15,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_impact = dict(status)
        wrong_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(wrong_impact, mode)

    def test_phalanx_splash_mode_requires_current_world_splash_proof(self) -> None:
        mode = GATE.GATE_MODES["phalanx-splash"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 14,
            "expected_damage": 93,
            "observed_damage": 93,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_splash = dict(status)
        wrong_splash["observed_damage"] = 100
        with self.assertRaisesRegex(RuntimeError, "reduced splash"):
            GATE.validate_status(wrong_splash, mode)

    def test_machinegun_mode_requires_its_own_policy_and_damage(self) -> None:
        mode = GATE.GATE_MODES["machinegun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":1:8:8:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
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
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":3:48:48:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
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
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":2:18:18:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
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
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":4:120:120:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
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
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":6:45:45:0:0:0:0:0:0:0:1:1:1:0:0:50000:50000")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        incomplete_daemon = dict(status)
        incomplete_daemon["observed_damage"] = 36
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(incomplete_daemon, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_rocket_mode_requires_current_authority_damage_and_forward_proof(self) -> None:
        mode = GATE.GATE_MODES["rocket"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 9,
            "expected_damage": 100,
            "observed_damage": 100,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_forward = dict(status)
        missing_forward["projectile_forward_advanced"] = 0
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(missing_forward, mode)
        wrong_damage = dict(status)
        wrong_damage["observed_damage"] = 90
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(wrong_damage, mode)

    def test_bfg_mode_requires_only_its_current_world_spawn_forward_proof(self) -> None:
        mode = GATE.GATE_MODES["bfg"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 18,
            "expected_damage": 200,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 950_000,
            "projectile_forward_advanced_age_us": 100_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        missing_forward = dict(status)
        missing_forward["projectile_forward_advanced"] = 0
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(missing_forward, mode)

    def test_ion_ripper_mode_requires_all_fifteen_current_world_bolt_launches(self) -> None:
        mode = GATE.GATE_MODES["ion-ripper"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 19,
            "expected_damage": 10,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "projectile_forward_launches": 15,
            "projectile_forward_expected_launches": 15,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_bolt = dict(status)
        missing_bolt["projectile_forward_launches"] = 14
        with self.assertRaisesRegex(RuntimeError, "complete every normal launch"):
            GATE.validate_status(missing_bolt, mode)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)

    def test_tesla_mine_mode_requires_release_bound_current_world_forward_only(self) -> None:
        mode = GATE.GATE_MODES["tesla-mine"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 20,
            "expected_damage": 3,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 56_000,
            "projectile_forward_advanced_age_us": 56_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_trap_mode_requires_release_bound_current_world_forward_only(self) -> None:
        mode = GATE.GATE_MODES["trap"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 21,
            "expected_damage": 20,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 56_000,
            "projectile_forward_advanced_age_us": 56_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_grapple_mode_requires_clear_current_world_hook_forward_only(self) -> None:
        mode = GATE.GATE_MODES["grapple"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 22,
            "expected_damage": 1,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 400_000,
            "projectile_forward_advanced_age_us": 100_000,
            "projectile_forward_clamped": 1,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_offhand_hook_mode_requires_its_native_current_world_forward_only(self) -> None:
        mode = GATE.GATE_MODES["offhand-hook"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 24,
            "expected_damage": 1,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 56_000,
            "projectile_forward_advanced_age_us": 56_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_proball_throw_mode_requires_clear_release_bound_forward_only(self) -> None:
        mode = GATE.GATE_MODES["proball-throw"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 23,
            "expected_damage": 1,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 56_000,
            "projectile_forward_advanced_age_us": 56_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_rocket_splash_mode_requires_reduced_current_authority_damage(self) -> None:
        mode = GATE.GATE_MODES["rocket-splash"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 9,
            "expected_damage": 58,
            "observed_damage": 58,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        direct_damage = dict(status)
        direct_damage["observed_damage"] = 100
        direct_damage["expected_damage"] = 100
        with self.assertRaisesRegex(RuntimeError, "reduced splash damage"):
            GATE.validate_status(direct_damage, mode)

    def test_plasma_gun_mode_requires_current_authority_direct_damage_and_forward_proof(self) -> None:
        mode = GATE.GATE_MODES["plasma-gun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 10,
            "expected_damage": 20,
            "observed_damage": 20,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        wrong_damage = dict(status)
        wrong_damage["observed_damage"] = 15
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(wrong_damage, mode)

    def test_plasma_gun_splash_mode_requires_reduced_current_authority_damage(self) -> None:
        mode = GATE.GATE_MODES["plasma-gun-splash"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 10,
            "expected_damage": 7,
            "observed_damage": 7,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        direct_damage = dict(status)
        direct_damage["observed_damage"] = 100
        direct_damage["expected_damage"] = 100
        with self.assertRaisesRegex(RuntimeError, "reduced splash damage"):
            GATE.validate_status(direct_damage, mode)

    def test_blaster_mode_requires_current_authority_direct_damage_and_forward_proof(self) -> None:
        mode = GATE.GATE_MODES["blaster"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 11,
            "expected_damage": 15,
            "observed_damage": 15,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_forward = dict(status)
        missing_forward["projectile_forward_authenticated"] = 0
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(missing_forward, mode)
        wrong_damage = dict(status)
        wrong_damage["observed_damage"] = 20
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(wrong_damage, mode)

    def test_blaster_local_action_lease_mode_requires_exact_join(self) -> None:
        mode = GATE.GATE_MODES["blaster-local-action-lease"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 11,
            "expected_damage": 15,
            "observed_damage": 15,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)

        missing_join = dict(status)
        missing_join["local_action_joined_record"] = 0
        with self.assertRaisesRegex(RuntimeError, "joined_record"):
            GATE.validate_status(missing_join, mode)

        rejected = dict(status)
        rejected["local_action_lease_rejected"] = 1
        with self.assertRaisesRegex(RuntimeError, "observed a rejection"):
            GATE.validate_status(rejected, mode)

        wrong_catalog = dict(status)
        wrong_catalog["local_action_shadow_catalog_id"] = 2
        with self.assertRaisesRegex(RuntimeError, "catalog identity"):
            GATE.validate_status(wrong_catalog, mode)

        missing_base_flag = dict(status)
        missing_base_flag["local_action_shadow_flags"] = 6
        with self.assertRaisesRegex(RuntimeError, "base flags"):
            GATE.validate_status(missing_base_flag, mode)

        wrong_blockers = dict(status)
        wrong_blockers["local_action_shadow_v2_blockers"] = 0
        with self.assertRaisesRegex(RuntimeError, "blocker mask"):
            GATE.validate_status(wrong_blockers, mode)

        missing_hash = dict(status)
        missing_hash["local_action_shadow_record_hash"] = 0
        with self.assertRaisesRegex(RuntimeError, "record hash"):
            GATE.validate_status(missing_hash, mode)

    def test_combined_native_status_requires_both_acknowledged_snapshot_peers(self) -> None:
        client_text = (
            "WORR_NATIVE_CLIENT_STATUS_V1 schema=1 enabled=1 mode=2 "
            "capability_confirmed=1 protocol=1038 private_mask=0x77 "
            "server_active=1 failures=0 last_failure=0\n"
        )
        client_rows = GATE.parse_native_status_rows(
            client_text, GATE.NATIVE_CLIENT_STATUS_MARKER,
        )
        self.assertEqual(len(client_rows), 1)
        self.assertEqual(client_rows[0]["private_mask"], 0x77)
        self.assertEqual(
            GATE.validate_combined_native_client_status(client_rows[0]),
            client_rows[0],
        )

        base_text = "\n".join(
            "WORR_NATIVE_SERVER_STATUS_V1 schema=1 slot={} protocol=1038 "
            "enabled=1 private_mask=0x77 wire_committed=1 failures=0 "
            "rx_rejections=0 tx_ack_rejections=0 last_failure=0".format(slot)
            for slot in (0, 1)
        )
        snapshot_text = "\n".join(
            "WORR_NATIVE_SERVER_SNAPSHOT_STATUS_V1 schema=1 slot={} "
            "snapshot_epoch={} sender=1 tx_open=1 queued=8 "
            "queue_failures=0 acks=7 released=7 confirmed=8 rejected=0 "
            "first_sends=8".format(slot, slot + 5)
            for slot in (0, 1)
        )
        base_rows = GATE.parse_native_status_rows(
            base_text, GATE.NATIVE_SERVER_STATUS_MARKER,
        )
        snapshot_rows = GATE.parse_native_status_rows(
            snapshot_text, GATE.NATIVE_SERVER_SNAPSHOT_STATUS_MARKER,
        )
        evidence = GATE.validate_combined_native_server_status(
            base_rows, snapshot_rows,
        )
        self.assertEqual(len(evidence["server_peers"]), 2)
        self.assertEqual(len(evidence["snapshot_peers"]), 2)
        target_evidence = GATE.validate_combined_native_server_status(
            base_rows[1:], snapshot_rows[1:], 0x77, (1,),
        )
        self.assertEqual(len(target_evidence["server_peers"]), 1)
        self.assertEqual(target_evidence["server_peers"][0]["slot"], 1)

        snapshot_rows[1]["acks"] = 0
        with self.assertRaisesRegex(RuntimeError, "acks traffic"):
            GATE.validate_combined_native_server_status(
                base_rows, snapshot_rows,
            )

    def test_native_snapshot_presentation_requires_exact_promoted_parity(self) -> None:
        text = (
            "cg_snapshot_timeline_render: epoch=9 mode=3 clock=120/0 "
            "pair=119/1 align_fail=0 pair_mode=1 pair_blocks=0x0 "
            "samples=64 fail=2 invisible=1 discontinuity=1 "
            "parity=0/0 native=60/4 promoted=60 events=0/0/0 "
            "max_error=0.0000/0.0000/0.0000\n"
        )
        rows = GATE.parse_native_snapshot_presentation(text)
        self.assertEqual(len(rows), 1)
        self.assertEqual(
            GATE.validate_native_snapshot_presentation(rows[0]), rows[0],
        )

        mismatch = dict(rows[0])
        mismatch["parity_mismatches"] = 1
        with self.assertRaisesRegex(RuntimeError, "parity_mismatches"):
            GATE.validate_native_snapshot_presentation(mismatch)

        not_promoted = dict(rows[0])
        not_promoted["promoted_transforms"] = 59
        with self.assertRaisesRegex(RuntimeError, "every native sample"):
            GATE.validate_native_snapshot_presentation(not_promoted)

    def test_plasma_beam_mode_requires_its_first_held_command_tick(self) -> None:
        mode = GATE.GATE_MODES["plasma-beam"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":7:8:8:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
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
        ).replace(
            ":5:80:80:" + ":".join(["0"] * 14),
            ":8:8:8:" + ":".join(["0"] * 14),
        )
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
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":7:24:24:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
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
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":8:24:24:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
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
            ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", f":{policy}:256:256:0:0:0:0:0:1:0:0:0:0:0:0:0:0")
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
            ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", f":{policy}:4:4:1:1:0:0:0:0:0:0:0:0:0:0:0:0")
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
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":8:70:70:0:0:1:1:1:0:0:0:0:0:0:0:0:0")
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
        later["projectile_forward_age_us"] = 64_000
        later["projectile_forward_advanced_age_us"] = 64_000
        self.assertEqual(
            GATE.determinism_signature(baseline),
            GATE.determinism_signature(later),
        )

    def test_runtime_launches_use_no_window_and_devnull(self) -> None:
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertIn("_headless_creation_flags", source)
        self.assertIn("terminate_process_tree", source)
        self.assertGreaterEqual(source.count("stdin=subprocess.DEVNULL"), 2)


if __name__ == "__main__":
    unittest.main()
