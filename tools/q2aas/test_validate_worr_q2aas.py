#!/usr/bin/env python3
"""Regression tests for WORR q2aas validation manifest handling."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import validate_worr_q2aas as validator


def write_manifest(path: pathlib.Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


class Q2AasManifestTests(unittest.TestCase):
    def test_reference_coverage_reports_skipped_missing_maps(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            staged_map = root / ".install" / "basew" / "maps" / "mm-rage.bsp"
            staged_map.parent.mkdir(parents=True, exist_ok=True)
            staged_map.write_bytes(b"fake bsp presence is enough for manifest loading")

            manifest_path = root / "tools" / "q2aas" / "validation_manifest.json"
            write_manifest(
                manifest_path,
                {
                    "schema": validator.VALIDATION_MANIFEST_SCHEMA,
                    "version": validator.VALIDATION_MANIFEST_VERSION,
                    "task_ids": ["FR-04-T11", "FR-04-T16", "DV-07-T06"],
                    "maps": [
                        {
                            "id": "mm-rage",
                            "path": ".install/basew/maps/mm-rage.bsp",
                            "required": True,
                            "coverage_categories": ["worr_current_dm"],
                        },
                        {
                            "id": "q2dm1",
                            "path": ".install/basew/maps/q2dm1.bsp",
                            "required": False,
                            "coverage_categories": ["id_deathmatch_reference"],
                        },
                    ],
                    "reference_coverage": [
                        {
                            "id": "worr_current_dm",
                            "map_ids": ["mm-rage"],
                            "minimum_validated_maps": 1,
                        },
                        {
                            "id": "id_deathmatch_reference",
                            "map_ids": ["q2dm1"],
                            "minimum_validated_maps": 1,
                        },
                        {
                            "id": "water_reference",
                            "map_ids": [],
                            "minimum_validated_maps": 1,
                            "required_features": ["water"],
                        },
                    ],
                },
            )

            maps, ok, report = validator.load_manifest(
                root,
                manifest_path,
                skip_missing=True,
                packaged_map_cache_dir=root / ".tmp" / "q2aas" / "packaged-maps",
            )

            self.assertTrue(ok, report["errors"])
            self.assertEqual([entry["id"] for entry in maps], ["mm-rage"])
            self.assertEqual(report["loaded_map_count"], 1)
            self.assertEqual(report["skipped_maps"][0]["id"], "q2dm1")
            self.assertEqual(
                report["skipped_maps"][0]["coverage_categories"],
                ["id_deathmatch_reference"],
            )

            coverage = report["reference_coverage"]
            self.assertEqual(coverage["status"], "incomplete")
            self.assertIn("id_deathmatch_reference", coverage["incomplete_categories"])
            categories = {entry["id"]: entry for entry in coverage["categories"]}
            self.assertEqual(categories["worr_current_dm"]["status"], "passed")
            self.assertEqual(categories["id_deathmatch_reference"]["status"], "incomplete")
            self.assertEqual(categories["water_reference"]["status"], "incomplete")
            self.assertEqual(
                categories["water_reference"]["candidate_absences"][0]["status"],
                "no_candidate_declared",
            )
            self.assertIn("water_reference", coverage["strict_failed_categories"])
            self.assertEqual(coverage["strict_gate"]["status"], "failed")
            self.assertEqual(coverage["missing_maps"][0]["id"], "q2dm1")

    def test_reference_coverage_schema_errors_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            manifest_path = root / "tools" / "q2aas" / "validation_manifest.json"
            write_manifest(
                manifest_path,
                {
                    "schema": validator.VALIDATION_MANIFEST_SCHEMA,
                    "version": validator.VALIDATION_MANIFEST_VERSION,
                    "task_ids": ["FR-04-T11"],
                    "maps": [],
                    "reference_coverage": [
                        {
                            "id": "bad-minimum",
                            "map_ids": ["q2dm1"],
                            "minimum_validated_maps": 0,
                            "required_features": ["not_a_feature"],
                            "strict_required": "yes",
                        },
                        {
                            "id": "bad-map-list",
                            "map_ids": "q2dm1",
                        },
                    ],
                },
            )

            _, ok, report = validator.load_manifest(
                root,
                manifest_path,
                skip_missing=True,
                packaged_map_cache_dir=root / ".tmp" / "q2aas" / "packaged-maps",
            )

            self.assertFalse(ok)
            errors = "\n".join(str(error) for error in report["errors"])
            self.assertIn("reference_coverage[0].minimum_validated_maps", errors)
            self.assertIn("required_features.not_a_feature", errors)
            self.assertIn("strict_required for reference_coverage[0]", errors)
            self.assertIn("map_ids for reference_coverage[1]", errors)

    def test_coverage_feature_readiness_uses_contents_entities_and_travel_counts(self) -> None:
        readiness = validator.build_coverage_feature_readiness(
            {
                "flag_counts": {
                    "water": 2,
                    "slime": 0,
                    "lava": 1,
                }
            },
            {
                "doors": [{"classname": "func_door"}],
                "elevators": [],
                "teleports": [{"classname": "trigger_teleport"}],
            },
            {
                "elevator": 1,
                "teleport": 0,
            },
        )

        self.assertEqual(readiness["features"]["water"]["status"], "present")
        self.assertEqual(readiness["features"]["slime"]["status"], "absent")
        self.assertEqual(readiness["features"]["lava"]["status"], "present")
        self.assertEqual(readiness["features"]["teleport"]["status"], "present")
        self.assertEqual(readiness["features"]["elevator"]["status"], "present")
        self.assertEqual(readiness["features"]["door"]["status"], "present")

    def test_reachability_and_mover_policy_reports_use_travel_counts(self) -> None:
        groups = {
            "doors": [{"classname": "func_door", "model": "*1"}],
            "elevators": [{"classname": "func_plat", "model": "*2"}],
            "teleports": [{"classname": "trigger_teleport", "target": "dest"}],
            "hurt": [],
        }
        travel_counts = {
            "swim": 3,
            "water jump": 1,
            "teleport": 2,
            "elevator": 4,
            "rocket jump": 5,
        }
        semantic_policy = {
            "contents": {
                "water": {
                    "q2_brush_count": 2,
                }
            }
        }

        reachability = validator.build_reachability_policy(
            groups,
            travel_counts,
            semantic_policy,
        )
        mover_routes = validator.build_mover_route_report(groups, travel_counts)
        metadata_policy = validator.build_metadata_policy()

        self.assertEqual(reachability["water_entry_exit"]["status"], "validated")
        self.assertEqual(reachability["movers"]["status"], "validated")
        self.assertEqual(reachability["teleports"]["status"], "validated")
        self.assertEqual(
            reachability["rocketjump_action"]["status"],
            "route_policy_only_action_deferred",
        )
        self.assertEqual(mover_routes["generated_routes"]["TRAVEL_ELEVATOR"], 4)
        self.assertEqual(mover_routes["generated_routes"]["TRAVEL_TELEPORT"], 2)
        self.assertEqual(mover_routes["status"], "generated_mover_routes")
        self.assertEqual(metadata_policy["package_policy"], "do_not_package_sidecars")

    def test_objective_reports_record_ctf_and_campaign_readiness(self) -> None:
        areas = [
            {
                "areanum": 1,
                "mins": (-16.0, -16.0, -16.0),
                "maxs": (16.0, 16.0, 16.0),
                "center": (0.0, 0.0, 0.0),
            },
            {
                "areanum": 2,
                "mins": (96.0, -16.0, -16.0),
                "maxs": (128.0, 16.0, 16.0),
                "center": (112.0, 0.0, 0.0),
            },
        ]
        reachable = {1, 2}
        groups = {
            "ctf_flags": [
                {"classname": "item_flag_team1", "origin": "0 0 0"},
                {"classname": "item_flag_team2", "origin": "112 0 0"},
            ],
            "team_spawns": [
                {"classname": "info_player_team1", "origin": "0 0 0"},
                {"classname": "info_player_team2", "origin": "112 0 0"},
            ],
            "campaign_progression_targets": [
                {"classname": "target_goal", "origin": "112 0 0"},
            ],
            "campaign_keys": [
                {"classname": "key_red_key", "origin": "0 0 0"},
            ],
            "triggers": [
                {"classname": "trigger_once", "model": "*1"},
            ],
            "doors": [
                {"classname": "func_door", "model": "*2"},
            ],
            "elevators": [],
        }

        team_report = validator.build_team_objective_report(groups, areas, reachable)
        campaign_report = validator.build_campaign_progression_report(groups, areas, reachable)

        self.assertEqual(team_report["status"], "validated")
        self.assertEqual(team_report["flag_coverage"]["reachable_from_spawn_areas"], 2)
        self.assertEqual(campaign_report["status"], "validated")
        self.assertEqual(campaign_report["progression_target_coverage"]["reachable_from_spawn_areas"], 1)
        self.assertEqual(campaign_report["key_coverage"]["reachable_from_spawn_areas"], 1)

    def test_manifest_requirement_gate_does_not_inherit_global_strictness(self) -> None:
        manifest_spec = {
            "source": "tools/q2aas/validation_manifest.json",
            "require_high_value_reachability": False,
        }
        cli_spec = {
            "source": "cli",
            "require_high_value_reachability": False,
        }

        self.assertFalse(
            validator.resolve_requirement_gate(
                manifest_spec,
                "require_high_value_reachability",
                True,
            )
        )
        self.assertTrue(
            validator.resolve_requirement_gate(
                cli_spec,
                "require_high_value_reachability",
                True,
            )
        )


if __name__ == "__main__":
    unittest.main()
