#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import run_bot_scenarios as harness


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
LATEST_REPORT_FIXTURE = REPO_ROOT / ".tmp" / "bot_scenarios" / "latest_report.json"


class BotScenarioHarnessTests(unittest.TestCase):
    def test_status_parsing_with_noisy_prefix_uses_last_status(self) -> None:
        text = "\n".join((
            "server chatter before status",
            "bot noise q3a_bot_frame_command_status frames=8 commands=8 route_failures=1 pass=0",
            "prefixed output q3a_bot_frame_command_status frames=92 commands=92 "
            "route_failures=0 last_debug_filter_client=-1 pass=1",
            "q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete "
            "pass_source=q3a_bot_frame_command_status pass=1",
        ))

        line, metrics = harness.parse_status_line(text)

        self.assertIsNotNone(line)
        self.assertTrue(line.startswith("prefixed output"))
        self.assertEqual(metrics["frames"], 92)
        self.assertEqual(metrics["commands"], 92)
        self.assertEqual(metrics["route_failures"], 0)
        self.assertEqual(metrics["last_debug_filter_client"], -1)
        self.assertEqual(metrics["pass"], 1)

    def test_status_parsing_prefers_positive_command_proof_over_cleanup_status(self) -> None:
        text = "\n".join((
            "q3a_bot_frame_command_status frames=184 commands=183 route_commands=183 "
            "route_failures=0 item_goal_active_reservations=8 "
            "expected_min_commands=8 pass=1",
            "q3a_bot_frame_command_smoke_map_repeat_cleanup_status_requested "
            "cycle=2 phase=post_reload reason=final_cycle_complete count=0 status_line=next",
            "q3a_bot_frame_command_status frames=184 commands=183 route_commands=183 "
            "route_failures=0 item_goal_active_reservations=0 "
            "expected_min_commands=0 pass=1",
            "q3a_bot_frame_command_smoke_map_repeat_cleanup_status "
            "cycle=2 phase=post_reload reason=final_cycle_complete "
            "count=0 active_reservations=0 pass=1 status_line=previous",
        ))

        line, metrics = harness.parse_status_line(text)

        self.assertIsNotNone(line)
        self.assertEqual(metrics["expected_min_commands"], 8)
        self.assertEqual(metrics["item_goal_active_reservations"], 8)
        self.assertEqual(metrics["route_commands"], 183)
        self.assertEqual(metrics["pass"], 1)

    def test_mode_19_marker_metric_parsing(self) -> None:
        marker = "q3a_bot_frame_command_smoke_map_repeat=complete"
        text = "\n".join((
            "q3a_bot_frame_command_smoke_map_repeat_cycle=complete cycle=1 completed_cycles=1",
            "log prefix q3a_bot_frame_command_smoke_map_repeat=complete "
            "cycles=2 map_changes=1 final_spawncount=432101776 final_count=0",
        ))

        parsed = harness.parse_marker_metrics(text, {marker})

        self.assertIn(marker, parsed)
        self.assertEqual(len(parsed[marker]), 1)
        self.assertEqual(parsed[marker][0]["cycles"], 2)
        self.assertEqual(parsed[marker][0]["map_changes"], 1)
        self.assertEqual(parsed[marker][0]["final_count"], 0)

    def test_check_evaluation_pass_fail_and_missing(self) -> None:
        passing = harness.evaluate_check(
            harness.MetricCheck("route_failures", "eq", 0),
            {"route_failures": 0},
        )
        failing = harness.evaluate_check(
            harness.MetricCheck("commands", "ge", 8),
            {"commands": 7},
        )
        missing = harness.evaluate_check(
            harness.MetricCheck("item_goal_assignments", "gt", 0),
            {},
        )

        self.assertTrue(passing["passed"])
        self.assertFalse(failing["passed"])
        self.assertEqual(failing["actual"], 7)
        self.assertFalse(missing["passed"])
        self.assertIsNone(missing["actual"])

    def test_pending_scenario_catalog_output_shape(self) -> None:
        pending_scenarios = harness.select_scenarios(["pending"])
        report = harness.catalog_report(pending_scenarios)

        self.assertEqual(report["summary"]["total"], 4)
        self.assertEqual(report["summary"]["implemented"], 0)
        self.assertEqual(report["summary"]["pending"], 4)

        engage_enemy = next(
            scenario for scenario in report["scenarios"]
            if scenario["name"] == "engage_enemy"
        )
        self.assertEqual(engage_enemy["status"], "pending")
        self.assertEqual(engage_enemy["task_ids"], ["DV-03-T05"])
        self.assertIsNone(engage_enemy["smoke_mode"])
        self.assertEqual(engage_enemy["planned_smoke_mode"], 20)
        self.assertEqual(engage_enemy["runtime_budget_seconds"], 0)
        self.assertEqual(engage_enemy["required_metrics"], [])
        self.assertIn("combat_damage_events", engage_enemy["promotion_required_metrics"])
        self.assertIn("action_applied_attack_buttons", engage_enemy["promotion_required_metrics"])
        self.assertTrue(engage_enemy["pending_blockers"])

        markdown = harness.build_markdown_report(report)
        self.assertIn("# Bot Scenario Catalog", markdown)
        self.assertIn("Pending Blockers", markdown)
        self.assertIn("engage_enemy", markdown)

    def test_pending_gap_report_identifies_missing_rows_and_metrics(self) -> None:
        report = harness.pending_gap_report(
            [harness.scenario_map()["engage_enemy"]],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
        )

        self.assertEqual(report["summary"]["total"], 1)
        self.assertEqual(report["summary"]["ready"], 0)
        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(report["summary"]["missing_rows"], 1)
        self.assertEqual(report["summary"]["overall"], "blocked")

        engage_enemy = report["scenarios"][0]
        self.assertEqual(engage_enemy["status"], "blocked")
        self.assertIn("fixture report has no scenario row named engage_enemy", engage_enemy["blockers"])
        self.assertIn("combat_damage_events", engage_enemy["missing_metrics"])

        markdown = harness.build_markdown_report(report)
        self.assertIn("# Bot Scenario Pending Gap Report", markdown)
        self.assertIn("combat_damage_events", markdown)

    def test_pending_gap_report_marks_ready_when_source_metrics_exist(self) -> None:
        scenario = harness.scenario_map()["engage_enemy"]
        metrics = {metric: 1 for metric in scenario.promotion_metrics}
        metrics["route_failures"] = 0
        fixture = {
            "scenarios": [
                {
                    "name": "engage_enemy",
                    "status": "passed",
                    "smoke_mode": 20,
                    "metrics": metrics,
                    "markers": {},
                },
            ],
        }

        report = harness.pending_gap_report([scenario], fixture, pathlib.Path("latest_report.json"))
        engage_enemy = report["scenarios"][0]

        self.assertEqual(report["summary"]["ready"], 1)
        self.assertEqual(report["summary"]["blocked"], 0)
        self.assertEqual(report["summary"]["overall"], "ready")
        self.assertEqual(engage_enemy["status"], "ready")
        self.assertEqual(engage_enemy["missing_metrics"], [])
        self.assertEqual(engage_enemy["blockers"], [])

    def test_comparison_metric_deltas(self) -> None:
        previous = {
            "scenarios": [
                {
                    "name": "spawn_route_to_item",
                    "status": "passed",
                    "metrics": {"commands": 8, "route_failures": 0, "pass": 1},
                    "duration_seconds": 1.5,
                },
                {
                    "name": "removed_case",
                    "status": "passed",
                    "metrics": {"commands": 1},
                },
            ],
        }
        current = {
            "scenarios": [
                {
                    "name": "spawn_route_to_item",
                    "status": "failed",
                    "metrics": {"commands": 10, "route_failures": 1, "pass": 0},
                    "duration_seconds": 2.0,
                },
                {
                    "name": "new_case",
                    "status": "pending",
                    "metrics": {},
                },
            ],
        }

        comparison = harness.compare_reports(
            current,
            previous,
            pathlib.Path("previous.json"),
        )

        self.assertEqual(comparison["summary"]["total"], 3)
        self.assertEqual(comparison["summary"]["matched"], 1)
        self.assertEqual(comparison["summary"]["added"], 1)
        self.assertEqual(comparison["summary"]["removed"], 1)
        self.assertEqual(comparison["summary"]["status_changed"], 3)
        self.assertEqual(comparison["summary"]["metric_changed"], 2)

        spawn = next(
            scenario for scenario in comparison["scenarios"]
            if scenario["name"] == "spawn_route_to_item"
        )
        self.assertEqual(spawn["previous_status"], "passed")
        self.assertEqual(spawn["current_status"], "failed")
        self.assertTrue(spawn["status_changed"])
        self.assertEqual(spawn["metric_changes"]["commands"]["delta"], 2)
        self.assertEqual(spawn["metric_changes"]["route_failures"]["delta"], 1)
        self.assertEqual(spawn["metric_changes"]["pass"]["delta"], -1)
        self.assertEqual(spawn["metric_changes"]["duration_seconds"]["delta"], 0.5)

    def test_latest_report_fixture_when_available(self) -> None:
        if not LATEST_REPORT_FIXTURE.is_file():
            self.skipTest(f"optional fixture missing: {LATEST_REPORT_FIXTURE}")

        report = json.loads(LATEST_REPORT_FIXTURE.read_text(encoding="utf-8"))
        scenarios = harness.report_scenario_map(report)
        required = {
            "spawn_route_to_item",
            "recover_from_stall",
            "multi_bot_reservation",
        }

        missing = sorted(required - set(scenarios))
        self.assertEqual(missing, [], f"latest report missing implemented scenario rows: {missing}")

        self.assert_passed_route_clean(scenarios["spawn_route_to_item"])
        self.assertGreaterEqual(scenarios["spawn_route_to_item"]["metrics"]["commands"], 1)
        self.assertGreaterEqual(scenarios["spawn_route_to_item"]["metrics"]["route_commands"], 1)
        self.assertGreaterEqual(scenarios["spawn_route_to_item"]["metrics"]["item_goal_assignments"], 1)

        self.assert_passed_route_clean(scenarios["recover_from_stall"])
        self.assertGreaterEqual(scenarios["recover_from_stall"]["metrics"]["stuck_detections"], 1)
        self.assertGreaterEqual(scenarios["recover_from_stall"]["metrics"]["recovery_command_uses"], 1)

        self.assert_passed_route_clean(scenarios["multi_bot_reservation"])
        self.assertGreaterEqual(
            scenarios["multi_bot_reservation"]["metrics"]["item_goal_peak_active_reservations"],
            8,
        )

        if "map_change_repeat" in scenarios:
            map_repeat = scenarios["map_change_repeat"]
            self.assert_passed_route_clean(map_repeat)
            key_metrics = harness.scenario_key_metrics(map_repeat)
            self.assertGreaterEqual(key_metrics["item_goal_peak_active_reservations"], 8)
            self.assertEqual(key_metrics["cycles"], 2)
            self.assertEqual(key_metrics["map_changes"], 1)
            self.assertEqual(key_metrics["final_count"], 0)

        gap_report = harness.pending_gap_report(
            harness.select_scenarios(["pending"]),
            report,
            LATEST_REPORT_FIXTURE,
        )
        self.assertEqual(gap_report["summary"]["total"], 4)
        self.assertEqual(gap_report["summary"]["ready"], 0)
        self.assertEqual(gap_report["summary"]["blocked"], 4)
        self.assertGreaterEqual(gap_report["summary"]["missing_rows"], 1)

    def assert_passed_route_clean(self, scenario: dict) -> None:
        self.assertEqual(scenario["status"], "passed")
        self.assertEqual(scenario["metrics"]["pass"], 1)
        self.assertEqual(scenario["metrics"]["route_failures"], 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
