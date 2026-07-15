#!/usr/bin/env python3
"""Regression tests for the headless command-gap acceptance runner."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("run_command_gap_acceptance_gate.py")
SPEC = importlib.util.spec_from_file_location("command_gap_gate", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
GATE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GATE
SPEC.loader.exec_module(GATE)


def status_line(gap: int, **overrides: int | str) -> str:
    expected_sequence = GATE.BASELINE_SEQUENCE + gap
    values: dict[str, int | str] = {
        "case": gap,
        "status": "pass",
        "gap": gap,
        "synthesized": 0,
        "skipped": gap,
        "received_epoch": 1,
        "received_sequence": expected_sequence,
        "consumed_epoch": 1,
        "consumed_sequence": expected_sequence,
        "attempts": 1,
        "fast_forwards": 1,
        "fast_forwarded": gap,
        "rejections": 0,
        "policy_rejections": 0,
        "stream_valid": 1,
        "cursor_valid": 1,
    }
    values.update(overrides)
    return (
        "worr_command_gap_selftest: "
        f"case=gap-{values['case']} status={values['status']} gap={values['gap']} "
        f"synthesized={values['synthesized']} skipped={values['skipped']} "
        f"received={values['received_epoch']}:{values['received_sequence']} "
        f"consumed={values['consumed_epoch']}:{values['consumed_sequence']} "
        f"attempts={values['attempts']} fast_forwards={values['fast_forwards']} "
        f"fast_forwarded={values['fast_forwarded']} "
        f"rejections={values['rejections']} "
        f"policy_rejections={values['policy_rejections']} "
        f"stream_valid={values['stream_valid']} cursor_valid={values['cursor_valid']}"
    )


class CommandGapAcceptanceGateTests(unittest.TestCase):
    def valid_statuses(self) -> list[dict[str, int | str]]:
        text = "\n".join(status_line(gap) for gap in GATE.EXPECTED_GAPS)
        return GATE.parse_statuses(text)

    def test_dedicated_only_command_has_no_client_or_renderer_arguments(self) -> None:
        command = GATE.build_command(Path("worr_ded_x86_64.exe"))
        self.assertEqual(command[0], "worr_ded_x86_64.exe")
        self.assertIn("+sv_worr_command_gap_selftest", command)
        self.assertIn("+quit", command)
        self.assertNotIn("r_renderer", command)
        self.assertNotIn("cgame", " ".join(command))

    def test_required_large_gap_cases_pass(self) -> None:
        validated = GATE.validate_statuses(self.valid_statuses())
        self.assertEqual([row["case"] for row in validated], [161, 401])

    def test_missing_or_duplicate_case_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "boundaries"):
            GATE.validate_statuses(GATE.parse_statuses(status_line(161)))
        duplicate = status_line(161) + "\n" + status_line(161) + "\n" + status_line(401)
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            GATE.validate_statuses(GATE.parse_statuses(duplicate))

    def test_nonzero_policy_or_fast_forward_rejection_is_rejected(self) -> None:
        for field in ("rejections", "policy_rejections"):
            with self.subTest(field=field):
                text = "\n".join(
                    status_line(gap, **({field: 1} if gap == 401 else {}))
                    for gap in GATE.EXPECTED_GAPS
                )
                with self.assertRaisesRegex(RuntimeError, field):
                    GATE.validate_statuses(GATE.parse_statuses(text))

    def test_synthetic_or_incomplete_recovery_is_rejected(self) -> None:
        for field, value in (("synthesized", 1), ("skipped", 400), ("cursor_valid", 0)):
            with self.subTest(field=field):
                text = "\n".join(
                    status_line(gap, **({field: value} if gap == 401 else {}))
                    for gap in GATE.EXPECTED_GAPS
                )
                with self.assertRaisesRegex(RuntimeError, field):
                    GATE.validate_statuses(GATE.parse_statuses(text))

    def test_stale_success_and_failure_outputs_are_invalidated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "command-gap.json"
            failure = output.with_suffix(".failure.json")
            output.write_text("stale success", encoding="utf-8")
            failure.write_text("stale failure", encoding="utf-8")
            self.assertEqual(GATE.invalidate_previous_outputs(output), failure)
            self.assertFalse(output.exists())
            self.assertFalse(failure.exists())


if __name__ == "__main__":
    unittest.main()
