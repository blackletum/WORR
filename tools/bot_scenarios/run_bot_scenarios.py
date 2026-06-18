#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Any


STATUS_MARKER = "q3a_bot_frame_command_status"
KEY_VALUE_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)=(-?\d+)\b")
STATUS_TOKEN_RE = re.compile(rf"(?:^|\s){re.escape(STATUS_MARKER)}(?:\s|$)")
FORBIDDEN_PATTERNS = (
    "commandMsec underflow",
)
KEY_METRICS = (
    "frames",
    "commands",
    "route_commands",
    "route_failures",
    "stuck_detections",
    "recovery_command_uses",
    "item_goal_assignments",
    "item_goal_peak_active_reservations",
    "cycles",
    "map_changes",
    "final_count",
    "duration_seconds",
    "pass",
)


@dataclass(frozen=True)
class MetricCheck:
    metric: str
    op: str
    expected: int
    note: str = ""


@dataclass(frozen=True)
class MarkerMetricCheck:
    marker: str
    metric: str
    op: str
    expected: int
    note: str = ""


@dataclass(frozen=True)
class Scenario:
    name: str
    title: str
    smoke_mode: int | None
    description: str
    task_ids: tuple[str, ...]
    budget_seconds: int
    checks: tuple[MetricCheck, ...] = field(default_factory=tuple)
    marker_checks: tuple[MarkerMetricCheck, ...] = field(default_factory=tuple)
    pending_reason: str = ""
    extra_cvars: tuple[tuple[str, str], ...] = field(default_factory=tuple)
    planned_smoke_mode: int | None = None
    promotion_metrics: tuple[str, ...] = field(default_factory=tuple)
    promotion_marker_metrics: tuple[tuple[str, str], ...] = field(default_factory=tuple)

    @property
    def implemented(self) -> bool:
        return self.smoke_mode is not None


SCENARIOS: tuple[Scenario, ...] = (
    Scenario(
        name="spawn_route_to_item",
        title="Spawn and route to item",
        smoke_mode=2,
        description="One bot spawns and receives an item-backed AAS route command.",
        task_ids=("DV-03-T05", "FR-04-T14"),
        budget_seconds=20,
        checks=(
            MetricCheck("pass", "eq", 1, "smoke status must pass"),
            MetricCheck("commands", "ge", 1, "bot command builder must emit commands"),
            MetricCheck("route_commands", "ge", 1, "route steering must drive commands"),
            MetricCheck("route_failures", "eq", 0, "item route must stay valid"),
            MetricCheck("item_goal_assignments", "ge", 1, "item goal must be selected"),
            MetricCheck("last_item_goal_area", "gt", 0, "item goal must resolve to an AAS area"),
        ),
    ),
    Scenario(
        name="recover_from_stall",
        title="Recover from stalled command",
        smoke_mode=4,
        description="Two bots build commands without applying movement, forcing stuck recovery.",
        task_ids=("DV-03-T05", "FR-04-T14"),
        budget_seconds=20,
        checks=(
            MetricCheck("pass", "eq", 1, "smoke status must pass"),
            MetricCheck("commands", "ge", 1, "command path must remain active"),
            MetricCheck("route_failures", "eq", 0, "recovery must not turn into route failures"),
            MetricCheck("stuck_detections", "ge", 1, "stalled movement must be detected"),
            MetricCheck("stuck_repath_refreshes", "ge", 1, "stuck detection must repath"),
            MetricCheck("stuck_recovery_activations", "ge", 1, "recovery policy must activate"),
            MetricCheck("recovery_command_uses", "ge", 1, "recovery commands must be emitted"),
        ),
    ),
    Scenario(
        name="multi_bot_reservation",
        title="Multi-bot route-command reservation",
        smoke_mode=17,
        description="Eight bots route concurrently while item reservations avoid duplicated goals.",
        task_ids=("DV-03-T05", "FR-04-T16"),
        budget_seconds=30,
        checks=(
            MetricCheck("pass", "eq", 1, "smoke status must pass"),
            MetricCheck("expected_min_commands", "ge", 8, "eight-bot target must be active"),
            MetricCheck("commands", "ge", 8, "all target bots must emit commands"),
            MetricCheck("route_commands", "ge", 8, "all target bots must route"),
            MetricCheck("route_failures", "eq", 0, "multi-bot route pressure must stay clean"),
            MetricCheck(
                "item_goal_peak_active_reservations",
                "ge",
                8,
                "reservation pressure proof must reach all eight bots",
            ),
        ),
    ),
    Scenario(
        name="map_change_repeat",
        title="Map-change repeat",
        smoke_mode=19,
        description="Eight bots route, unload/reload the active map, then repeat the route proof.",
        task_ids=("DV-03-T05", "FR-04-T16"),
        budget_seconds=45,
        checks=(
            MetricCheck("pass", "eq", 1, "final repeated smoke status must pass"),
            MetricCheck("expected_min_commands", "ge", 8, "eight-bot target must be active after reload"),
            MetricCheck("commands", "ge", 8, "post-reload bots must emit commands"),
            MetricCheck("route_commands", "ge", 8, "post-reload bots must route"),
            MetricCheck("route_failures", "eq", 0, "post-reload route proof must stay clean"),
            MetricCheck(
                "item_goal_peak_active_reservations",
                "ge",
                8,
                "post-reload reservation pressure proof must reach all eight bots",
            ),
        ),
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "cycles",
                "eq",
                2,
                "default repeat smoke must complete two proof cycles",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "map_changes",
                "eq",
                1,
                "two proof cycles must include one map reload",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "final_count",
                "eq",
                0,
                "bots must be removed after the final repeat cycle",
            ),
        ),
        extra_cvars=(("sv_bot_frame_command_smoke_map_repeat_cycles", "2"),),
    ),
    Scenario(
        name="engage_enemy",
        title="Engage enemy",
        smoke_mode=None,
        description="Bot selects an enemy target and emits attack intent.",
        task_ids=("DV-03-T05",),
        budget_seconds=0,
        pending_reason="No landed dedicated-server smoke mode reports enemy acquisition or attack-button counters yet.",
        planned_smoke_mode=20,
        promotion_metrics=(
            "pass",
            "combat_enemy_acquisitions",
            "combat_enemy_visible",
            "combat_enemy_shootable",
            "combat_fire_decisions",
            "action_attack_decisions",
            "action_applied_attack_buttons",
            "combat_damage_events",
            "last_combat_enemy_client",
            "last_combat_damage",
            "route_failures",
        ),
    ),
    Scenario(
        name="switch_weapons",
        title="Switch weapons",
        smoke_mode=None,
        description="Bot evaluates weapon inventory and switches to a preferred weapon.",
        task_ids=("DV-03-T05",),
        budget_seconds=0,
        pending_reason="No landed smoke status reports weapon inventory, weapon choice, or weapon-change commands yet.",
        planned_smoke_mode=21,
        promotion_metrics=(
            "pass",
            "combat_weapon_switch_decisions",
            "action_weapon_switch_decisions",
            "action_pending_weapon_switches",
            "weapon_switch_requests",
            "weapon_switch_completions",
            "weapon_switch_failures",
            "weapon_switch_expected_item",
            "weapon_switch_actual_item",
            "weapon_switch_expected_match",
        ),
    ),
    Scenario(
        name="health_armor_pickup",
        title="Health/armor pickup",
        smoke_mode=None,
        description="Bot prioritizes health or armor after taking damage.",
        task_ids=("DV-03-T05",),
        budget_seconds=0,
        pending_reason="Current item-goal smokes do not yet force damaged armor/health state or verify pickup completion.",
        planned_smoke_mode=22,
        promotion_metrics=(
            "pass",
            "item_low_health_boosts",
            "item_low_armor_boosts",
            "item_health_goal_assignments",
            "item_armor_goal_assignments",
            "item_health_pickups",
            "item_armor_pickups",
            "last_health_pickup_delta",
            "last_armor_pickup_delta",
            "route_failures",
        ),
    ),
    Scenario(
        name="team_objective",
        title="Team objective",
        smoke_mode=None,
        description="Bot chooses and pursues a team objective.",
        task_ids=("DV-03-T05",),
        budget_seconds=0,
        pending_reason="Team policy smoke exists, but no team-objective route/goal completion status is landed yet.",
        planned_smoke_mode=23,
        promotion_metrics=(
            "pass",
            "team_objective_evaluations",
            "team_objective_assignments",
            "team_objective_route_requests",
            "team_objective_route_commands",
            "team_objective_reaches",
            "team_objective_flag_pickups",
            "last_team_objective_type",
            "last_team_objective_client",
            "last_team_objective_item",
            "route_failures",
        ),
    ),
)


