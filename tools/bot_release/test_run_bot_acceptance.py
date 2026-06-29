#!/usr/bin/env python3
"""Tests for the WORR bot release acceptance runner."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import run_bot_acceptance as acceptance


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


def write_json(path: pathlib.Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


def scenario_payload(
    *,
    total: int = 114,
    passed: int = 114,
    failed: int = 0,
    required: bool = True,
) -> dict:
    names = list(acceptance.REQUIRED_SCENARIOS) if required else ["spawn_route_to_item"]
    scenarios = [
        {
            "name": name,
            "selection_tags": ["movement"] if name.startswith("movement_") else [],
        }
        for name in names
    ]
    while len(scenarios) < total:
        scenarios.append({"name": f"filler_{len(scenarios)}", "selection_tags": []})
    return {
        "summary": {
            "total": total,
            "passed": passed,
            "failed": failed,
            "timeout": 0,
            "error": 0,
            "pending": 0,
            "overall": "pass" if failed == 0 else "fail",
        },
        "scenarios": scenarios,
    }


class BotAcceptanceUnitTests(unittest.TestCase):
    def test_scenario_report_passes_required_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            report = root / ".tmp" / "bot_scenarios" / "implemented.json"
            write_json(report, scenario_payload())

            result = acceptance.check_scenario_report(
                root,
                report,
                min_implemented_rows=114,
                allow_missing=False,
            )

        self.assertEqual("pass", result.status)
        self.assertEqual(114, result.metrics["total"])

    def test_scenario_report_fails_missing_required_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            report = root / ".tmp" / "bot_scenarios" / "small.json"
            write_json(report, scenario_payload(total=20, passed=20, required=False))

            result = acceptance.check_scenario_report(
                root,
                report,
                min_implemented_rows=114,
                allow_missing=False,
            )

        self.assertEqual("fail", result.status)
        self.assertTrue(any("expected at least 114" in failure for failure in result.failures))
        self.assertTrue(any("behavior_arbitration" in failure for failure in result.failures))

    def test_missing_scenario_report_can_warn(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)

            result = acceptance.check_scenario_report(
                root,
                None,
                min_implemented_rows=114,
                allow_missing=True,
            )

        self.assertEqual("warn", result.status)
        self.assertTrue(result.warnings)

    def test_bots_txt_requires_first_party_roster(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            bots_txt = root / "assets" / "botfiles" / "bots.txt"
            bots_txt.parent.mkdir(parents=True)
            bots_txt.write_text(
                """
                { name vanguard aifile bots/vanguard_c.c }
                { name vector aifile bots/vector_c.c }
                """,
                encoding="utf-8",
            )

            result = acceptance.check_bots_txt(root)

        self.assertEqual("fail", result.status)
        self.assertTrue(any("bulwark" in failure for failure in result.failures))

    def test_playtest_plan_covers_required_modes_and_min_players(self) -> None:
        result = acceptance.check_playtest_plan(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertEqual(4, result.metrics["cases"])
        self.assertEqual(4, result.metrics["modes"])

    def test_playtest_triage_classifies_playtest_failures(self) -> None:
        result = acceptance.check_playtest_triage(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertGreaterEqual(result.metrics["categories"], 7)
        self.assertGreaterEqual(result.metrics["case_failure_signals"], 10)

    def test_perf_tooling_validates_budget_artifacts(self) -> None:
        result = acceptance.check_perf_tooling(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertEqual(2, result.metrics["budgets"])
        self.assertEqual(1, result.metrics["variance_budgets"])

    def test_current_repo_acceptance_core_passes_with_existing_artifacts(self) -> None:
        report = acceptance.run_acceptance(
            REPO_ROOT,
            scenario_report=REPO_ROOT / ".tmp" / "bot_scenarios" / "implemented_hazard_context.json",
            min_implemented_rows=114,
        )

        self.assertEqual("passed", report["summary"]["status"])


if __name__ == "__main__":
    unittest.main()
