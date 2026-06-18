#!/usr/bin/env python3
"""Regression tests for the WORR bot perf analyzer."""

from __future__ import annotations

import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import analyze_bot_perf as perf


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
REAL_SOAK_FIXTURE = REPO_ROOT / ".tmp" / "q3a_bot_nav_soak_10min_final.stdout.txt"
DEFAULT_BUDGET = pathlib.Path(__file__).resolve().parent / "default_soak_budget.json"


def synthetic_log(
    *,
    elapsed_ms: int | None = 2000,
    progress_elapsed_ms: int = 1000,
    bots: int = 2,
    commands: int = 40,
    route_requests: int = 20,
    route_queries: int = 5,
    route_refreshes: int = 5,
    route_reuses: int = 15,
    route_failures: int = 0,
    recovery_command_uses: int = 4,
    stuck_detections: int = 1,
) -> str:
    complete_line = ""
    if elapsed_ms is not None:
        complete_line = (
            "complete-prefix"
            f"{perf.SOAK_COMPLETE_MARKER} elapsed_ms={elapsed_ms} "
            f"duration_ms={elapsed_ms} count={bots} reports=1\n"
        )

    return (
        "server chatter before the smoke markers\n"
        f"noisy-prefix{perf.SOAK_BEGIN_MARKER} target={bots} duration_ms=2000 "
        f"progress_ms=1000 count={bots}\n"
        f"glued-output{perf.SOAK_PROGRESS_MARKER} elapsed_ms={progress_elapsed_ms} "
        f"duration_ms=2000 count={bots} reports=1\n"
        f"{complete_line}"
        "status-prefix"
        f"{perf.STATUS_MARKER} frames={commands} commands={commands} "
        f"route_requests={route_requests} route_queries={route_queries} "
        f"route_refreshes={route_refreshes} route_reuses={route_reuses} "
        f"route_commands={commands} route_failures={route_failures} "
        "route_invalid_slots=0 route_debug_routes=20 route_debug_goals=20 "
        "route_debug_missing_frames=0 route_debug_lines=10 route_debug_crosses=4 "
        "route_debug_arrows=2 route_debug_labels=2 "
        "route_debug_polyline_segments=12 "
        f"stuck_detections={stuck_detections} "
        f"stuck_recovery_activations={stuck_detections} "
        f"recovery_command_uses={recovery_command_uses} "
        "route_goal_assignments=3 item_goal_assignments=2 "
        "item_goal_reservation_skips=1 item_goal_peak_active_reservations=2 "
        "skipped_inactive=0 "
        f"expected_min_frames={bots} expected_min_commands={bots} pass=1\n"
    )