def scenario_map() -> dict[str, Scenario]:
    return {scenario.name: scenario for scenario in SCENARIOS}


def utc_timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_status_line(text: str) -> tuple[str | None, dict[str, int]]:
    status_rows: list[tuple[str, dict[str, int]]] = []
    for line in text.splitlines():
        if STATUS_TOKEN_RE.search(line):
            status_line = line.strip()
            status_rows.append((
                status_line,
                {match.group(1): int(match.group(2)) for match in KEY_VALUE_RE.finditer(status_line)},
            ))

    if not status_rows:
        return None, {}

    for status_line, metrics in reversed(status_rows):
        if metrics.get("expected_min_commands", 0) > 0:
            return status_line, metrics
    return status_rows[-1]


def evaluate_check(check: MetricCheck, metrics: dict[str, int]) -> dict[str, Any]:
    actual = metrics.get(check.metric)
    passed = False
    if actual is not None:
        if check.op == "eq":
            passed = actual == check.expected
        elif check.op == "ge":
            passed = actual >= check.expected
        elif check.op == "gt":
            passed = actual > check.expected
        elif check.op == "le":
            passed = actual <= check.expected
        elif check.op == "lt":
            passed = actual < check.expected
        else:
            raise ValueError(f"unknown check operator: {check.op}")

    return {
        "metric": check.metric,
        "op": check.op,
        "expected": check.expected,
        "actual": actual,
        "passed": passed,
        "note": check.note,
    }


def evaluate_marker_check(check: MarkerMetricCheck, marker_metrics: dict[str, list[dict[str, int]]]) -> dict[str, Any]:
    matches = marker_metrics.get(check.marker, [])
    metrics = matches[-1] if matches else {}
    actual = metrics.get(check.metric)
    passed = False
    if actual is not None:
        if check.op == "eq":
            passed = actual == check.expected
        elif check.op == "ge":
            passed = actual >= check.expected
        elif check.op == "gt":
            passed = actual > check.expected
        elif check.op == "le":
            passed = actual <= check.expected
        elif check.op == "lt":
            passed = actual < check.expected
        else:
            raise ValueError(f"unknown marker check operator: {check.op}")

    return {
        "marker": check.marker,
        "metric": check.metric,
        "op": check.op,
        "expected": check.expected,
        "actual": actual,
        "passed": passed,
        "note": check.note,
    }


def parse_marker_metrics(text: str, markers: set[str]) -> dict[str, list[dict[str, int]]]:
    marker_metrics: dict[str, list[dict[str, int]]] = {marker: [] for marker in sorted(markers)}
    if not markers:
        return marker_metrics

    for line in text.splitlines():
        for marker in markers:
            if marker in line:
                marker_metrics[marker].append(
                    {match.group(1): int(match.group(2)) for match in KEY_VALUE_RE.finditer(line)}
                )

    return marker_metrics


