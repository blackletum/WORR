#!/usr/bin/env python3
"""Unit contracts for the headless moving-brush runtime gate parser."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/networking/run_rewind_mover_runtime_gate.py"
SPEC = importlib.util.spec_from_file_location("rewind_mover_runtime_gate", SCRIPT)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


PASS_LINE = (
    'sg_worr_rewind_mover_selftest_status '
    '"pass:1:1:1:1:1:1:1:1:1:1:1:1:1:3:12:1000000:374756:0"'
)


class RewindMoverRuntimeGateTests(unittest.TestCase):
    def test_command_is_dedicated_only_and_loads_the_collision_fixture(self) -> None:
        command = GATE.build_command(Path("C:/staged/worr_ded_x86_64.exe"))
        self.assertEqual(command[0], str(Path("C:/staged/worr_ded_x86_64.exe")))
        self.assertIn("+map", command)
        self.assertEqual(command[command.index("+map") + 1], GATE.MAP_NAME)
        self.assertEqual(command[command.index("+addbot") + 1], "RewindRider")
        self.assertEqual(
            command[command.index("worr_rewind_mover_arm_rider") - 1], "+sv"
        )
        self.assertEqual(command[command.index("+wait") + 1], "12")
        self.assertNotIn("+quit", command)
        self.assertNotIn("worr_x86_64.exe", " ".join(command))

    def test_parse_and_validate_accept_the_complete_runtime_proof(self) -> None:
        status = GATE.parse_status("boot\n" + PASS_LINE + "\n")
        self.assertEqual(GATE.validate_status(status), status)
        self.assertEqual(status["candidate_count"], 3)
        self.assertEqual(status["rider_continuity_samples"], 12)
        self.assertEqual(status["rider_frame_scene_sealed"], 1)
        self.assertEqual(status["historical_fraction_q6"], 374756)

    def test_rejects_missing_or_partial_status_evidence(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            GATE.parse_status("")
        partial = PASS_LINE.replace(
            "pass:1:1:1:1:1:1:1:1:1:1:1:1:1",
            "pass:1:1:1:1:0:1:1:1:1:1:1:1:1",
        )
        with self.assertRaisesRegex(RuntimeError, "rider_frame_continuity"):
            GATE.validate_status(GATE.parse_status(partial))

    def test_rejects_stale_or_duplicate_status_rows(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            GATE.parse_status(PASS_LINE + "\n" + PASS_LINE)
        stale = PASS_LINE.replace(":0\"", ":9\"")
        with self.assertRaisesRegex(RuntimeError, "failure code"):
            GATE.validate_status(GATE.parse_status(stale))


if __name__ == "__main__":
    unittest.main()
