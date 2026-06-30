#!/usr/bin/env python3
"""Audit whether bot movement gap scenarios have promotable map evidence."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

SCHEMA = "worr-bot-movement-reference-gap-audit-v1"
DEFAULT_Q2AAS_REPORT = Path(".tmp") / "q2aas" / "validation-report.json"
RUNTIME_HAZARD_CLASSES = ("trigger_hurt", "target_laser", "misc_lavaball")


JsonDict = dict[str, Any]


def as_dict(value: object) -> JsonDict:
    return value if isinstance(value, dict) else {}


def as_list(value: object) -> list[object]:
    return value if isinstance(value, list) else []


def as_int(value: object) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str):
        try:
            return int(value)
        except ValueError:
            return 0
    return 0


def read_json(path: Path) -> JsonDict:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def write_json(path: Path, payload: JsonDict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def load_scenario_rows() -> tuple[dict[str, JsonDict], str | None]:
    try:
        import run_bot_scenarios as scenarios
    except Exception as exc:  # pragma: no cover - defensive CLI diagnostic
        return {}, f"could not import run_bot_scenarios: {exc}"

    rows: dict[str, JsonDict] = {}
    for name in (
        "movement_crouch_route",
        "movement_crouch_gap",
        "movement_hazard_context",
        "movement_hazard_context_gap",
    ):
        scenario = scenarios.scenario_map().get(name)
        if scenario is None:
            continue
        rows[name] = {
            "name": scenario.name,
            "smoke_mode": scenario.smoke_mode,
            "map_name": scenario.map_name,
            "selection_tags": list(scenario.selection_tags),
        }
    return rows, None


def category_by_id(q2aas_report: JsonDict, category_id: str) -> JsonDict:
    readiness = as_dict(q2aas_report.get("reference_feature_readiness"))
    for category in as_list(readiness.get("categories")):
        category_report = as_dict(category)
        if category_report.get("id") == category_id:
            return category_report
    return {}


def category_summary(category: JsonDict, category_id: str) -> JsonDict:
    if not category:
        return {
            "id": category_id,
            "status": "missing",
            "readiness": "not_reported",
            "candidate_absence_count": 0,
            "candidate_absences": [],
            "candidate_maps": [],
        }
    return {
        "id": category.get("id", category_id),
        "status": category.get("status", "unknown"),
        "readiness": category.get("readiness", "unknown"),
        "candidate_absence_count": as_int(category.get("candidate_absence_count")),
        "candidate_absences": as_list(category.get("candidate_absences")),
        "candidate_maps": as_list(category.get("candidate_maps")),
        "strict_gate": as_dict(category.get("strict_gate")),
    }


def category_passed(category: JsonDict) -> bool:
    if category.get("status") == "passed":
        return True
    strict_gate = as_dict(category.get("strict_gate"))
    return strict_gate.get("passed") is True


def scenario_summary(rows: dict[str, JsonDict], scenario_name: str) -> JsonDict:
    row = rows.get(scenario_name, {})
    tags = [str(tag) for tag in as_list(row.get("selection_tags"))]
    gap_tagged = "gap" in tags if row else scenario_name.endswith("_gap")
    if not row:
        return {
            "name": scenario_name,
            "present": False,
            "smoke_mode": None,
            "map_name": None,
            "selection_tags": [],
            "gap_tagged": gap_tagged,
        }
    return {
        "name": scenario_name,
        "present": True,
        "smoke_mode": row.get("smoke_mode"),
        "map_name": row.get("map_name"),
        "selection_tags": tags,
        "gap_tagged": gap_tagged,
    }


def q2aas_maps(q2aas_report: JsonDict) -> list[JsonDict]:
    return [as_dict(entry) for entry in as_list(q2aas_report.get("maps"))]


def crouch_map_evidence(q2aas_report: JsonDict) -> list[JsonDict]:
    candidates: list[JsonDict] = []
    for map_report in q2aas_maps(q2aas_report):
        travel_counts = as_dict(map_report.get("travel_counts"))
        crouch_count = as_int(travel_counts.get("crouch"))
        if crouch_count <= 0:
            continue
        candidates.append({
            "id": map_report.get("id"),
            "status": map_report.get("status", "unknown"),
            "path": map_report.get("path"),
            "travel_count": crouch_count,
        })
    return candidates


def all_crouch_observations(q2aas_report: JsonDict) -> list[JsonDict]:
    observations: list[JsonDict] = []
    for map_report in q2aas_maps(q2aas_report):
        travel_counts = as_dict(map_report.get("travel_counts"))
        observations.append({
            "id": map_report.get("id"),
            "status": map_report.get("status", "unknown"),
            "travel_count": as_int(travel_counts.get("crouch")),
        })
    return observations


def semantic_contents(map_report: JsonDict) -> JsonDict:
    diagnostics = as_dict(map_report.get("diagnostics"))
    semantic = as_dict(diagnostics.get("aas_semantic_policy"))
    return as_dict(semantic.get("contents"))


def entity_class_counts(map_report: JsonDict) -> JsonDict:
    diagnostics = as_dict(map_report.get("diagnostics"))
    entities = as_dict(diagnostics.get("entities"))
    return as_dict(entities.get("classname_counts"))


def hazard_entity_count(map_report: JsonDict) -> int:
    class_counts = entity_class_counts(map_report)
    total = sum(as_int(class_counts.get(classname)) for classname in RUNTIME_HAZARD_CLASSES)
    hurt = as_dict(semantic_contents(map_report).get("hurt"))
    total = max(total, as_int(hurt.get("entity_count")))
    return total


def runtime_hazard_map_evidence(q2aas_report: JsonDict) -> list[JsonDict]:
    candidates: list[JsonDict] = []
    for map_report in q2aas_maps(q2aas_report):
        count = hazard_entity_count(map_report)
        if count <= 0:
            continue
        class_counts = entity_class_counts(map_report)
        candidates.append({
            "id": map_report.get("id"),
            "status": map_report.get("status", "unknown"),
            "path": map_report.get("path"),
            "runtime_hazard_entities": count,
            "classes": {
                classname: as_int(class_counts.get(classname))
                for classname in RUNTIME_HAZARD_CLASSES
                if as_int(class_counts.get(classname)) > 0
            },
        })
    return candidates


def all_hazard_observations(q2aas_report: JsonDict) -> list[JsonDict]:
    observations: list[JsonDict] = []
    for map_report in q2aas_maps(q2aas_report):
        contents = semantic_contents(map_report)
        slime = as_dict(contents.get("slime"))
        lava = as_dict(contents.get("lava"))
        observations.append({
            "id": map_report.get("id"),
            "status": map_report.get("status", "unknown"),
            "slime_brush_count": as_int(slime.get("q2_brush_count")),
            "slime_aas_area_count": as_int(slime.get("aas_area_count")),
            "lava_brush_count": as_int(lava.get("q2_brush_count")),
            "lava_aas_area_count": as_int(lava.get("aas_area_count")),
            "runtime_hazard_entities": hazard_entity_count(map_report),
        })
    return observations


def build_check_status(ready: bool, scenario: JsonDict) -> str:
    if not ready:
        return "blocked_no_reference_content"
    if scenario.get("gap_tagged"):
        return "ready_for_promotion"
    return "accepted"


def build_crouch_check(q2aas_report: JsonDict, scenario_rows: dict[str, JsonDict]) -> JsonDict:
    scenario_name = "movement_crouch_route" if "movement_crouch_route" in scenario_rows else "movement_crouch_gap"
    scenario = scenario_summary(scenario_rows, scenario_name)
    category = category_by_id(q2aas_report, "crouch_reference")
    candidate_maps = crouch_map_evidence(q2aas_report)
    ready = category_passed(category) and bool(candidate_maps)
    blockers: list[str] = []
    if not category_passed(category):
        blockers.append("q2aas reference_feature_readiness has no passing crouch_reference category.")
    if not candidate_maps:
        blockers.append("No staged map reports generated TRAVEL_CROUCH reachability.")
    if ready and scenario.get("gap_tagged"):
        blockers.append(f"{scenario_name} still carries the expected-blocked gap contract.")

    return {
        "id": "natural_crouch",
        "title": "Natural crouch traversal",
        "status": build_check_status(ready, scenario),
        "promotion_ready": ready and scenario.get("gap_tagged"),
        "scenario": scenario,
        "blockers": blockers,
        "evidence": {
            "reference_category": category_summary(category, "crouch_reference"),
            "candidate_maps": candidate_maps,
            "observed_maps": all_crouch_observations(q2aas_report),
        },
    }


def liquid_hazard_candidate_maps(categories: list[JsonDict]) -> list[JsonDict]:
    candidates: list[JsonDict] = []
    for category in categories:
        for candidate in as_list(category.get("candidate_maps")):
            candidate_report = as_dict(candidate)
            if candidate_report.get("feature_status") == "passed":
                enriched = dict(candidate_report)
                enriched["category"] = category.get("id")
                candidates.append(enriched)
    return candidates


def build_hazard_check(q2aas_report: JsonDict, scenario_rows: dict[str, JsonDict]) -> JsonDict:
    scenario_name = "movement_hazard_context" if "movement_hazard_context" in scenario_rows else "movement_hazard_context_gap"
    scenario = scenario_summary(scenario_rows, scenario_name)
    slime_category = category_by_id(q2aas_report, "slime_reference")
    lava_category = category_by_id(q2aas_report, "lava_reference")
    liquid_categories = [slime_category, lava_category]
    liquid_ready = any(category_passed(category) for category in liquid_categories)
    liquid_maps = liquid_hazard_candidate_maps(liquid_categories)
    runtime_maps = runtime_hazard_map_evidence(q2aas_report)
    ready = bool(runtime_maps)
    blockers: list[str] = []
    notes: list[str] = []
    if not runtime_maps:
        blockers.append("No staged map reports trigger_hurt, target_laser, or misc_lavaball runtime hazard entities.")
    if liquid_ready and not runtime_maps:
        notes.append("Slime/lava AAS reference coverage exists, but the runtime hazard scenario still needs trigger_hurt, target_laser, or misc_lavaball map entities.")
    elif not liquid_ready:
        notes.append("No slime_reference or lava_reference category has passed yet.")
    if ready and scenario.get("gap_tagged"):
        blockers.append(f"{scenario_name} still carries the expected-blocked gap contract.")

    return {
        "id": "hazard_context",
        "title": "Hazard traversal/context",
        "status": build_check_status(ready, scenario) if ready else "blocked_no_runtime_hazard_content",
        "promotion_ready": ready and scenario.get("gap_tagged"),
        "scenario": scenario,
        "blockers": blockers,
        "notes": notes,
        "evidence": {
            "slime_reference": category_summary(slime_category, "slime_reference"),
            "lava_reference": category_summary(lava_category, "lava_reference"),
            "liquid_reference_ready": liquid_ready,
            "liquid_candidate_maps": liquid_maps,
            "runtime_hazard_candidate_maps": runtime_maps,
            "observed_maps": all_hazard_observations(q2aas_report),
        },
    }


def build_audit(
    q2aas_report: JsonDict,
    scenario_rows: dict[str, JsonDict] | None = None,
    *,
    q2aas_report_path: str | None = None,
) -> JsonDict:
    if scenario_rows is None:
        scenario_rows, scenario_warning = load_scenario_rows()
    else:
        scenario_warning = None

    checks = [
        build_crouch_check(q2aas_report, scenario_rows),
        build_hazard_check(q2aas_report, scenario_rows),
    ]
    blocked_count = sum(1 for check in checks if str(check["status"]).startswith("blocked"))
    ready_count = sum(1 for check in checks if check["status"] == "ready_for_promotion")
    accepted_count = sum(1 for check in checks if check["status"] == "accepted")
    if blocked_count:
        status = "blocked"
    elif ready_count:
        status = "ready_for_promotion"
    elif accepted_count == len(checks):
        status = "accepted"
    else:
        status = "unknown"

    warnings = []
    if scenario_warning:
        warnings.append(scenario_warning)

    return {
        "schema": SCHEMA,
        "status": status,
        "q2aas_report": q2aas_report_path,
        "summary": {
            "check_count": len(checks),
            "blocked": blocked_count,
            "ready_for_promotion": ready_count,
            "accepted": accepted_count,
        },
        "warnings": warnings,
        "checks": checks,
    }


def render_markdown(audit: JsonDict) -> str:
    lines = [
        "# Bot Movement Reference Gap Audit",
        "",
        f"Status: `{audit.get('status', 'unknown')}`",
        "",
    ]
    report_path = audit.get("q2aas_report")
    if report_path:
        lines.extend([f"q2aas report: `{report_path}`", ""])

    for check in as_list(audit.get("checks")):
        check_report = as_dict(check)
        lines.append(f"## {check_report.get('title', check_report.get('id'))}")
        lines.append("")
        lines.append(f"- Status: `{check_report.get('status', 'unknown')}`")
        scenario = as_dict(check_report.get("scenario"))
        lines.append(
            "- Scenario: "
            f"`{scenario.get('name')}` mode `{scenario.get('smoke_mode')}` "
            f"gap-tagged `{str(bool(scenario.get('gap_tagged'))).lower()}`"
        )
        blockers = [str(blocker) for blocker in as_list(check_report.get("blockers"))]
        if blockers:
            lines.append("- Blockers:")
            for blocker in blockers:
                lines.append(f"  - {blocker}")
        else:
            lines.append("- Blockers: none")
        notes = [str(note) for note in as_list(check_report.get("notes"))]
        if notes:
            lines.append("- Notes:")
            for note in notes:
                lines.append(f"  - {note}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def render_text(audit: JsonDict) -> str:
    lines = [f"status={audit.get('status', 'unknown')}"]
    for check in as_list(audit.get("checks")):
        check_report = as_dict(check)
        scenario = as_dict(check_report.get("scenario"))
        lines.append(
            f"{check_report.get('id')}: status={check_report.get('status')} "
            f"scenario={scenario.get('name')} mode={scenario.get('smoke_mode')}"
        )
        for blocker in as_list(check_report.get("blockers")):
            lines.append(f"  blocker: {blocker}")
        for note in as_list(check_report.get("notes")):
            lines.append(f"  note: {note}")
    return "\n".join(lines) + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--q2aas-report", type=Path, default=DEFAULT_Q2AAS_REPORT)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--markdown-out", type=Path)
    parser.add_argument(
        "--format",
        choices=("text", "json", "markdown"),
        default="text",
        help="Output format written to stdout.",
    )
    parser.add_argument(
        "--fail-on-ready",
        action="store_true",
        help="Return exit code 1 when map evidence is ready but the scenario rows are still gap-tagged.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(list(argv or sys.argv[1:]))
    if not args.q2aas_report.exists():
        print(f"q2aas report not found: {args.q2aas_report}", file=sys.stderr)
        return 2

    try:
        q2aas_report = read_json(args.q2aas_report)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"failed to read q2aas report: {exc}", file=sys.stderr)
        return 2

    audit = build_audit(q2aas_report, q2aas_report_path=str(args.q2aas_report))
    if args.json_out:
        write_json(args.json_out, audit)
    if args.markdown_out:
        args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
        args.markdown_out.write_text(render_markdown(audit), encoding="utf-8")

    if args.format == "json":
        print(json.dumps(audit, indent=2))
    elif args.format == "markdown":
        print(render_markdown(audit), end="")
    else:
        print(render_text(audit), end="")

    if args.fail_on_ready and audit["status"] == "ready_for_promotion":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