def check_catalog(check: MetricCheck) -> dict[str, Any]:
    return {
        "source": STATUS_MARKER,
        "metric": check.metric,
        "op": check.op,
        "expected": check.expected,
        "note": check.note,
    }


def marker_check_catalog(check: MarkerMetricCheck) -> dict[str, Any]:
    return {
        "source": check.marker,
        "metric": check.metric,
        "op": check.op,
        "expected": check.expected,
        "note": check.note,
    }


def marker_metric_catalog(marker_metric: tuple[str, str]) -> dict[str, str]:
    marker, metric = marker_metric
    return {
        "source": marker,
        "metric": metric,
    }


def scenario_catalog(scenario: Scenario) -> dict[str, Any]:
    return {
        "name": scenario.name,
        "title": scenario.title,
        "status": "implemented" if scenario.implemented else "pending",
        "task_ids": list(scenario.task_ids),
        "smoke_mode": scenario.smoke_mode,
        "planned_smoke_mode": scenario.planned_smoke_mode,
        "description": scenario.description,
        "runtime_budget_seconds": scenario.budget_seconds,
        "required_metrics": [check_catalog(check) for check in scenario.checks],
        "required_marker_metrics": [marker_check_catalog(check) for check in scenario.marker_checks],
        "promotion_required_metrics": list(scenario.promotion_metrics),
        "promotion_required_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in scenario.promotion_marker_metrics
        ],
        "extra_cvars": [
            {"name": name, "value": value}
            for name, value in scenario.extra_cvars
        ],
        "pending_blockers": [scenario.pending_reason] if scenario.pending_reason else [],
    }


def catalog_report(scenarios: list[Scenario]) -> dict[str, Any]:
    implemented = sum(1 for scenario in scenarios if scenario.implemented)
    pending = len(scenarios) - implemented
    return {
        "schema_version": 1,
        "generated_utc": utc_timestamp(),
        "summary": {
            "total": len(scenarios),
            "implemented": implemented,
            "pending": pending,
        },
        "scenarios": [scenario_catalog(scenario) for scenario in scenarios],
    }


def load_report(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def markdown_cell(value: Any) -> str:
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\n", " ")


def display_value(value: Any) -> str:
    return "-" if value is None else str(value)


def scenario_artifacts(scenario_result: dict[str, Any]) -> list[str]:
    artifacts: list[str] = []
    for key in ("stdout_path", "stderr_path"):
        path = scenario_result.get(key)
        if path:
            artifacts.append(str(path))
    return artifacts


def marker_summary_metrics(scenario_result: dict[str, Any]) -> dict[str, int]:
    metrics: dict[str, int] = {}
    for marker_rows in scenario_result.get("markers", {}).values():
        if not marker_rows:
            continue
        for key, value in marker_rows[-1].items():
            if key in KEY_METRICS:
                metrics[key] = value
    return metrics


def scenario_key_metrics(scenario_result: dict[str, Any]) -> dict[str, int | float]:
    metrics: dict[str, int | float] = {}
    status_metrics = scenario_result.get("metrics", {})
    for key in KEY_METRICS:
        if key in status_metrics:
            metrics[key] = status_metrics[key]

    metrics.update(marker_summary_metrics(scenario_result))
    duration = scenario_result.get("duration_seconds")
    if isinstance(duration, int | float):
        metrics["duration_seconds"] = duration
    return metrics


def report_scenario_map(report: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        scenario.get("name"): scenario
        for scenario in report.get("scenarios", [])
        if scenario.get("name")
    }


def compare_reports(current: dict[str, Any], previous: dict[str, Any], previous_path: pathlib.Path) -> dict[str, Any]:
    current_scenarios = report_scenario_map(current)
    previous_scenarios = report_scenario_map(previous)
    scenario_names = sorted(set(current_scenarios) | set(previous_scenarios))
    scenario_results: list[dict[str, Any]] = []

    for name in scenario_names:
        current_result = current_scenarios.get(name)
        previous_result = previous_scenarios.get(name)
        current_metrics = scenario_key_metrics(current_result or {})
        previous_metrics = scenario_key_metrics(previous_result or {})
        metric_changes: dict[str, dict[str, Any]] = {}

        for metric in sorted(set(current_metrics) | set(previous_metrics)):
            current_value = current_metrics.get(metric)
            previous_value = previous_metrics.get(metric)
            if current_value == previous_value:
                continue
            delta = None
            if isinstance(current_value, int | float) and isinstance(previous_value, int | float):
                delta = current_value - previous_value
            metric_changes[metric] = {
                "previous": previous_value,
                "current": current_value,
                "delta": delta,
            }

        previous_status = previous_result.get("status") if previous_result else None
        current_status = current_result.get("status") if current_result else None
        scenario_results.append({
            "name": name,
            "previous_status": previous_status,
            "current_status": current_status,
            "added": previous_result is None,
            "removed": current_result is None,
            "status_changed": previous_status != current_status,
            "metric_changes": metric_changes,
        })

    summary = {
        "total": len(scenario_results),
        "matched": sum(1 for item in scenario_results if not item["added"] and not item["removed"]),
        "added": sum(1 for item in scenario_results if item["added"]),
        "removed": sum(1 for item in scenario_results if item["removed"]),
        "status_changed": sum(1 for item in scenario_results if item["status_changed"]),
        "metric_changed": sum(1 for item in scenario_results if item["metric_changes"]),
    }

    return {
        "previous_path": str(previous_path),
        "summary": summary,
        "scenarios": scenario_results,
    }


def attach_comparison(report: dict[str, Any], previous_path: pathlib.Path | None) -> None:
    if previous_path is None:
        return
    previous = load_report(previous_path)
    report["comparison"] = compare_reports(report, previous, previous_path)


def scenario_marker_metric_pairs(scenario_result: dict[str, Any]) -> set[tuple[str, str]]:
    pairs: set[tuple[str, str]] = set()
    for marker, rows in scenario_result.get("markers", {}).items():
        for row in rows:
            for metric in row:
                pairs.add((marker, metric))
    return pairs


def pending_gap_scenario(scenario: Scenario, fixture_result: dict[str, Any] | None) -> dict[str, Any]:
    present_metrics: set[str] = set()
    present_marker_metrics: set[tuple[str, str]] = set()
    fixture_status = None
    fixture_smoke_mode = None
    blockers: list[str] = []

    if fixture_result is None:
        blockers.append(f"fixture report has no scenario row named {scenario.name}")
    else:
        fixture_status = fixture_result.get("status")
        fixture_smoke_mode = fixture_result.get("smoke_mode")
        present_metrics = set(fixture_result.get("metrics", {}))
        present_marker_metrics = scenario_marker_metric_pairs(fixture_result)

        if fixture_status == "pending":
            blockers.append("fixture row is still pending, not source-backed")
        if scenario.planned_smoke_mode is not None and fixture_smoke_mode != scenario.planned_smoke_mode:
            blockers.append(
                f"fixture smoke_mode is {display_value(fixture_smoke_mode)}, "
                f"expected {scenario.planned_smoke_mode}"
            )

    missing_metrics = [
        metric
        for metric in scenario.promotion_metrics
        if metric not in present_metrics
    ]
    missing_marker_metrics = [
        marker_metric
        for marker_metric in scenario.promotion_marker_metrics
        if marker_metric not in present_marker_metrics
    ]

    if missing_metrics:
        blockers.append(f"missing status metrics: {', '.join(missing_metrics)}")
    if missing_marker_metrics:
        blockers.append(
            "missing marker metrics: "
            + ", ".join(f"{marker}::{metric}" for marker, metric in missing_marker_metrics)
        )

    return {
        "name": scenario.name,
        "title": scenario.title,
        "status": "blocked" if blockers else "ready",
        "task_ids": list(scenario.task_ids),
        "smoke_mode": None,
        "planned_smoke_mode": scenario.planned_smoke_mode,
        "description": scenario.description,
        "pending_reason": scenario.pending_reason,
        "fixture_status": fixture_status,
        "fixture_smoke_mode": fixture_smoke_mode,
        "promotion_required_metrics": list(scenario.promotion_metrics),
        "promotion_required_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in scenario.promotion_marker_metrics
        ],
        "present_metrics": sorted(metric for metric in scenario.promotion_metrics if metric in present_metrics),
        "missing_metrics": missing_metrics,
        "present_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in scenario.promotion_marker_metrics
            if marker_metric in present_marker_metrics
        ],
        "missing_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in missing_marker_metrics
        ],
        "blockers": blockers,
    }


