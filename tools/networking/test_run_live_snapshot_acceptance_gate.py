#!/usr/bin/env python3
"""Regression tests for the target-count live snapshot gate."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("run_live_snapshot_acceptance_gate.py")
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("live_snapshot_gate", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


def valid_telemetry(count: int) -> dict[str, int]:
    return {
        "attempts": count,
        "projected": count,
        "published": count,
        "lineage_only": 0,
        "promotion_eligible": count,
        "comparisons": count,
        "mismatches": 0,
        "entity_mismatches": 0,
        "frame_failures": 0,
        "capture_overflows": 0,
        "promotion_blocks": 0,
        "consumer_attempts": count,
        "consumer_accepts": count,
        "consumer_rejections": 0,
    }


def valid_impaired_counters() -> dict[str, int]:
    return {
        "seen": 100,
        "dropped": 2,
        "reordered": 3,
        "duplicated": 4,
        "upstream_stalled": 50,
        "throttled": 0,
        "overflow": 0,
    }


class LiveSnapshotAcceptanceGateTests(unittest.TestCase):
    def test_wait_commands_respect_engine_limit(self) -> None:
        commands = GATE.wait_commands(2501)
        self.assertEqual(
            commands,
            ["+wait", "1000", "+wait", "1000", "+wait", "501"],
        )

    def test_command_schedules_target_and_safety_frames(self) -> None:
        command, frames = GATE.client_command(
            Path("client.exe"),
            impaired=True,
            target=100_000,
            schedule_percent=GATE.DEFAULT_SCHEDULE_PERCENT,
            startup_frames=GATE.DEFAULT_STARTUP_FRAMES,
            completion_marker="terminal-marker",
        )
        self.assertEqual(frames, 152_048)
        settings = {
            command[index + 1]: command[index + 2]
            for index, value in enumerate(command[:-2])
            if value == "+set"
        }
        self.assertEqual(
            {
                name: settings[name]
                for name in (
                    "net_impair_seed",
                    "net_impair_latency_ms",
                    "net_impair_jitter_ms",
                    "net_impair_loss_pct",
                    "net_impair_burst_loss_pct",
                    "net_impair_burst_length",
                    "net_impair_reorder_pct",
                    "net_impair_duplicate_pct",
                    "net_impair_upstream_stall_ms",
                    "net_impair_rate_kbps",
                )
            },
            {
                "net_impair_seed": "424242",
                "net_impair_latency_ms": "25",
                "net_impair_jitter_ms": "5",
                "net_impair_loss_pct": "1",
                "net_impair_burst_loss_pct": "0.1",
                "net_impair_burst_length": "3",
                "net_impair_reorder_pct": "0.5",
                "net_impair_duplicate_pct": "0.5",
                "net_impair_upstream_stall_ms": "20",
                "net_impair_rate_kbps": str(GATE.LIVE_IMPAIR_RATE_KBPS),
            },
        )
        self.assertEqual(GATE.LIVE_IMPAIR_RATE_KBPS, 0)
        self.assertEqual(command[-4:], ["+echo", "terminal-marker", "+wait", "1000"])
        wait_values = [
            int(command[index + 1])
            for index, value in enumerate(command[:-1])
            if value == "+wait"
        ]
        self.assertEqual(sum(wait_values[:-1]), frames)
        self.assertTrue(all(value <= GATE.WAIT_LIMIT for value in wait_values))

    def test_previous_pass_and_failure_pointer_are_invalidated(self) -> None:
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "evidence.json"
            failure = output.with_suffix(".failure.json")
            output.write_text("stale pass", encoding="utf-8")
            failure.write_text("stale failure", encoding="utf-8")
            self.assertEqual(GATE.invalidate_previous_outputs(output), failure)
            self.assertFalse(output.exists())
            self.assertFalse(failure.exists())

    def test_build_manifest_binds_every_loaded_runtime_component(self) -> None:
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            client = root / "worr_x86_64.exe"
            client.write_bytes(b"client")
            expected_roles = {"client"}
            for role, relative in GATE.WINDOWS_RUNTIME_COMPONENTS:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(role.encode("ascii"))
                expected_roles.add(role)

            first = GATE.runtime_component_manifest(client, root)
            self.assertEqual(
                {entry["role"] for entry in first}, expected_roles
            )
            self.assertEqual(
                GATE.manifest_sha256(first, "client"),
                GATE.file_sha256(client),
            )
            engine = root / "worr_engine_x86_64.dll"
            engine.write_bytes(b"changed-engine")
            second = GATE.runtime_component_manifest(client, root)
            self.assertNotEqual(first, second)

    def test_missing_runtime_component_fails_before_launch(self) -> None:
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            client = root / "worr_x86_64.exe"
            client.write_bytes(b"client")
            with self.assertRaisesRegex(FileNotFoundError, "component"):
                GATE.runtime_component_manifest(client, root)

    def test_completion_marker_rejects_command_injection(self) -> None:
        with self.assertRaisesRegex(ValueError, "safe token"):
            GATE.client_command(
                Path("client.exe"),
                impaired=False,
                target=1,
                completion_marker="bad;quit",
            )

    def test_target_accepts_complete_pipeline(self) -> None:
        GATE.validate_target(valid_telemetry(152_048), target=100_000)

    def test_target_rejects_short_consumer(self) -> None:
        telemetry = valid_telemetry(152_048)
        telemetry["consumer_accepts"] = 99_999
        with self.assertRaisesRegex(RuntimeError, "consumer_accepts"):
            GATE.validate_target(telemetry, target=100_000)

    def test_target_rejects_pipeline_loss_above_target(self) -> None:
        telemetry = valid_telemetry(152_048)
        telemetry["published"] = 152_047
        with self.assertRaisesRegex(RuntimeError, "retain every attempted frame"):
            GATE.validate_target(telemetry, target=100_000)

    def test_impaired_counter_contract_accepts_complete_profile(self) -> None:
        GATE.validate_impairment_counters(
            valid_impaired_counters(), impaired=True
        )

    def test_impaired_counter_contract_requires_every_impairment(self) -> None:
        for name in GATE.IMPAIRED_REQUIRED_COUNTERS:
            with self.subTest(name=name):
                counters = valid_impaired_counters()
                counters[name] = 0
                with self.assertRaisesRegex(RuntimeError, f"no {name} events"):
                    GATE.validate_impairment_counters(counters, impaired=True)

    def test_impaired_counter_contract_rejects_queue_overflow(self) -> None:
        counters = valid_impaired_counters()
        counters["overflow"] = 1
        with self.assertRaisesRegex(RuntimeError, "overflowed"):
            GATE.validate_impairment_counters(counters, impaired=True)

    def test_accelerated_gate_rejects_wall_clock_throttling(self) -> None:
        counters = valid_impaired_counters()
        counters["throttled"] = 1
        with self.assertRaisesRegex(RuntimeError, "rate shaping"):
            GATE.validate_impairment_counters(counters, impaired=True)

    def test_clean_counter_contract_includes_throttling(self) -> None:
        counters = {
            name: 0 for name in GATE.RAW_ROUTE_COUNTERS
        }
        GATE.validate_impairment_counters(counters, impaired=False)
        counters["throttled"] = 1
        with self.assertRaisesRegex(RuntimeError, "raw routing"):
            GATE.validate_impairment_counters(counters, impaired=False)


if __name__ == "__main__":
    unittest.main()
