#!/usr/bin/env python3
"""Unit contracts for the headless railgun-damage runtime gate parser."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/networking/run_rewind_rail_damage_runtime_gate.py"
SPEC = importlib.util.spec_from_file_location("rewind_rail_damage_runtime_gate", SCRIPT)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


PASS_LINE = (
    'sg_worr_rewind_rail_damage_selftest_status '
    '"pass:1:1:1:1:1:1:1:1:1:1:1:1:1:2:30:1000000:17574:17574:17574:0"'
)


class RewindRailDamageRuntimeGateTests(unittest.TestCase):
    def test_command_is_dedicated_only_and_enables_the_explicit_opt_in(self) -> None:
        command = GATE.build_command(Path("C:/staged/worr_ded_x86_64.exe"))
        self.assertEqual(command[0], str(Path("C:/staged/worr_ded_x86_64.exe")))
        self.assertIn("+map", command)
        self.assertEqual(command[command.index("+map") + 1], GATE.MAP_NAME)
        self.assertEqual(command[command.index("+set") + 1], "game")
        self.assertIn("g_lag_compensation", command)
        self.assertIn("sg_lag_compensation_debug", command)
        self.assertEqual(command[command.index("+addbot") + 1], "RewindRailShooter")
        second_bot = command.index("+addbot", command.index("+addbot") + 1)
        self.assertEqual(command[second_bot + 1], "RewindRailTarget")
        self.assertEqual(
            command[command.index("worr_rewind_rail_damage_arm") - 1], "+sv"
        )
        self.assertEqual(
            command[command.index("worr_rewind_rail_damage_selftest") - 1], "+sv"
        )
        self.assertNotIn("+quit", command)
        self.assertNotIn("worr_x86_64.exe", " ".join(command))

    def test_parse_and_validate_accept_the_complete_runtime_proof(self) -> None:
        status = GATE.parse_status("boot\n" + PASS_LINE + "\n")
        self.assertEqual(GATE.validate_status(status), status)
        self.assertEqual(status["candidate_count"], 2)
        self.assertEqual(status["damage_amount"], 30)
        self.assertEqual(status["current_fraction_q6"], 1_000_000)
        self.assertEqual(status["near_latency_fraction_q6"], 17_574)
        self.assertEqual(status["bounded_latency_fraction_q6"], 17_574)
        self.assertEqual(status["capped_latency_fraction_q6"], 17_574)

    def test_rejects_missing_or_partial_status_evidence(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            GATE.parse_status("")
        partial = PASS_LINE.replace(
            "pass:1:1:1:1:1:1:1:1:1:1:1:1:1:2",
            "pass:1:1:1:1:0:1:1:1:1:1:1:1:1:2",
        )
        with self.assertRaisesRegex(RuntimeError, "rejected_no_damage"):
            GATE.validate_status(GATE.parse_status(partial))

    def test_rejects_stale_or_duplicate_status_rows(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            GATE.parse_status(PASS_LINE + "\n" + PASS_LINE)
        stale = PASS_LINE.replace(":0\"", ":9\"")
        with self.assertRaisesRegex(RuntimeError, "failure code"):
            GATE.validate_status(GATE.parse_status(stale))


if __name__ == "__main__":
    unittest.main()