class BotPerfAnalyzerTests(unittest.TestCase):
    def write_text(self, root: pathlib.Path, name: str, text: str) -> pathlib.Path:
        path = root / name
        path.write_text(text, encoding="utf-8")
        return path

    def test_noisy_marker_prefixes_and_complete_duration(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(pathlib.Path(temp), "smoke.log", synthetic_log())

            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            self.assertEqual(parsed.soak_begin["target"], 2)
            self.assertEqual(parsed.progress[0]["elapsed_ms"], 1000)
            self.assertEqual(parsed.soak_complete["elapsed_ms"], 2000)
            self.assertEqual(parsed.status_lines, 1)
            self.assertEqual(report["duration_sec"], 2.0)
            self.assertEqual(report["bot_count"], 2)
            self.assertEqual(report["commands_per_bot_sec"], 10.0)
            self.assertEqual(report["route_refresh_ratio"], 0.25)

    def test_progress_duration_fallback_without_complete_line(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(
                pathlib.Path(temp),
                "progress-only.log",
                synthetic_log(elapsed_ms=None, progress_elapsed_ms=1500),
            )

            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            self.assertIsNone(parsed.soak_complete)
            self.assertEqual(perf.choose_duration_ms(parsed), 1500)
            self.assertEqual(report["duration_sec"], 1.5)

    def test_budget_pass_and_fail_function_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(pathlib.Path(temp), "smoke.log", synthetic_log())
            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            pass_budget = perf.Budget(
                path=pathlib.Path("pass-budget.json"),
                metrics={"commands_per_bot_sec": {"min": 5, "max": 20}},
                status={"route_failures": {"max": 0}},
            )
            fail_budget = perf.Budget(
                path=pathlib.Path("fail-budget.json"),
                metrics={"commands_per_bot_sec": {"min": 999}},
                status={},
            )

            self.assertTrue(perf.evaluate_budget(report, parsed.status, pass_budget)["pass"])

            failure = perf.evaluate_budget(report, parsed.status, fail_budget)
            self.assertFalse(failure["pass"])
            self.assertIn("commands_per_bot_sec", failure["failures"][0])

    def test_budget_failure_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            log_path = self.write_text(root, "smoke.log", synthetic_log())
            budget_path = self.write_text(
                root,
                "budget.json",
                json.dumps({
                    "checks": {
                        "metrics": {
                            "commands_per_bot_sec": {"min": 999}
                        }
                    }
                }),
            )

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = perf.main(["--budget", str(budget_path), str(log_path)])

            self.assertEqual(exit_code, 1)
            self.assertIn("failure:", stdout.getvalue())

    def test_multi_run_comparison_shape(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = perf.analyze(perf.parse_log(
                self.write_text(root, "first.log", synthetic_log(commands=40))
            ))
            second = perf.analyze(perf.parse_log(
                self.write_text(root, "second.log", synthetic_log(commands=20))
            ))

            first["budget"] = {"pass": True, "failures": [], "warnings": [], "checks": []}
            second["budget"] = {"pass": False, "failures": ["synthetic"], "warnings": [], "checks": []}

            comparison = perf.build_comparison([first, second])
            command_metric = next(
                metric for metric in comparison["metrics"]
                if metric["key"] == "commands_per_bot_sec"
            )

            self.assertEqual(comparison["run_count"], 2)
            self.assertEqual(comparison["latest_file"], second["file"])
            self.assertEqual(command_metric["best_run"], 1)
            self.assertEqual(command_metric["worst_run"], 2)
            self.assertEqual(comparison["budget"]["passed"], 1)
            self.assertEqual(comparison["budget"]["failed"], 1)
            self.assertFalse(comparison["budget"]["latest_pass"])

    def test_comparison_guards_mixed_scenarios(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = perf.analyze(
                perf.parse_log(self.write_text(root, "spawn.log", synthetic_log(bots=1))),
                {"name": "spawn_route_to_item", "duration_seconds": 1.0},
            )
            second = perf.analyze(
                perf.parse_log(self.write_text(root, "recover.log", synthetic_log(bots=2))),
                {"name": "recover_from_stall", "duration_seconds": 2.0},
            )

            comparison = perf.build_comparison([first, second])
            guards = {guard["code"]: guard for guard in comparison["guards"]}

            self.assertIn("mixed_scenarios", guards)
            self.assertIn("mixed_bot_counts", guards)
            self.assertEqual(
                guards["mixed_scenarios"]["values"],
                ["spawn_route_to_item", "recover_from_stall"],
            )
            self.assertEqual(guards["mixed_bot_counts"]["values"], ["1", "2"])

    def test_scenario_report_duration_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            log_path = self.write_text(root, "scenario.stdout.txt", synthetic_log())
            report_path = self.write_text(
                root,
                "latest_report.json",
                json.dumps({
                    "scenarios": [
                        {
                            "name": "synthetic_scenario",
                            "status": "passed",
                            "stdout_path": str(log_path),
                            "duration_seconds": 4.0,
                            "duration_budget_passed": True,
                            "returncode": 0,
                        }
                    ]
                }),
            )

            metadata = perf.load_scenario_report(report_path)
            parsed = perf.parse_log(log_path)
            analyzed = perf.analyze(parsed, metadata[perf.path_key(log_path)])

            self.assertEqual(analyzed["scenario_name"], "synthetic_scenario")
            self.assertEqual(analyzed["duration_source"], "scenario_report")
            self.assertEqual(analyzed["duration_sec"], 4.0)
            self.assertEqual(analyzed["commands_per_bot_sec"], 5.0)

    def test_real_soak_fixture_baseline_when_available(self) -> None:
        if not REAL_SOAK_FIXTURE.is_file():
            self.skipTest(f"optional soak fixture is missing: {REAL_SOAK_FIXTURE}")

        parsed = perf.parse_log(REAL_SOAK_FIXTURE)
        report = perf.analyze(parsed)
        budget = perf.load_budget(DEFAULT_BUDGET)
        budget_result = perf.evaluate_budget(report, parsed.status, budget)

        self.assertEqual(report["pass"], 1)
        self.assertEqual(report["route_failures"], 0)
        self.assertAlmostEqual(report["commands_per_bot_sec"], 40.0, delta=1.0)
        self.assertAlmostEqual(report["route_refresh_ratio"], 0.285, delta=0.02)
        self.assertTrue(budget_result["pass"])


if __name__ == "__main__":
    unittest.main()
