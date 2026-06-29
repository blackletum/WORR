#!/usr/bin/env python3
"""Triage WORR multiplayer bot playtest notes into scenario candidates."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass
from typing import Any, Iterable


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DEFAULT_PLAYTEST_JSON = pathlib.Path(".tmp") / "bot_playtest" / "bot_multiplayer_playtest.json"
DEFAULT_NOTES_JSON = pathlib.Path(".tmp") / "bot_playtest" / "bot_multiplayer_playtest_notes_template.json"
DEFAULT_OUTPUT_DIR = pathlib.Path(".tmp") / "bot_playtest"
TRIAGE_SCHEMA = "worr.bot_playtest.triage.v1"
ALLOWED_OUTCOMES = {"pass", "fail", "blocked", "pending", "skip"}

sys.path.insert(0, str(SCRIPT_DIR))
import generate_bot_playtest  # noqa: E402


@dataclass(frozen=True)
class FailureCategory:
    key: str
    title: str
    scenario_candidate: str
    tasks: tuple[str, ...]
    patterns: tuple[str, ...]
    description: str
    critical: bool = False


FAILURE_CATEGORIES: tuple[FailureCategory, ...] = (
    FailureCategory(
        key="route_commitment",
        title="Route Commitment",
        scenario_candidate="playtest_route_commitment_regression",
        tasks=("FR-04-T02", "FR-04-T14", "FR-04-T15"),
        patterns=("spin", "spins", "spinning", "one spot", "one place"),
        description=(
            "A bot fails to commit to a route or stable decision when no immediate "
            "target should own the frame."
        ),
    ),
    FailureCategory(
        key="route_stuck",
        title="Route Stuck Or Wall Pressure",
        scenario_candidate="playtest_route_stuck_regression",
        tasks=("FR-04-T05", "FR-04-T14", "FR-04-T16"),
        patterns=("wall", "corner", "pile", "cluster", "pressed", "stuck"),
        description="A bot reaches an AAS/navigation failure or collision trap during play.",
    ),
    FailureCategory(
        key="close_threat_spacing",
        title="Close-Threat Spacing",
        scenario_candidate="playtest_close_threat_spacing_regression",
        tasks=("FR-04-T04", "FR-04-T15"),
        patterns=("point-blank", "nose-to-nose", "touching", "overlap", "walking forward"),
        description=(
            "A bot has a nearby player directly ahead and fails to strafe, back up, "
            "switch target, or otherwise make space."
        ),
    ),
    FailureCategory(
        key="weak_retreat",
        title="Weak-State Retreat",
        scenario_candidate="playtest_weak_retreat_regression",
        tasks=("FR-04-T03", "FR-04-T15"),
        patterns=(
            "blaster",
            "weak",
            "low-resource",
            "low health",
            "health",
            "armor",
            "stronger",
            "stacked",
            "poor weapon",
            "unfavorable attack",
        ),
        description=(
            "A weak or poorly armed bot continues a bad fight instead of recovering "
            "health, armor, ammo, or weapon position."
        ),
    ),
    FailureCategory(
        key="min_players",
        title="Min-Players Autofill",
        scenario_candidate="playtest_min_players_regression",
        tasks=("FR-04-T07", "FR-04-T13", "FR-04-T16"),
        patterns=("bot_min_players", "target population", "target active count", "below two"),
        description="The public min-player target does not fill or trim the bot population.",
        critical=True,
    ),
    FailureCategory(
        key="duel_queue",
        title="Duel Queue And Active Count",
        scenario_candidate="playtest_duel_queue_regression",
        tasks=("FR-04-T04", "FR-04-T06"),
        patterns=("queue", "spectator", "extra active", "maxplayers", "duel participants"),
        description="Duel active-player limits or surplus-player queue handling regressed.",
        critical=True,
    ),
    FailureCategory(
        key="ctf_objective",
        title="CTF Objective Response",
        scenario_candidate="playtest_ctf_objective_regression",
        tasks=("FR-04-T04", "FR-04-T15"),
        patterns=(
            "ctf",
            "flag",
            "carrier",
            "base",
            "pickup",
            "drop",
            "dropped",
            "return",
            "objective",
        ),
        description=(
            "CTF pickup, drop, return, carrier-support, or base-return decisions "
            "did not produce the expected route/combat response."
        ),
        critical=True,
    ),
    FailureCategory(
        key="team_fire_spacing",
        title="Team Fire And Team Spacing",
        scenario_candidate="playtest_team_fire_spacing_regression",
        tasks=("FR-04-T04", "FR-04-T15"),
        patterns=("friendly", "teammate", "team shooting", "firing lane", "through them"),
        description="Team-mode bots fail to respect teammate lines or team spacing.",
    ),
)


def resolve_repo_path(repo_root: pathlib.Path, path: pathlib.Path) -> pathlib.Path:
    if path.is_absolute():
        return path
    return repo_root / path


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def category_matches(signal: str) -> list[FailureCategory]:
    text = signal.lower()
    return [
        category
        for category in FAILURE_CATEGORIES
        if any(pattern in text for pattern in category.patterns)
    ]


def categories_for_signal(signal: str) -> list[str]:
    matches = category_matches(signal)
    if not matches:
        return ["unclassified"]
    return [category.key for category in matches]


def category_by_key() -> dict[str, FailureCategory]:
    return {category.key: category for category in FAILURE_CATEGORIES}


def default_notes_from_plan(plan: dict[str, Any]) -> dict[str, Any]:
    cases = []
    for case in plan.get("cases", []):
        cases.append(
            {
                "id": case["id"],
                "outcome": "pending",
                "duration_minutes": case.get("duration_minutes", 0),
                "botlist": "",
                "profiles_observed": [],
                "failure_signals": [],
                "custom_failure_signals": [],
                "repro_steps": [],
                "notes": "",
            }
        )
    return {
        "schema": generate_bot_playtest.NOTES_SCHEMA,
        "generated_at": plan.get("generated_at", ""),
        "playtest_artifact": "bot_multiplayer_playtest.json",
        "operator": "",
        "build": "",
        "server": "",
        "notes": "",
        "cases": cases,
    }


def validate_notes(plan: dict[str, Any], notes: dict[str, Any]) -> list[str]:
    warnings: list[str] = []
    plan_ids = {case.get("id") for case in plan.get("cases", [])}
    note_ids = {case.get("id") for case in notes.get("cases", [])}
    missing = sorted(case_id for case_id in plan_ids - note_ids if case_id)
    unknown = sorted(case_id for case_id in note_ids - plan_ids if case_id)
    for case_id in missing:
        warnings.append(f"notes missing playtest case: {case_id}")
    for case_id in unknown:
        warnings.append(f"notes include unknown playtest case: {case_id}")

    for case in notes.get("cases", []):
        outcome = str(case.get("outcome", "pending")).lower()
        if outcome not in ALLOWED_OUTCOMES:
            warnings.append(
                f"{case.get('id', '<unknown>')} has unsupported outcome {outcome!r}; "
                "treating it as pending"
            )
    return warnings


def validate_category_coverage(cases: Iterable[generate_bot_playtest.PlaytestCase]) -> list[str]:
    missing: list[str] = []
    for case in cases:
        for signal in case.failure_signals:
            if categories_for_signal(signal) == ["unclassified"]:
                missing.append(f"{case.case_id}: {signal}")
    return missing


def normalize_outcome(raw: Any) -> str:
    outcome = str(raw or "pending").lower()
    if outcome not in ALLOWED_OUTCOMES:
        return "pending"
    return outcome


def failure_signal_rows(case_note: dict[str, Any]) -> list[str]:
    signals: list[str] = []
    for key in ("failure_signals", "custom_failure_signals"):
        value = case_note.get(key, [])
        if isinstance(value, str):
            signals.append(value)
        else:
            signals.extend(str(item) for item in value if str(item).strip())
    return signals


def triage(
    plan: dict[str, Any],
    notes: dict[str, Any],
    *,
    repeat_threshold: int = 2,
) -> dict[str, Any]:
    warnings = validate_notes(plan, notes)
    categories = category_by_key()
    plan_cases = {case["id"]: case for case in plan.get("cases", [])}
    note_cases = {case.get("id"): case for case in notes.get("cases", [])}
    case_results: list[dict[str, Any]] = []
    category_counts: dict[str, int] = {}
    category_cases: dict[str, set[str]] = {}
    unclassified: list[dict[str, str]] = []
    outcome_counts = {outcome: 0 for outcome in sorted(ALLOWED_OUTCOMES)}

    for case_id, plan_case in plan_cases.items():
        note = note_cases.get(case_id, {})
        outcome = normalize_outcome(note.get("outcome"))
        outcome_counts[outcome] += 1
        signals = failure_signal_rows(note)
        signal_rows: list[dict[str, Any]] = []
        for signal in signals:
            keys = categories_for_signal(signal)
            signal_rows.append({"text": signal, "categories": keys})
            for key in keys:
                category_counts[key] = category_counts.get(key, 0) + 1
                category_cases.setdefault(key, set()).add(case_id)
            if keys == ["unclassified"]:
                unclassified.append({"case": case_id, "signal": signal})

        if outcome == "fail" and not signals:
            warnings.append(f"{case_id} is marked fail but has no failure signals")

        case_results.append(
            {
                "id": case_id,
                "mode": plan_case.get("mode", ""),
                "map": plan_case.get("map", ""),
                "outcome": outcome,
                "duration_minutes": note.get(
                    "duration_minutes",
                    plan_case.get("duration_minutes", 0),
                ),
                "signals": signal_rows,
                "botlist_present": bool(str(note.get("botlist", "")).strip()),
                "notes": note.get("notes", ""),
                "repro_steps": note.get("repro_steps", []),
            }
        )

    scenario_candidates: list[dict[str, Any]] = []
    for key, count in sorted(category_counts.items()):
        if key == "unclassified":
            scenario_candidates.append(
                {
                    "category": key,
                    "title": "Unclassified Playtest Failure",
                    "scenario_candidate": "playtest_unclassified_regression",
                    "tasks": ["FR-04-T15", "DV-07-T06"],
                    "evidence_count": count,
                    "cases": sorted(category_cases.get(key, set())),
                    "promote_to_scenario": count >= repeat_threshold,
                    "promotion_reason": (
                        f"repeated {count} times" if count >= repeat_threshold else "needs classification"
                    ),
                    "description": "A failure signal was not recognized by the triage catalog.",
                }
            )
            continue

        category = categories[key]
        repeated = count >= repeat_threshold
        promote = repeated or category.critical
        scenario_candidates.append(
            {
                "category": key,
                "title": category.title,
                "scenario_candidate": category.scenario_candidate,
                "tasks": list(category.tasks),
                "evidence_count": count,
                "cases": sorted(category_cases.get(key, set())),
                "promote_to_scenario": promote,
                "promotion_reason": (
                    f"repeated {count} times"
                    if repeated
                    else "single critical release signal"
                    if category.critical
                    else "watchlist"
                ),
                "description": category.description,
            }
        )

    failed_cases = outcome_counts.get("fail", 0)
    blocked_cases = outcome_counts.get("blocked", 0)
    pending_cases = outcome_counts.get("pending", 0)
    status = "failed" if failed_cases or blocked_cases else "pending" if pending_cases else "passed"

    return {
        "schema": TRIAGE_SCHEMA,
        "playtest_schema": plan.get("schema", ""),
        "notes_schema": notes.get("schema", ""),
        "summary": {
            "status": status,
            "cases": len(plan_cases),
            "passed": outcome_counts.get("pass", 0),
            "failed": failed_cases,
            "blocked": blocked_cases,
            "pending": pending_cases,
            "skipped": outcome_counts.get("skip", 0),
            "failure_signals": sum(category_counts.values()),
            "scenario_candidates": len(scenario_candidates),
            "promoted_candidates": sum(
                1 for candidate in scenario_candidates if candidate["promote_to_scenario"]
            ),
            "warnings": len(warnings),
        },
        "warnings": warnings,
        "cases": case_results,
        "failure_categories": [
            {
                "category": key,
                "count": count,
                "cases": sorted(category_cases.get(key, set())),
            }
            for key, count in sorted(category_counts.items())
        ],
        "scenario_candidates": scenario_candidates,
        "unclassified": unclassified,
    }


def render_markdown(report: dict[str, Any]) -> str:
    summary = report["summary"]
    lines = [
        "# WORR Bot Playtest Triage",
        "",
        f"Status: `{summary['status']}`",
        (
            f"Cases: `{summary['cases']}` passed `{summary['passed']}` "
            f"failed `{summary['failed']}` blocked `{summary['blocked']}` "
            f"pending `{summary['pending']}` skipped `{summary['skipped']}`"
        ),
        (
            f"Failure signals: `{summary['failure_signals']}`; "
            f"scenario candidates: `{summary['scenario_candidates']}`; "
            f"promoted candidates: `{summary['promoted_candidates']}`"
        ),
        "",
    ]
    if report["warnings"]:
        lines.extend(["## Warnings", ""])
        lines.extend(f"- {warning}" for warning in report["warnings"])
        lines.append("")

    lines.extend(["## Cases", ""])
    for case in report["cases"]:
        lines.append(
            f"- `{case['id']}` ({case['mode']} on `{case['map']}`): `{case['outcome']}`"
        )
        for signal in case["signals"]:
            lines.append(
                f"  - {signal['text']} -> `{', '.join(signal['categories'])}`"
            )
    lines.append("")

    lines.extend(["## Scenario Candidates", ""])
    if not report["scenario_candidates"]:
        lines.append("- None.")
    for candidate in report["scenario_candidates"]:
        promote = "promote" if candidate["promote_to_scenario"] else "watch"
        lines.append(
            f"- `{candidate['scenario_candidate']}` [{promote}]: "
            f"{candidate['title']} in {', '.join(candidate['cases']) or 'no cases'} "
            f"({candidate['promotion_reason']})."
        )
    lines.append("")
    return "\n".join(lines)


def write_report(
    report: dict[str, Any],
    *,
    output_dir: pathlib.Path,
) -> dict[str, str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "bot_multiplayer_playtest_triage.json"
    markdown_path = output_dir / "bot_multiplayer_playtest_triage.md"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    markdown_path.write_text(render_markdown(report), encoding="utf-8", newline="\n")
    return {
        "json": str(json_path),
        "markdown": str(markdown_path),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=pathlib.Path, default=REPO_ROOT)
    parser.add_argument("--plan", type=pathlib.Path, default=DEFAULT_PLAYTEST_JSON)
    parser.add_argument("--notes", type=pathlib.Path, default=DEFAULT_NOTES_JSON)
    parser.add_argument("--output-dir", type=pathlib.Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--repeat-threshold", type=int, default=2)
    parser.add_argument("--format", choices=("text", "json"), default="text")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    plan_path = resolve_repo_path(repo_root, args.plan)
    notes_path = resolve_repo_path(repo_root, args.notes)
    output_dir = resolve_repo_path(repo_root, args.output_dir)

    plan = load_json(plan_path)
    notes = load_json(notes_path) if notes_path.is_file() else default_notes_from_plan(plan)
    report = triage(plan, notes, repeat_threshold=args.repeat_threshold)
    artifacts = write_report(report, output_dir=output_dir)
    report["artifacts"] = artifacts

    if args.format == "json":
        print(json.dumps(report, indent=2))
    else:
        summary = report["summary"]
        print(
            "bot playtest triage: "
            f"{summary['status']} cases={summary['cases']} "
            f"failed={summary['failed']} blocked={summary['blocked']} "
            f"pending={summary['pending']} candidates={summary['scenario_candidates']}"
        )
        print(f"json: {artifacts['json']}")
        print(f"markdown: {artifacts['markdown']}")
    return 0 if report["summary"]["status"] in {"passed", "pending"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
