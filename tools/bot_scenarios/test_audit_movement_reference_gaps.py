#!/usr/bin/env python3
"""Tests for bot movement reference gap audit tooling."""

from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import audit_movement_reference_gaps as audit


def scenario_rows(*, gap_tagged: bool = True, crouch_route: bool = False) -> dict[str, dict[str, object]]:
    tags = ["movement", "navigation"]
    if gap_tagged:
        tags.append("gap")
    crouch_name = "movement_crouch_route" if crouch_route else "movement_crouch_gap"
    return {
        crouch_name: {
            "name": crouch_name,
            "smoke_mode": 92,
            "map_name": "worr_crouch_ref" if crouch_route else None,
            "selection_tags": list(tags),
        },
        "movement_hazard_context": {
            "name": "movement_hazard_context",
            "smoke_mode": 96,
            "map_name": "fact2",
            "selection_tags": list(tags),
        },
    }


def feature_category(
    category_id: str,
    *,
    passed: bool = False,
    candidate_maps: list[dict[str, object]] | None = None,
) -> dict[str, object]:
    return {
        "id": category_id,
        "status": "passed" if passed else "incomplete",
        "readiness": "feature_ready" if passed else "feature_absent",
        "candidate_absence_count": 0 if passed else 1,
        "candidate_absences": [] if passed else [{"status": "no_candidate_declared"}],
        "candidate_maps": candidate_maps or [],
        "strict_gate": {
            "status": "passed" if passed else "failed",
            "passed": passed,
        },
    }


def map_report(
    map_id: str,
    *,
    crouch: int = 0,
    slime_brushes: int = 0,
    slime_areas: int = 0,
    lava_brushes: int = 0,
    lava_areas: int = 0,
    trigger_hurt: int = 0,
    target_laser: int = 0,
) -> dict[str, object]:
    return {
        "id": map_id,
        "path": f".install/basew/maps/{map_id}.bsp",
        "status": "passed",
        "travel_counts": {
            "walk": 10,
            "crouch": crouch,
        },
        "diagnostics": {
            "entities": {
                "classname_counts": {
                    "trigger_hurt": trigger_hurt,
                    "target_laser": target_laser,
                },
            },
            "aas_semantic_policy": {
                "contents": {
                    "slime": {
                        "q2_brush_count": slime_brushes,
                        "aas_area_count": slime_areas,
                    },
                    "lava": {
                        "q2_brush_count": lava_brushes,
                        "aas_area_count": lava_areas,
                    },
                    "hurt": {
                        "entity_count": trigger_hurt,
                    },
                },
            },
        },
    }


def q2aas_report(
    *,
    crouch: int = 0,
    crouch_category: bool = False,
    slime_category: bool = False,
    lava_category: bool = False,
    trigger_hurt: int = 0,
    target_laser: int = 0,
) -> dict[str, object]:
    crouch_candidates = (
        [{"id": "crouch_test", "feature_status": "passed"}]
        if crouch_category
        else []
    )
    slime_candidates = (
        [{"id": "slime_test", "feature_status": "passed"}]
        if slime_category
        else []
    )
    lava_candidates = (
        [{"id": "lava_test", "feature_status": "passed"}]
        if lava_category
        else []
    )
    return {
        "reference_feature_readiness": {
            "categories": [
                feature_category(
                    "crouch_reference",
                    passed=crouch_category,
                    candidate_maps=crouch_candidates,
                ),
                feature_category(
                    "slime_reference",
                    passed=slime_category,
                    candidate_maps=slime_candidates,
                ),
                feature_category(
                    "lava_reference",
                    passed=lava_category,
                    candidate_maps=lava_candidates,
                ),
            ],
        },
        "maps": [
            map_report(
                "crouch_test",
                crouch=crouch,
                trigger_hurt=trigger_hurt,
                target_laser=target_laser,
            ),
        ],
    }


def check_by_id(result: dict[str, object], check_id: str) -> dict[str, object]:
    for check in result["checks"]:
        if check["id"] == check_id:
            return check
    raise AssertionError(f"missing check {check_id}")