def pending_gap_report(
    scenarios: list[Scenario],
    fixture_report: dict[str, Any],
    fixture_path: pathlib.Path,
) -> dict[str, Any]:
    pending_scenarios = [scenario for scenario in scenarios if not scenario.implemented]
    fixture_scenarios = report_scenario_map(fixture_report)
    gap_rows = [
        pending_gap_scenario(scenario, fixture_scenarios.get(scenario.name))
        for scenario in pending_scenarios
    ]
    summary = {
        "total": len(gap_rows),
        "ready": sum(1 for row in gap_rows if row["status"] == "ready"),
        "blocked": sum(1 for row in gap_rows if row["status"] == "blocked"),
        "missing_rows": sum(1 for row in gap_rows if row["fixture_status"] is None),
        "pending_rows": sum(1 for row in gap_rows if row["fixture_status"] == "pending"),
        "missing_status_metrics": sum(len(row["missing_metrics"]) for row in gap_rows),
        "missing_marker_metrics": sum(len(row["missing_marker_metrics"]) for row in gap_rows),
        "overall": "ready" if gap_rows and all(row["status"] == "ready" for row in gap_rows) else "blocked",
    }
    return {
        "schema_version": 1,
        "report_type": "pending_gap",
        "generated_utc": utc_timestamp(),
        "fixture_path": str(fixture_path),
        "fixture_summary": fixture_report.get("summary", {}),
        "summary": summary,
        "scenarios": gap_rows,
    }


def scenario_metric_text(scenario_result: dict[str, Any]) -> str:
    metrics = scenario_key_metrics(scenario_result)
    if not metrics:
        return ""
    return ", ".join(f"{key}={metrics[key]}" for key in KEY_METRICS if key in metrics)


def scenario_pending_text(scenario_result: dict[str, Any]) -> str:
    if scenario_result.get("pending_reason"):
        return scenario_result["pending_reason"]
    blockers = scenario_result.get("pending_blockers", [])
    return "; ".join(blockers)


