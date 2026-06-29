#!/usr/bin/env python3
"""Tests for the source-counter variance soak runner."""

from __future__ import annotations

import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import run_source_counter_variance_soak as runner


class SourceCounterVarianceSoakRunnerTests(unittest.TestCase):
    def write_json(self, root: pathlib.Path, name: str, payload: object) -> pathlib.Path:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        return path

    def test_dry_run_writes_planned_commands_without_real_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            artifact_dir = root / "artifacts"
            json_out = root / "soak-plan.json"
            markdown_out = root / "soak.md"

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = runner.main([
                    "--repo-root",
                    str(root),
                    "--dry-run",
                    "--runs",
                    "2",
                    "--artifact-dir",
                    str(artifact_dir),
                    "--json-out",
                    str(json_out),
                    "--markdown-out",
                    str(markdown_out),
                    "--format",
                    "json",
                ])

            self.assertEqual(0, exit_code)
            report = json.loads(json_out.read_text(encoding="utf-8"))
            self.assertEqual("dry-run", report["status"])
            self.assertEqual(2, len(report["runs"]))
            self.assertEqual(
                "high_bot_soak_degradation",
                report["runs"][0]["command"][report["runs"][0]["command"].index("--scenario") + 1],
            )
            self.assertIn("--variance-budget", report["analysis_command"])
            self.assertIn(str(markdown_out), report["analysis_command"])
            self.assertEqual(report, json.loads(stdout.getvalue()))

    def test_scenario_stdout_path_prefers_named_scenario(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            fallback_log = root / "fallback.stdout.txt"
            target_log = root / "target.stdout.txt"
            report_path = self.write_json(
                root,
                "scenario-report.json",
                {
                    "scenarios": [
                        {"name": "other", "stdout_path": str(fallback_log)},
                        {
                            "name": "high_bot_soak_degradation",
                            "stdout_path": str(target_log),
                        },
                    ]
                },
            )

            self.assertEqual(
                target_log,
                runner.scenario_stdout_path(report_path, "high_bot_soak_degradation"),
            )

    def test_merged_scenario_report_keeps_all_run_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = self.write_json(
                root,
                "run-01/scenario-report.json",
                {
                    "scenarios": [
                        {
                            "name": "high_bot_soak_degradation",
                            "stdout_path": str(root / "run-01/stdout.txt"),
                            "duration_seconds": 600.0,
                        }
                    ]
                },
            )
            second = self.write_json(
                root,
                "run-02/scenario-report.json",
                {
                    "scenarios": [
                        {
                            "name": "high_bot_soak_degradation",
                            "stdout_path": str(root / "run-02/stdout.txt"),
                            "duration_seconds": 601.0,
                        }
                    ]
                },
            )

            payload = runner.merged_scenario_report_payload(
                [first, second],
                root,
                root / "artifacts",
                "20260629T000000Z",
            )

            self.assertEqual(2, len(payload["scenarios"]))
            self.assertEqual(1, payload["scenarios"][0]["source_run_index"])
            self.assertEqual(2, payload["scenarios"][1]["source_run_index"])
            self.assertEqual(str(first), payload["input_reports"][0])

    def test_analyzer_command_includes_budget_variance_report_and_stdout_logs(self) -> None:
        config = runner.RunnerConfig(
            repo_root=pathlib.Path("E:/repo"),
            python_executable="py",
            scenario_runner=pathlib.Path("E:/repo/tools/bot_scenarios/run_bot_scenarios.py"),
            perf_analyzer=pathlib.Path("E:/repo/tools/bot_perf/analyze_bot_perf.py"),
            binary=pathlib.Path("E:/repo/.install/worr_ded_x86_64.exe"),
            install_dir=pathlib.Path("E:/repo/.install"),
            budget=pathlib.Path("E:/repo/tools/bot_perf/source_counter_soak_budget.json"),
            variance_budget=pathlib.Path("E:/repo/tools/bot_perf/source_counter_variance_budget.json"),
            artifact_dir=pathlib.Path("E:/repo/.tmp/bot_perf/source_counter_variance_soak"),
            json_out=pathlib.Path("E:/repo/.tmp/bot_perf/source_counter_variance_soak/report.json"),
            markdown_out=pathlib.Path("E:/repo/.tmp/bot_perf/source_counter_variance_soak/report.md"),
            scenario="high_bot_soak_degradation",
            game="basew",
            map_name="mm-rage",
            runs=2,
            timeout=720,
            base_port=28000,
            dry_run=False,
            output_format="text",
        )

        command = runner.build_analyzer_command(
            config,
            [pathlib.Path("a.stdout.txt"), pathlib.Path("b.stdout.txt")],
            pathlib.Path("combined.json"),
        )

        self.assertEqual("py", command[0])
        self.assertIn("--budget", command)
        self.assertIn(str(config.budget), command)
        self.assertIn("--variance-budget", command)
        self.assertIn(str(config.variance_budget), command)
        self.assertIn("--scenario-report", command)
        self.assertTrue(command[-2].endswith("a.stdout.txt"))
        self.assertTrue(command[-1].endswith("b.stdout.txt"))

    def test_runs_must_support_variance_comparison(self) -> None:
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            runner.parse_args(["--runs", "1"])

        self.assertEqual(2, raised.exception.code)
        self.assertIn("--runs must be at least 2", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