class MovementReferenceGapAuditTests(unittest.TestCase):
    def test_current_gap_signals_block_promotion(self) -> None:
        result = audit.build_audit(q2aas_report(), scenario_rows())

        self.assertEqual(result["status"], "blocked")
        self.assertEqual(check_by_id(result, "natural_crouch")["status"], "blocked_no_reference_content")
        self.assertEqual(check_by_id(result, "hazard_context")["status"], "blocked_no_runtime_hazard_content")
        self.assertIn(
            "No staged map reports generated TRAVEL_CROUCH reachability.",
            check_by_id(result, "natural_crouch")["blockers"],
        )
        self.assertIn(
            "No staged map reports trigger_hurt, target_laser, or misc_lavaball runtime hazard entities.",
            check_by_id(result, "hazard_context")["blockers"],
        )

    def test_crouch_reference_marks_gap_ready_for_promotion(self) -> None:
        result = audit.build_audit(
            q2aas_report(crouch=4, crouch_category=True),
            scenario_rows(),
        )

        crouch_check = check_by_id(result, "natural_crouch")
        self.assertEqual(crouch_check["status"], "ready_for_promotion")
        self.assertTrue(crouch_check["promotion_ready"])
        self.assertEqual(
            crouch_check["evidence"]["candidate_maps"][0]["travel_count"],
            4,
        )
        self.assertIn(
            "movement_crouch_gap still carries the expected-blocked gap contract.",
            crouch_check["blockers"],
        )

    def test_runtime_hazard_entity_marks_gap_ready_for_promotion(self) -> None:
        result = audit.build_audit(
            q2aas_report(trigger_hurt=1, target_laser=2),
            scenario_rows(),
        )

        hazard_check = check_by_id(result, "hazard_context")
        self.assertEqual(hazard_check["status"], "ready_for_promotion")
        self.assertTrue(hazard_check["promotion_ready"])
        self.assertEqual(
            hazard_check["evidence"]["runtime_hazard_candidate_maps"][0]["runtime_hazard_entities"],
            3,
        )

    def test_liquid_hazard_reference_marks_gap_ready_for_promotion(self) -> None:
        result = audit.build_audit(
            q2aas_report(slime_category=True),
            scenario_rows(),
        )

        hazard_check = check_by_id(result, "hazard_context")
        self.assertEqual(hazard_check["status"], "blocked_no_runtime_hazard_content")
        self.assertFalse(hazard_check["promotion_ready"])
        self.assertTrue(hazard_check["evidence"]["liquid_reference_ready"])
        self.assertEqual(
            hazard_check["evidence"]["liquid_candidate_maps"][0]["category"],
            "slime_reference",
        )
        self.assertIn(
            "Slime/lava AAS reference coverage exists, but the runtime hazard scenario still needs trigger_hurt, target_laser, or misc_lavaball map entities.",
            hazard_check["notes"],
        )

    def test_accepted_when_references_exist_and_rows_are_no_longer_gap_tagged(self) -> None:
        result = audit.build_audit(
            q2aas_report(crouch=2, crouch_category=True, trigger_hurt=1),
            scenario_rows(gap_tagged=False, crouch_route=True),
        )

        self.assertEqual(result["status"], "accepted")
        self.assertEqual(check_by_id(result, "natural_crouch")["status"], "accepted")
        self.assertEqual(check_by_id(result, "hazard_context")["status"], "accepted")

    def test_markdown_reports_blockers(self) -> None:
        markdown = audit.render_markdown(
            audit.build_audit(q2aas_report(), scenario_rows(), q2aas_report_path="report.json")
        )

        self.assertIn("# Bot Movement Reference Gap Audit", markdown)
        self.assertIn("Status: `blocked`", markdown)
        self.assertIn("q2aas report: `report.json`", markdown)
        self.assertIn("No staged map reports generated TRAVEL_CROUCH reachability.", markdown)


if __name__ == "__main__":
    unittest.main()