def build_pending_gap_markdown_report(report: dict[str, Any]) -> str:
    lines: list[str] = [
        "# Bot Scenario Pending Gap Report",
        "",
        f"- Generated UTC: `{report.get('generated_utc', '')}`",
        f"- Fixture: `{report.get('fixture_path', '')}`",
    ]
    summary = report.get("summary", {})
    if summary:
        summary_text = ", ".join(f"{key}={value}" for key, value in summary.items())
        lines.append(f"- Summary: `{summary_text}`")
    lines.extend((
        "",
        "## Scenarios",
        "",
        "| Scenario | Status | Planned Smoke | Fixture Status | Missing Metrics | Blockers |",
        "| --- | --- | --- | --- | --- | --- |",
    ))

    for scenario in report.get("scenarios", []):
        missing = ", ".join(scenario.get("missing_metrics", []))
        marker_missing = [
            f"{item['source']}::{item['metric']}"
            for item in scenario.get("missing_marker_metrics", [])
        ]
        if marker_missing:
            missing = "; ".join(part for part in (missing, ", ".join(marker_missing)) if part)
        blockers = "; ".join(scenario.get("blockers", []))
        lines.append(
            "| {name} | {status} | {planned} | {fixture_status} | {missing} | {blockers} |".format(
                name=markdown_cell(scenario.get("name", "")),
                status=markdown_cell(scenario.get("status", "")),
                planned=markdown_cell(scenario.get("planned_smoke_mode")),
                fixture_status=markdown_cell(scenario.get("fixture_status")),
                missing=markdown_cell(missing),
                blockers=markdown_cell(blockers),
            )
        )

    lines.append("")
    return "\n".join(lines)


def build_markdown_report(report: dict[str, Any]) -> str:
    if report.get("report_type") == "pending_gap":
        return build_pending_gap_markdown_report(report)

    lines: list[str] = []
    is_catalog = "generated_utc" in report and "started_utc" not in report
    title = "Bot Scenario Catalog" if is_catalog else "Bot Scenario Smoke Report"
    lines.append(f"# {title}")
    lines.append("")
    if report.get("repo_root"):
        lines.append(f"- Repo: `{report['repo_root']}`")
    if report.get("started_utc"):
        lines.append(f"- Started UTC: `{report['started_utc']}`")
    if report.get("generated_utc"):
        lines.append(f"- Generated UTC: `{report['generated_utc']}`")
    if report.get("artifact_dir"):
        lines.append(f"- Artifact dir: `{report['artifact_dir']}`")

    summary = report.get("summary", {})
    if summary:
        summary_text = ", ".join(f"{key}={value}" for key, value in summary.items())
        lines.append(f"- Summary: `{summary_text}`")
    lines.append("")

    lines.append("## Scenarios")
    lines.append("")
    lines.append("| Scenario | Status | Smoke | Tasks | Key Metrics | Pending Blockers | Artifacts |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- |")
    for scenario in report.get("scenarios", []):
        tasks = ",".join(scenario.get("task_ids", []))
        smoke = scenario.get("smoke_mode")
        artifacts = "<br>".join(f"`{artifact}`" for artifact in scenario_artifacts(scenario))
        lines.append(
            "| {name} | {status} | {smoke} | {tasks} | {metrics} | {pending} | {artifacts} |".format(
                name=markdown_cell(scenario.get("name", "")),
                status=markdown_cell(scenario.get("status", "")),
                smoke=markdown_cell(smoke if smoke is not None else "-"),
                tasks=markdown_cell(tasks),
                metrics=markdown_cell(scenario_metric_text(scenario)),
                pending=markdown_cell(scenario_pending_text(scenario)),
                artifacts=artifacts,
            )
        )

    comparison = report.get("comparison")
    if comparison:
        lines.append("")
        lines.append("## Comparison")
        lines.append("")
        lines.append(f"- Previous report: `{comparison['previous_path']}`")
        comparison_summary = ", ".join(
            f"{key}={value}" for key, value in comparison.get("summary", {}).items()
        )
        lines.append(f"- Summary: `{comparison_summary}`")
        lines.append("")
        lines.append("| Scenario | Previous | Current | Status Changed | Metric Changes |")
        lines.append("| --- | --- | --- | --- | --- |")
        for scenario in comparison.get("scenarios", []):
            changes = []
            for metric, change in scenario.get("metric_changes", {}).items():
                delta = change.get("delta")
                if delta is None:
                    changes.append(
                        f"{metric}: {display_value(change.get('previous'))} -> "
                        f"{display_value(change.get('current'))}"
                    )
                else:
                    changes.append(
                        f"{metric}: {display_value(change.get('previous'))} -> "
                        f"{display_value(change.get('current'))} ({delta:+})"
                    )
            lines.append(
                "| {name} | {previous} | {current} | {status_changed} | {changes} |".format(
                    name=markdown_cell(scenario.get("name", "")),
                    previous=markdown_cell(scenario.get("previous_status")),
                    current=markdown_cell(scenario.get("current_status")),
                    status_changed=markdown_cell(scenario.get("status_changed")),
                    changes=markdown_cell("; ".join(changes)),
                )
            )

    lines.append("")
    return "\n".join(lines)


def write_report_outputs(
    report: dict[str, Any],
    repo_root: pathlib.Path,
    json_out: str | None,
    markdown_out: str | None,
) -> None:
    if json_out:
        json_path = resolve_path(repo_root, json_out)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if markdown_out:
        markdown_path = resolve_path(repo_root, markdown_out)
        markdown_path.parent.mkdir(parents=True, exist_ok=True)
        markdown_path.write_text(build_markdown_report(report), encoding="utf-8")


def resolve_path(root: pathlib.Path, value: str) -> pathlib.Path:
    path = pathlib.Path(value)
    if not path.is_absolute():
        path = root / path
    return path.resolve()


def build_command(
    binary: pathlib.Path,
    install_dir: pathlib.Path,
    scenario: Scenario,
    game: str,
    map_name: str,
    port: int,
    log_name: str,
) -> list[str]:
    command = [
        str(binary),
        "+set",
        "game",
        game,
        "+set",
        "basedir",
        str(install_dir),
        "+set",
        "net_port",
        str(port),
        "+set",
        "logfile",
        "1",
        "+set",
        "logfile_name",
        log_name,
        "+set",
        "logfile_flush",
        "1",
        "+set",
        "developer",
        "1",
        "+set",
        "deathmatch",
        "1",
        "+set",
        "sg_bot_enable",
        "1",
        "+set",
        "sg_bot_debug_route",
        "1",
        "+set",
        "sg_bot_debug_goal",
        "1",
    ]

    for name, value in scenario.extra_cvars:
        command.extend(("+set", name, value))

    command.extend((
        "+set",
        "sv_bot_frame_command_smoke",
        str(scenario.smoke_mode),
        "+map",
        map_name,
    ))
    return command


