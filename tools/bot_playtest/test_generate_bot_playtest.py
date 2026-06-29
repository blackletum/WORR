#!/usr/bin/env python3
"""Tests for the WORR bot multiplayer playtest generator."""

from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

import generate_bot_playtest as playtest


class BotPlaytestGeneratorTests(unittest.TestCase):
    def test_default_cases_cover_required_modes(self) -> None:
        cases = playtest.default_playtest_cases()
        modes = {case.mode for case in cases}

        self.assertEqual(set(playtest.REQUIRED_MODES), modes)
        self.assertEqual([], playtest.validate_cases(cases))

    def test_generated_outputs_include_markdown_json_and_configs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            output_dir = root / "out"

            payload = playtest.generate_outputs(repo_root=root, output_dir=output_dir)

            self.assertEqual(playtest.SCHEMA, payload["schema"])
            self.assertEqual(4, payload["summary"]["cases"])
            self.assertEqual(["CTF", "Duel", "FFA", "TDM"], payload["summary"]["modes"])
            self.assertTrue((output_dir / "bot_multiplayer_playtest.md").is_file())
            json_path = output_dir / "bot_multiplayer_playtest.json"
            self.assertTrue(json_path.is_file())
            notes_path = output_dir / "bot_multiplayer_playtest_notes_template.json"
            self.assertTrue(notes_path.is_file())
            self.assertEqual(payload["schema"], json.loads(json_path.read_text())["schema"])
            self.assertEqual(
                playtest.NOTES_SCHEMA,
                json.loads(notes_path.read_text())["schema"],
            )
            for case in playtest.default_playtest_cases():
                self.assertTrue((output_dir / case.config_name).is_file())

    def test_generated_configs_use_canonical_public_bot_surface(self) -> None:
        cases = playtest.default_playtest_cases()
        config_text = "\n".join(case.config_text() for case in cases)

        self.assertIn("set bot_min_players 4", config_text)
        self.assertIn("set bot_duel_live_pacing 1", config_text)
        self.assertIn("set bot_ctf_objective_transitions 1", config_text)
        for token in playtest.FORBIDDEN_PUBLIC_TOKENS:
            self.assertNotIn(token, config_text)

    def test_markdown_targets_observed_behavior_failures(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            output_dir = root / "out"

            playtest.generate_outputs(repo_root=root, output_dir=output_dir)
            markdown = (output_dir / "bot_multiplayer_playtest.md").read_text(
                encoding="utf-8"
            )

        self.assertIn("spinning in place", markdown)
        self.assertIn("wall-sticking", markdown)
        self.assertIn("point-blank overlap", markdown)
        self.assertIn("blaster-only suicide pushes", markdown)
        self.assertIn("botlist", markdown)

    def test_each_case_resets_mode_policy_cvars_before_specific_setup(self) -> None:
        for case in playtest.default_playtest_cases():
            commands = list(case.command_lines())
            first_mode_policy = commands.index("set bot_ffa_roam_route 0")
            map_index = commands.index(f"map {case.map_name}")
            self.assertLess(first_mode_policy, map_index)
            self.assertLess(commands.index("set bot_min_players 0"), map_index)


if __name__ == "__main__":
    unittest.main()
