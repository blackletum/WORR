#!/usr/bin/env python3
"""Regression tests for staged runtime status parsing and acceptance gates."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("run_staged_impairment_smoke.py")
SPEC = importlib.util.spec_from_file_location("staged_impairment_smoke", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
SMOKE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SMOKE)


def status_text(
    *,
    enabled: bool,
    parity: int = 0,
    recovery_disabled: int = 0,
    recovery_generation: int = 0,
    consumer_accepts: int = 400,
    consumer_rejections: int = 0,
    consumer: int = 1,
    accept_flags: int = 0x3,
) -> str:
    adaptive_enabled = 1 if enabled else 0
    adaptive_active = adaptive_enabled
    decision_valid = adaptive_enabled
    adaptive_telemetry = ""
    if enabled:
        adaptive_telemetry = (
            "  evaluations=400 windows=20 held=380 recovery_holds=1 "
            "counter_resets=0 clock_resets=0 received=390 dropped=10\n"
        )
    return (
        f"adaptive input: enabled={adaptive_enabled} active={adaptive_active} "
        f"decision_valid={decision_valid} fallbacks=0\n"
        f"{adaptive_telemetry}"
        f"snapshot shadow: active=1 lifecycle=2 epoch=7 pending=0 "
        f"last_result=0 capture_failure=0 parity_last=0x{parity:x} "
        f"accept_flags=0x{accept_flags:x} consumer={consumer} "
        "consumer_last_rejection="
        f"{0 if consumer_rejections == 0 else 3}\n"
        "snapshot shadow telemetry: attempts=400 projected=400 published=400 "
        "lineage_only=0 promotion_eligible=400 comparisons=400 "
        f"mismatches={1 if parity else 0} "
        f"entity_mismatches={1 if parity & 0x0c else 0} "
        "frame_failures=0 capture_overflows=0 promotion_blocks=0 "
        f"consumer_attempts=400 consumer_accepts={consumer_accepts} "
        f"consumer_rejections={consumer_rejections}\n"
        f"snapshot recovery: enabled={adaptive_enabled} active=0 exhausted=0 "
        f"generation={recovery_generation} reasons=0x0 "
        "legacy_streak=0 canonical_streak=0 "
        "attempts=0 cooldown=0 arms=0 decisions=0 recoveries=0 overrides=0 "
        f"inherited=0 disabled={recovery_disabled} ignored_nontransport=0 "
        "last_result=0\n"
    )


class RuntimeStatusGateTests(unittest.TestCase):
    def test_clean_control_passes(self) -> None:
        adaptive, recovery, telemetry, shadow, shadow_telemetry = (
            SMOKE.validate_adapter_status(status_text(enabled=False), False)
        )
        self.assertEqual(adaptive["active"], 0)
        self.assertEqual(recovery["reasons"], 0)
        self.assertIsNone(telemetry)
        self.assertEqual(shadow["parity_last"], 0)
        self.assertEqual(shadow_telemetry["comparisons"], 400)

    def test_impaired_live_adaptive_path_passes(self) -> None:
        adaptive, _, telemetry, _, _ = SMOKE.validate_adapter_status(
            status_text(enabled=True), True
        )
        self.assertEqual(adaptive["decision_valid"], 1)
        self.assertIsNotNone(telemetry)
        assert telemetry is not None
        self.assertEqual(telemetry["windows"], 20)

    def test_snapshot_parity_mismatch_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "legacy parity"):
            SMOKE.validate_adapter_status(
                status_text(enabled=False, parity=0x108), False
            )

    def test_clean_control_recovery_activity_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exercised snapshot recovery"):
            SMOKE.validate_adapter_status(
                status_text(enabled=False, recovery_disabled=1), False
            )

    def test_clean_control_prior_recovery_generation_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exercised snapshot recovery"):
            SMOKE.validate_adapter_status(
                status_text(enabled=False, recovery_generation=1), False
            )

    def test_snapshot_consumer_rejection_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "consumer did not accept"):
            SMOKE.validate_adapter_status(
                status_text(
                    enabled=False,
                    consumer_accepts=399,
                    consumer_rejections=1,
                ),
                False,
            )

    def test_terminal_consumer_detachment_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "shadow/consumer"):
            SMOKE.validate_adapter_status(
                status_text(enabled=False, consumer=0), False
            )

    def test_terminal_acceptance_flags_are_required(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "shadow/consumer"):
            SMOKE.validate_adapter_status(
                status_text(enabled=False, accept_flags=0x1), False
            )

    def run_fake_client(
        self,
        body: str,
        *,
        marker: str = "gate-complete",
        failure_markers: tuple[str, ...] = (),
    ) -> tuple[str, str, dict[str, int], bool]:
        counter = (
            "net_impair counters: seen=1 dropped=1 burst_dropped=0 "
            "reordered=1 duplicated=1 corrupted=0 upstream_stalled=1 "
            "throttled=0 overflow=0 resets=0"
        )
        script = (
            "import time\n"
            "print('Going from cs_primed to cs_spawned', flush=True)\n"
            f"print({counter!r}, flush=True)\n"
            f"{body}\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            return SMOKE.run_client(
                [sys.executable, "-u", "-c", script],
                root,
                root / "stdout.log",
                root / "stderr.log",
                2.0,
                subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
                completion_marker=marker,
                failure_markers=failure_markers,
            )

    def test_completion_marker_controls_terminal_detection(self) -> None:
        text, _, counters, terminated = self.run_fake_client(
            "print('gate-complete', flush=True); time.sleep(1)"
        )
        self.assertIn("gate-complete", text)
        self.assertEqual(counters["seen"], 1)
        self.assertTrue(terminated)

    def test_failure_marker_stops_before_completion(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "failure marker"):
            self.run_fake_client(
                "print('invalid canonical command stream', flush=True); "
                "time.sleep(1)",
                failure_markers=("invalid canonical command stream",),
            )

    def test_failure_marker_wins_when_completion_shares_chunk(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "failure marker"):
            self.run_fake_client(
                "print('invalid canonical command stream'); "
                "print('gate-complete', flush=True); time.sleep(1)",
                failure_markers=("invalid canonical command stream",),
            )

    def test_natural_exit_before_marker_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exited before live evidence"):
            self.run_fake_client("print('not-the-marker', flush=True)")


if __name__ == "__main__":
    unittest.main()