def run_implemented_scenario(
    root: pathlib.Path,
    binary: pathlib.Path,
    install_dir: pathlib.Path,
    scenario: Scenario,
    artifact_dir: pathlib.Path,
    game: str,
    map_name: str,
    port: int,
    timeout: int,
) -> dict[str, Any]:
    started = time.monotonic()
    log_stem = f"bot_scenario_{scenario.name}_{utc_timestamp()}"
    stdout_path = artifact_dir / f"{scenario.name}.stdout.txt"
    stderr_path = artifact_dir / f"{scenario.name}.stderr.txt"
    command = build_command(binary, install_dir, scenario, game, map_name, port, log_stem)

    result: dict[str, Any] = {
        "name": scenario.name,
        "title": scenario.title,
        "status": "error",
        "implemented": True,
        "smoke_mode": scenario.smoke_mode,
        "task_ids": list(scenario.task_ids),
        "description": scenario.description,
        "runtime_budget_seconds": scenario.budget_seconds,
        "port": port,
        "command": command,
        "stdout_path": str(stdout_path),
        "stderr_path": str(stderr_path),
        "required_metrics": [check_catalog(check) for check in scenario.checks],
        "required_marker_metrics": [marker_check_catalog(check) for check in scenario.marker_checks],
        "checks": [],
        "marker_checks": [],
        "markers": {},
        "metrics": {},
        "status_line": None,
        "returncode": None,
        "duration_seconds": None,
        "duration_budget_passed": None,
        "failures": [],
    }

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, \
            stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
        process = subprocess.Popen(
            command,
            cwd=root,
            stdout=stdout_file,
            stderr=stderr_file,
            text=True,
            creationflags=creationflags,
        )
        try:
            result["returncode"] = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            result["returncode"] = process.wait(timeout=10)
            result["duration_seconds"] = round(time.monotonic() - started, 3)
            result["duration_budget_passed"] = False
            result["status"] = "timeout"
            result["failures"].append(f"timed out after {timeout} seconds")
            return result

    result["duration_seconds"] = round(time.monotonic() - started, 3)
    result["duration_budget_passed"] = (
        scenario.budget_seconds <= 0 or result["duration_seconds"] <= scenario.budget_seconds
    )
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    combined_text = stdout_text + "\n" + stderr_text
    status_line, metrics = parse_status_line(combined_text)
    marker_metrics = parse_marker_metrics(
        combined_text,
        {check.marker for check in scenario.marker_checks},
    )
    result["status_line"] = status_line
    result["metrics"] = metrics
    result["markers"] = marker_metrics

    if status_line is None:
        result["status"] = "failed"
        result["failures"].append(f"missing {STATUS_MARKER} line")
        return result

    check_results = [evaluate_check(check, metrics) for check in scenario.checks]
    marker_check_results = [
        evaluate_marker_check(check, marker_metrics)
        for check in scenario.marker_checks
    ]
    result["checks"] = check_results
    result["marker_checks"] = marker_check_results
    result["failures"].extend(
        f"{check['metric']} {check['op']} {check['expected']} failed, actual={check['actual']}"
        for check in check_results
        if not check["passed"]
    )
    result["failures"].extend(
        f"{check['marker']} {check['metric']} {check['op']} {check['expected']} "
        f"failed, actual={check['actual']}"
        for check in marker_check_results
        if not check["passed"]
    )

    forbidden_hits = [
        pattern
        for pattern in FORBIDDEN_PATTERNS
        if pattern in stdout_text or pattern in stderr_text
    ]
    if forbidden_hits:
        result["failures"].extend(f"forbidden output matched: {pattern}" for pattern in forbidden_hits)

    result["status"] = "passed" if not result["failures"] else "failed"
    return result


def pending_result(scenario: Scenario) -> dict[str, Any]:
    return {
        "name": scenario.name,
        "title": scenario.title,
        "status": "pending",
        "implemented": False,
        "smoke_mode": None,
        "planned_smoke_mode": scenario.planned_smoke_mode,
        "task_ids": list(scenario.task_ids),
        "description": scenario.description,
        "runtime_budget_seconds": scenario.budget_seconds,
        "pending_reason": scenario.pending_reason,
        "required_metrics": [check_catalog(check) for check in scenario.checks],
        "required_marker_metrics": [marker_check_catalog(check) for check in scenario.marker_checks],
        "promotion_required_metrics": list(scenario.promotion_metrics),
        "promotion_required_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in scenario.promotion_marker_metrics
        ],
        "checks": [],
        "marker_checks": [],
        "markers": {},
        "metrics": {},
        "failures": [],
    }


def select_scenarios(tokens: list[str]) -> list[Scenario]:
    names = scenario_map()
    expanded: list[str] = []

    if not tokens:
        tokens = ["all"]

    for token in tokens:
        for part in token.split(","):
            part = part.strip()
            if part:
                expanded.append(part)

    selected: list[Scenario] = []
    seen: set[str] = set()
    for token in expanded:
        if token == "all":
            candidates = list(SCENARIOS)
        elif token == "implemented":
            candidates = [scenario for scenario in SCENARIOS if scenario.implemented]
        elif token == "pending":
            candidates = [scenario for scenario in SCENARIOS if not scenario.implemented]
        elif token in names:
            candidates = [names[token]]
        else:
            choices = ", ".join(sorted([*names.keys(), "all", "implemented", "pending"]))
            raise SystemExit(f"Unknown scenario '{token}'. Choices: {choices}")

        for scenario in candidates:
            if scenario.name not in seen:
                selected.append(scenario)
                seen.add(scenario.name)

    return selected


