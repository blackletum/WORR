#!/usr/bin/env python3
"""Tests for WORR bot multiplayer playtest triage."""

from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

import generate_bot_playtest
import triage_bot_playtest as triage


def build_plan() -> dict:
    cases = generate_bot_playtest.default_playtest_cases()
    return generate_bot_playtest.build_payload(
        cases,
        repo_root=pathlib.Path.cwd(),
        output_dir=pathlib.Path(".tmp") / "bot_playtest",
        base_game="basew",
        generated_at="2026-06-29T00:00:00Z",
    )


class BotPlaytestTriageTests(unittest.TestCase):
    def test_all_generated_failure_signals_are_classified(self) -> None:
        missing = triage.validate_category_coverage(
            generate_bot_playtest.default_playtest_cases()
        )

        self.assertEqual([], missing)

    def test_pending_template_triages_without_candidates(self) -> None:
        plan = build_plan()
        notes = triage.default_notes_from_plan(plan)

        report = triage.triage(plan, notes)

        self.assertEqual("pending", report["summary"]["status"])
        self.assertEqual(4, report["summary"]["pending"])
        self.assertEqual(0, report["summary"]["scenario_candidates"])
        self.assertEqual([], report["warnings"])

    def test_repeated_and_critical_failures_create_scenario_candidates(self) -> None:
        plan = build_plan()
        notes = triage.default_notes_from_plan(plan)
        notes["cases"][0]["outcome"] = "fail"
        notes["cases"][0]["failure_signals"] = [
            "A bot spins in one spot for more than five seconds with no visible target.",
            "A bot repeatedly charges a stacked enemy with only the blaster and no retreat attempt.",
        ]
        notes["cases"][1]["outcome"] = "fail"
        notes["cases"][1]["failure_signals"] = [
            "bot_min_players remains below two after the map has been running for several seconds.",
            "A bot ignores available weapons or armor while chasing a stronger opponent with the blaster.",
        ]
        notes["cases"][3]["outcome"] = "fail"
        notes["cases"][3]["failure_signals"] = [
            "Bots spin in place after a flag pickup, drop, or return transition.",
        ]

        report = triage.triage(plan, notes)
        candidates = {
            candidate["category"]: candidate
            for candidate in report["scenario_candidates"]
        }

        self.assertEqual("failed", report["summary"]["status"])
        self.assertTrue(candidates["weak_retreat"]["promote_to_scenario"])
        self.assertEqual("repeated 2 times", candidates["weak_retreat"]["promotion_reason"])
        self.assertTrue(candidates["min_players"]["promote_to_scenario"])
        self.assertEqual(
            "single critical release signal",
            candidates["min_players"]["promotion_reason"],
        )
        self.assertTrue(candidates["ctf_objective"]["promote_to_scenario"])

    def test_unknown_case_and_bad_outcome_warn(self) -> None:
        plan = build_plan()
        notes = triage.default_notes_from_plan(plan)
        notes["cases"].append({"id": "not_a_case", "outcome": "mystery"})

        report = triage.triage(plan, notes)

        self.assertTrue(any("unknown playtest case" in warning for warning in report["warnings"]))
        self.assertTrue(any("unsupported outcome" in warning for warning in report["warnings"]))

    def test_cli_writes_json_and_markdown_reports_for_pending_notes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            output_dir = root / "out"
            generated = generate_bot_playtest.generate_outputs(
                repo_root=root,
                output_dir=output_dir,
            )
            report = triage.triage(
                json.loads((output_dir / "bot_multiplayer_playtest.json").read_text()),
                json.loads(
                    (output_dir / "bot_multiplayer_playtest_notes_template.json").read_text()
                ),
            )
            artifacts = triage.write_report(report, output_dir=output_dir)

            self.assertEqual(4, generated["summary"]["cases"])
            self.assertTrue(pathlib.Path(artifacts["json"]).is_file())
            self.assertTrue(pathlib.Path(artifacts["markdown"]).is_file())


if __name__ == "__main__":
    unittest.main()