def summarize(results: list[dict[str, Any]]) -> dict[str, Any]:
    counts = {
        "passed": 0,
        "failed": 0,
        "timeout": 0,
        "error": 0,
        "pending": 0,
    }
    for result in results:
        status = result["status"]
        counts[status] = counts.get(status, 0) + 1

    blocking = counts["failed"] + counts["timeout"] + counts["error"]
    return {
        "total": len(results),
        **counts,
        "overall": "pass" if blocking == 0 else "fail",
    }


def print_text_report(report: dict[str, Any]) -> None:
    summary = report["summary"]
    print("Bot scenario smoke summary")
    print(f"Repo: {report['repo_root']}")
    print(f"Binary: {report['binary']}")
    print(f"Install: {report['install_dir']}")
    print(f"Artifacts: {report['artifact_dir']}")
    print(
        "Overall: {overall} ({passed} passed, {failed} failed, {timeout} timeout, "
        "{error} error, {pending} pending)".format(**summary)
    )
    print("")

    for result in report["scenarios"]:
        status = result["status"].upper()
        mode = result.get("smoke_mode")
        mode_text = f"mode={mode}" if mode is not None else "mode=pending"
        print(f"[{status}] {result['name']} ({mode_text}) - {result['title']}")
        if result["status"] == "passed":
            metrics = result.get("metrics", {})
            interesting = [
                "frames",
                "commands",
                "route_commands",
                "route_failures",
                "stuck_detections",
                "recovery_command_uses",
                "item_goal_assignments",
                "item_goal_peak_active_reservations",
                "pass",
            ]
            parts = [f"{key}={metrics[key]}" for key in interesting if key in metrics]
            for marker, marker_rows in result.get("markers", {}).items():
                if not marker_rows:
                    continue
                marker_metrics = marker_rows[-1]
                for key in ("cycles", "map_changes", "final_count"):
                    if key in marker_metrics:
                        parts.append(f"{key}={marker_metrics[key]}")
            if parts:
                print(f"  metrics: {' '.join(parts)}")
            budget = result.get("runtime_budget_seconds", 0)
            if budget:
                duration = result.get("duration_seconds")
                budget_passed = result.get("duration_budget_passed")
                print(f"  runtime: {duration}s budget={budget}s budget_passed={budget_passed}")
        elif result["status"] == "pending":
            print(f"  pending: {result['pending_reason']}")
        else:
            for failure in result.get("failures", []):
                print(f"  failure: {failure}")
            if result.get("stdout_path"):
                print(f"  stdout: {result['stdout_path']}")
            if result.get("stderr_path"):
                print(f"  stderr: {result['stderr_path']}")


def print_catalog_report(report: dict[str, Any]) -> None:
    summary = report["summary"]
    print("Bot scenario catalog")
    print(
        "Scenarios: {total} ({implemented} implemented, {pending} pending)".format(**summary)
    )
    print("")
    for scenario in report["scenarios"]:
        status = scenario["status"]
        mode = scenario["smoke_mode"] if scenario["smoke_mode"] is not None else "-"
        task_ids = ",".join(scenario["task_ids"])
        print(f"[{status.upper()}] {scenario['name']} mode={mode} tasks={task_ids}")
        print(f"  budget_seconds: {scenario['runtime_budget_seconds']}")
        if scenario["planned_smoke_mode"] is not None:
            print(f"  planned_smoke_mode: {scenario['planned_smoke_mode']}")
        if scenario["required_metrics"]:
            metrics = [
                f"{check['metric']} {check['op']} {check['expected']}"
                for check in scenario["required_metrics"]
            ]
            print(f"  required_metrics: {'; '.join(metrics)}")
        if scenario["required_marker_metrics"]:
            marker_metrics = [
                f"{check['source']}::{check['metric']} {check['op']} {check['expected']}"
                for check in scenario["required_marker_metrics"]
            ]
            print(f"  required_marker_metrics: {'; '.join(marker_metrics)}")
        if scenario["promotion_required_metrics"]:
            print(f"  promotion_required_metrics: {', '.join(scenario['promotion_required_metrics'])}")
        if scenario["promotion_required_marker_metrics"]:
            marker_metrics = [
                f"{check['source']}::{check['metric']}"
                for check in scenario["promotion_required_marker_metrics"]
            ]
            print(f"  promotion_required_marker_metrics: {', '.join(marker_metrics)}")
        for blocker in scenario["pending_blockers"]:
            print(f"  pending: {blocker}")


def print_pending_gap_report(report: dict[str, Any]) -> None:
    summary = report["summary"]
    print("Bot scenario pending gap report")
    print(f"Fixture: {report['fixture_path']}")
    print(
        "Scenarios: {total} ({ready} ready, {blocked} blocked, {missing_rows} missing rows, "
        "{pending_rows} pending rows, {missing_status_metrics} missing status metrics, "
        "{missing_marker_metrics} missing marker metrics)".format(**summary)
    )
    print(f"Overall: {summary['overall']}")
    print("")

    for scenario in report["scenarios"]:
        planned = scenario["planned_smoke_mode"] if scenario["planned_smoke_mode"] is not None else "-"
        fixture_status = scenario["fixture_status"] if scenario["fixture_status"] is not None else "missing"
        fixture_mode = scenario["fixture_smoke_mode"] if scenario["fixture_smoke_mode"] is not None else "-"
        print(
            f"[{scenario['status'].upper()}] {scenario['name']} "
            f"planned_mode={planned} fixture_status={fixture_status} fixture_mode={fixture_mode}"
        )
        if scenario["missing_metrics"]:
            print(f"  missing_metrics: {', '.join(scenario['missing_metrics'])}")
        if scenario["missing_marker_metrics"]:
            missing_marker_metrics = [
                f"{item['source']}::{item['metric']}"
                for item in scenario["missing_marker_metrics"]
            ]
            print(f"  missing_marker_metrics: {', '.join(missing_marker_metrics)}")
        for blocker in scenario["blockers"]:
            print(f"  blocker: {blocker}")


def list_scenarios() -> None:
    for scenario in SCENARIOS:
        status = "implemented" if scenario.implemented else "pending"
        mode = scenario.smoke_mode if scenario.smoke_mode is not None else "-"
        print(f"{scenario.name:28} {status:11} mode={mode}  {scenario.title}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run WORR Q3A BotLib scenario smokes through dedicated-server smoke modes."
    )
    parser.add_argument("--repo-root", default=".", help="WORR repository root")
    parser.add_argument("--binary", default=".install/worr_ded_x86_64.exe", help="Dedicated server binary")
    parser.add_argument("--install-dir", default=".install", help="Prepared install root")
    parser.add_argument("--game", default="basew", help="Game directory to launch")
    parser.add_argument("--map", default="mm-rage", help="Map used by scenario smoke modes")
    parser.add_argument("--scenario", action="append", help="Scenario name, comma list, or all/implemented/pending")
    parser.add_argument("--timeout", type=int, default=60, help="Per-scenario timeout in seconds")
    parser.add_argument("--base-port", type=int, default=27970, help="First net_port to use")
    parser.add_argument("--artifact-dir", default=".tmp/bot_scenarios", help="Output directory for stdout/stderr")
    parser.add_argument("--format", choices=("text", "json", "both"), default="text", help="Console output format")
    parser.add_argument("--json-out", help="Optional machine-readable JSON report path")
    parser.add_argument("--markdown-out", help="Optional Markdown scenario report path")
    parser.add_argument("--compare", help="Compare this report with one previous JSON report")
    parser.add_argument(
        "--pending-gap-report",
        help="Analyze one JSON report for pending scenario promotion gaps and exit without launching the game",
    )
    parser.add_argument("--fail-on-pending", action="store_true", help="Treat pending placeholders as suite failures")
    parser.add_argument("--catalog", action="store_true", help="Emit the declarative scenario catalog and exit")
    parser.add_argument("--list", action="store_true", help="List known scenarios and exit")
    args = parser.parse_args()

    if args.list:
        list_scenarios()
        return 0

    started_utc = utc_timestamp()
    repo_root = pathlib.Path(args.repo_root).resolve()
    if not repo_root.is_dir():
        raise SystemExit(f"Repository root not found: {repo_root}")

    selected = select_scenarios(args.scenario or [])

    if args.catalog:
        report = catalog_report(selected)
        report["repo_root"] = str(repo_root)
        compare_path = resolve_path(repo_root, args.compare) if args.compare else None
        attach_comparison(report, compare_path)
        write_report_outputs(report, repo_root, args.json_out, args.markdown_out)
        if args.format in ("text", "both"):
            print_catalog_report(report)
        if args.format in ("json", "both"):
            print(json.dumps(report, indent=2))
        return 0

    if args.pending_gap_report:
        gap_path = resolve_path(repo_root, args.pending_gap_report)
        if not gap_path.is_file():
            raise SystemExit(f"Pending gap fixture report not found: {gap_path}")
        fixture_report = load_report(gap_path)
        report = pending_gap_report(selected, fixture_report, gap_path)
        report["repo_root"] = str(repo_root)
        write_report_outputs(report, repo_root, args.json_out, args.markdown_out)
        if args.format in ("text", "both"):
            print_pending_gap_report(report)
        if args.format in ("json", "both"):
            print(json.dumps(report, indent=2))
        return 0

    binary = resolve_path(repo_root, args.binary)
    install_dir = resolve_path(repo_root, args.install_dir)
    if not binary.is_file():
        raise SystemExit(f"Dedicated server binary not found: {binary}")
    if not install_dir.is_dir():
        raise SystemExit(f"Install dir not found: {install_dir}")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")

    artifact_root = resolve_path(repo_root, args.artifact_dir)
    artifact_dir = artifact_root / started_utc
    artifact_dir.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, Any]] = []
    run_index = 0
    for scenario in selected:
        if not scenario.implemented:
            results.append(pending_result(scenario))
            continue

        port = args.base_port + run_index
        run_index += 1
        results.append(
            run_implemented_scenario(
                repo_root,
                binary,
                install_dir,
                scenario,
                artifact_dir,
                args.game,
                args.map,
                port,
                args.timeout,
            )
        )

    summary = summarize(results)
    if args.fail_on_pending and summary["pending"] > 0 and summary["overall"] == "pass":
        summary["overall"] = "fail"

    report = {
        "schema_version": 1,
        "started_utc": started_utc,
        "repo_root": str(repo_root),
        "binary": str(binary),
        "install_dir": str(install_dir),
        "artifact_dir": str(artifact_dir),
        "map": args.map,
        "game": args.game,
        "timeout_seconds": args.timeout,
        "catalog": [scenario_catalog(scenario) for scenario in selected],
        "summary": summary,
        "scenarios": results,
    }

    compare_path = resolve_path(repo_root, args.compare) if args.compare else None
    attach_comparison(report, compare_path)
    write_report_outputs(report, repo_root, args.json_out, args.markdown_out)

    if args.format in ("text", "both"):
        print_text_report(report)
    if args.format in ("json", "both"):
        print(json.dumps(report, indent=2))

    return 0 if summary["overall"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
