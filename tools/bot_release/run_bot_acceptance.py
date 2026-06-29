#!/usr/bin/env python3
"""Run a release-readiness acceptance audit for WORR bots."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass, field
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS_ROOT = REPO_ROOT / "tools"

sys.path.insert(0, str(TOOLS_ROOT))
sys.path.insert(0, str(TOOLS_ROOT / "bot_playtest"))
sys.path.insert(0, str(TOOLS_ROOT / "bot_perf"))
sys.path.insert(0, str(TOOLS_ROOT / "bot_profiles"))
sys.path.insert(0, str(TOOLS_ROOT / "bot_surface"))

import package_assets  # noqa: E402
import generate_bot_playtest  # noqa: E402
import analyze_bot_perf  # noqa: E402
import triage_bot_playtest  # noqa: E402
import validate_bot_profiles  # noqa: E402
import audit_bot_surface  # noqa: E402


REQUIRED_BOT_PROFILES = ("bulwark", "relay", "smoke", "vanguard", "vector")
REQUIRED_AAS_MAPS = (
    "mm-rage",
    "q2dm1",
    "q2dm2",
    "q2dm8",
    "q2ctf1",
    "base1",
    "base2",
    "train",
)
REQUIRED_USER_DOCS = (
    "docs-user/bots.md",
    "docs-user/bot-cvars.md",
    "docs-user/bot-profiles.md",
    "docs-user/bot-map-readiness.md",
    "docs-user/bot-playtest.md",
)
REQUIRED_PLAYTEST_MODES = ("FFA", "Duel", "TDM", "CTF")
REQUIRED_PERF_BUDGETS = (
    "default_soak_budget.json",
    "source_counter_soak_budget.json",
)
REQUIRED_VARIANCE_BUDGETS = ("source_counter_variance_budget.json",)
REQUIRED_SCENARIOS = (
    "spawn_route_to_item",
    "behavior_arbitration",
    "combat_survival_regression",
    "ffa_live_pacing",
    "duel_live_pacing",
    "ctf_objective_transitions",
    "coop_campaign_interaction_matrix",
    "movement_hazard_context_gap",
)


@dataclass
class CheckResult:
    name: str
    status: str
    message: str
    metrics: dict[str, Any] = field(default_factory=dict)
    failures: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    artifacts: dict[str, str] = field(default_factory=dict)

    def to_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "status": self.status,
            "message": self.message,
            "metrics": self.metrics,
            "failures": self.failures,
            "warnings": self.warnings,
            "artifacts": self.artifacts,
        }


def ok(
    name: str,
    message: str,
    *,
    metrics: dict[str, Any] | None = None,
    artifacts: dict[str, str] | None = None,
    warnings: list[str] | None = None,
) -> CheckResult:
    return CheckResult(
        name=name,
        status="pass",
        message=message,
        metrics=metrics or {},
        artifacts=artifacts or {},
        warnings=warnings or [],
    )


def fail(
    name: str,
    message: str,
    failures: list[str],
    *,
    metrics: dict[str, Any] | None = None,
    artifacts: dict[str, str] | None = None,
    warnings: list[str] | None = None,
) -> CheckResult:
    return CheckResult(
        name=name,
        status="fail",
        message=message,
        failures=failures,
        metrics=metrics or {},
        artifacts=artifacts or {},
        warnings=warnings or [],
    )


def warn(
    name: str,
    message: str,
    warnings: list[str],
    *,
    metrics: dict[str, Any] | None = None,
    artifacts: dict[str, str] | None = None,
) -> CheckResult:
    return CheckResult(
        name=name,
        status="warn",
        message=message,
        warnings=warnings,
        metrics=metrics or {},
        artifacts=artifacts or {},
    )


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: pathlib.Path, repo_root: pathlib.Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def bots_txt_entries(path: pathlib.Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    entries: dict[str, str] = {}
    current_name: str | None = None
    for raw_line in text.splitlines():
        parts = raw_line.strip().split()
        if len(parts) < 2:
            continue
        key, value = parts[0].lower(), parts[1]
        if key == "name":
            current_name = value.lower()
            entries.setdefault(current_name, "")
        elif key == "aifile" and current_name:
            entries[current_name] = value.replace("\\", "/")
    return entries


def check_surface(repo_root: pathlib.Path) -> CheckResult:
    result = audit_bot_surface.audit_repo(repo_root)
    payload = result.to_json()
    summary = payload["summary"]
    metrics = {
        "cvars": summary["cvars"],
        "commands": summary["commands"],
        "public_cvars": summary["classifications"].get("public", 0),
        "smoke_only_cvars": summary["classifications"].get("smoke-only", 0),
        "violations": summary["violations"],
        "warnings": summary["warnings"],
    }
    if result.violations:
        return fail(
            "public_surface",
            "public bot surface audit failed",
            result.violations,
            metrics=metrics,
        )
    return ok(
        "public_surface",
        "public bot cvars and Q3-style commands are clean",
        metrics=metrics,
        warnings=result.warnings,
    )


def check_profiles(repo_root: pathlib.Path) -> CheckResult:
    report = validate_bot_profiles.validate_paths(
        [str(repo_root / "assets" / "botfiles" / "bots")],
        validate_bot_profiles.ValidationOptions(
            allow_unknown=False,
            fail_on_empty=True,
            check_companions=True,
        ),
        cwd=repo_root,
    )
    summary = report["summary"]
    profile_ids = sorted(profile["id"] for profile in report.get("profiles", []))
    missing = sorted(set(REQUIRED_BOT_PROFILES) - set(profile_ids))
    errors = [
        validate_bot_profiles.format_issue(issue)
        for issue in report.get("issues", [])
        if issue.get("severity") == "error"
    ]
    warnings = [
        validate_bot_profiles.format_issue(issue)
        for issue in report.get("issues", [])
        if issue.get("severity") == "warning"
    ]
    failures = errors + [f"missing required first-party bot profile: {name}" for name in missing]
    metrics = {
        "files": summary["files"],
        "profiles": summary["profiles"],
        "errors": summary["errors"],
        "warnings": summary["warnings"],
        "required_profiles": len(REQUIRED_BOT_PROFILES),
    }
    if failures:
        return fail(
            "profile_pack",
            "bot profile validation failed",
            failures,
            metrics=metrics,
            warnings=warnings,
        )
    return ok(
        "profile_pack",
        "first-party bot profiles validate and cover min-player rotation",
        metrics=metrics,
        warnings=warnings,
    )


def check_bots_txt(repo_root: pathlib.Path) -> CheckResult:
    bots_txt = repo_root / "assets" / "botfiles" / "bots.txt"
    if not bots_txt.is_file():
        return fail("bots_txt", "bots.txt is missing", [f"missing {bots_txt}"])

    entries = bots_txt_entries(bots_txt)
    failures: list[str] = []
    for name in REQUIRED_BOT_PROFILES:
        aifile = entries.get(name)
        expected = f"bots/{name}_c.c"
        if aifile != expected:
            failures.append(
                f"bots.txt entry {name!r} should reference {expected!r}; found {aifile!r}"
            )

    metrics = {
        "entries": len(entries),
        "required_entries": len(REQUIRED_BOT_PROFILES),
    }
    if failures:
        return fail(
            "bots_txt",
            "bots.txt does not expose the first-party character roster",
            failures,
            metrics=metrics,
            artifacts={"bots_txt": rel(bots_txt, repo_root)},
        )
    return ok(
        "bots_txt",
        "bots.txt exposes the first-party character roster",
        metrics=metrics,
        artifacts={"bots_txt": rel(bots_txt, repo_root)},
    )


def check_authored_botfiles(repo_root: pathlib.Path) -> CheckResult:
    assets_dir = repo_root / "assets"
    try:
        members = package_assets.botfile_release_members(assets_dir)
    except SystemExit as exc:
        return fail(
            "authored_botfiles",
            "authored botfile release payload is invalid",
            [str(exc)],
        )

    failures: list[str] = []
    member_set = set(members)
    for name in REQUIRED_BOT_PROFILES:
        for suffix in package_assets.BOTFILE_PROFILE_SUFFIXES:
            member = f"botfiles/bots/{name}{suffix}"
            if member not in member_set:
                failures.append(f"missing authored botfile member: {member}")
        script_member = f"botfiles/scripts/{name}{package_assets.BOTFILE_SCRIPT_SUFFIX}"
        if script_member not in member_set:
            failures.append(f"missing authored bot script member: {script_member}")

    metrics = {
        "members": len(members),
        "required_profiles": len(REQUIRED_BOT_PROFILES),
    }
    if failures:
        return fail(
            "authored_botfiles",
            "authored botfile release payload is incomplete",
            failures,
            metrics=metrics,
        )
    return ok(
        "authored_botfiles",
        "authored botfile release payload is complete",
        metrics=metrics,
    )


def check_staged_botfiles(repo_root: pathlib.Path, install_dir: pathlib.Path, base_game: str) -> CheckResult:
    assets_dir = repo_root / "assets"
    output_dir = install_dir / base_game
    archive_path = output_dir / "pak0.pkz"

    try:
        members = package_assets.botfile_release_members(assets_dir)
        package_assets.validate_botfile_payload(assets_dir, output_dir, archive_path, members)
    except SystemExit as exc:
        return fail(
            "staged_botfiles",
            "staged package/loose botfiles are invalid",
            [str(exc)],
            artifacts={
                "install_dir": rel(install_dir, repo_root),
                "archive": rel(archive_path, repo_root),
            },
        )

    metrics = {
        "members": len(members),
        "archive_bytes": archive_path.stat().st_size if archive_path.exists() else 0,
    }
    return ok(
        "staged_botfiles",
        "staged archive and loose botfiles match authored assets",
        metrics=metrics,
        artifacts={
            "install_dir": rel(install_dir, repo_root),
            "archive": rel(archive_path, repo_root),
        },
    )


def check_staged_aas(repo_root: pathlib.Path, install_dir: pathlib.Path, base_game: str) -> CheckResult:
    maps_dir = install_dir / base_game / "maps"
    failures: list[str] = []
    sizes: dict[str, int] = {}
    for map_name in REQUIRED_AAS_MAPS:
        path = maps_dir / f"{map_name}.aas"
        if not path.is_file():
            failures.append(f"missing staged AAS: {rel(path, repo_root)}")
            continue
        size = path.stat().st_size
        sizes[map_name] = size
        if size <= 0:
            failures.append(f"empty staged AAS: {rel(path, repo_root)}")

    metrics = {
        "required_maps": len(REQUIRED_AAS_MAPS),
        "present_maps": len(sizes),
        "total_aas_bytes": sum(sizes.values()),
    }
    if failures:
        return fail(
            "staged_aas",
            "required staged AAS files are missing or empty",
            failures,
            metrics=metrics,
            artifacts={"maps_dir": rel(maps_dir, repo_root)},
        )
    return ok(
        "staged_aas",
        "required staged AAS files are present",
        metrics=metrics,
        artifacts={"maps_dir": rel(maps_dir, repo_root)},
    )


def check_user_docs(repo_root: pathlib.Path) -> CheckResult:
    failures: list[str] = []
    metrics: dict[str, Any] = {}
    for doc in REQUIRED_USER_DOCS:
        path = repo_root / doc
        if not path.is_file():
            failures.append(f"missing user doc: {doc}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        metrics[doc] = len(text)

    bots_text = (repo_root / "docs-user" / "bots.md").read_text(
        encoding="utf-8",
        errors="replace",
    ) if (repo_root / "docs-user" / "bots.md").is_file() else ""
    for token in ("bot_min_players", "addbot", "bot_reload_profiles"):
        if token not in bots_text:
            failures.append(f"docs-user/bots.md should mention {token}")

    if failures:
        return fail("user_docs", "bot user documentation is incomplete", failures, metrics=metrics)
    return ok("user_docs", "bot user documentation covers setup, profiles, and map readiness", metrics=metrics)


def check_playtest_plan(repo_root: pathlib.Path) -> CheckResult:
    cases = generate_bot_playtest.default_playtest_cases()
    failures = generate_bot_playtest.validate_cases(cases)
    modes = {case.mode for case in cases}
    for mode in REQUIRED_PLAYTEST_MODES:
        if mode not in modes:
            failures.append(f"playtest plan missing required mode: {mode}")

    for case in cases:
        command_text = "\n".join(case.command_lines())
        if "bot_min_players" not in command_text:
            failures.append(f"{case.case_id} does not exercise bot_min_players")
        if case.map_name not in REQUIRED_AAS_MAPS:
            failures.append(
                f"{case.case_id} uses map {case.map_name!r}, which is not in the release AAS gate"
            )

    metrics = {
        "cases": len(cases),
        "modes": len(modes),
        "total_minutes": sum(case.duration_minutes for case in cases),
        "configs": len({case.config_name for case in cases}),
    }
    artifacts = {
        "tool": "tools/bot_playtest/generate_bot_playtest.py",
        "doc": "docs-user/bot-playtest.md",
    }
    if failures:
        return fail(
            "playtest_plan",
            "multiplayer bot playtest plan is incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "playtest_plan",
        "multiplayer bot playtest covers FFA, Duel, TDM, and CTF",
        metrics=metrics,
        artifacts=artifacts,
    )


def check_playtest_triage(repo_root: pathlib.Path) -> CheckResult:
    cases = generate_bot_playtest.default_playtest_cases()
    missing = triage_bot_playtest.validate_category_coverage(cases)
    categories = {
        category.key: category
        for category in triage_bot_playtest.FAILURE_CATEGORIES
    }
    failures: list[str] = []
    if missing:
        failures.extend(f"unclassified playtest failure signal: {item}" for item in missing)

    required_categories = {
        "route_commitment",
        "route_stuck",
        "close_threat_spacing",
        "weak_retreat",
        "min_players",
        "duel_queue",
        "ctf_objective",
    }
    missing_categories = sorted(required_categories - set(categories))
    failures.extend(f"missing playtest triage category: {item}" for item in missing_categories)

    metrics = {
        "categories": len(categories),
        "case_failure_signals": sum(len(case.failure_signals) for case in cases),
        "critical_categories": sum(
            1 for category in triage_bot_playtest.FAILURE_CATEGORIES if category.critical
        ),
    }
    artifacts = {
        "tool": "tools/bot_playtest/triage_bot_playtest.py",
        "notes_template": ".tmp/bot_playtest/bot_multiplayer_playtest_notes_template.json",
    }
    if failures:
        return fail(
            "playtest_triage",
            "multiplayer bot playtest triage catalog is incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "playtest_triage",
        "multiplayer bot playtest failure signals map to scenario candidates",
        metrics=metrics,
        artifacts=artifacts,
    )


def check_perf_tooling(repo_root: pathlib.Path) -> CheckResult:
    perf_dir = repo_root / "tools" / "bot_perf"
    failures: list[str] = []
    artifacts: dict[str, str] = {}

    for name in REQUIRED_PERF_BUDGETS:
        path = perf_dir / name
        artifacts[name] = rel(path, repo_root)
        if not path.is_file():
            failures.append(f"missing perf budget: {rel(path, repo_root)}")
            continue
        try:
            analyze_bot_perf.load_budget(path)
        except SystemExit as exc:
            failures.append(f"invalid perf budget {rel(path, repo_root)}: {exc}")

    for name in REQUIRED_VARIANCE_BUDGETS:
        path = perf_dir / name
        artifacts[name] = rel(path, repo_root)
        if not path.is_file():
            failures.append(f"missing perf variance budget: {rel(path, repo_root)}")
            continue
        try:
            analyze_bot_perf.load_variance_budget(path)
        except SystemExit as exc:
            failures.append(f"invalid perf variance budget {rel(path, repo_root)}: {exc}")

    readme = perf_dir / "README.md"
    artifacts["readme"] = rel(readme, repo_root)
    if not readme.is_file():
        failures.append(f"missing perf README: {rel(readme, repo_root)}")
    else:
        text = readme.read_text(encoding="utf-8", errors="replace")
        for token in ("--variance-budget", "source_counter_variance_budget.json"):
            if token not in text:
                failures.append(f"tools/bot_perf/README.md should mention {token}")

    metrics = {
        "budgets": len(REQUIRED_PERF_BUDGETS),
        "variance_budgets": len(REQUIRED_VARIANCE_BUDGETS),
    }
    if failures:
        return fail(
            "perf_tooling",
            "bot perf budgets and variance gate are incomplete",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "perf_tooling",
        "bot perf per-run and repeated-soak variance budgets are valid",
        metrics=metrics,
        artifacts=artifacts,
    )


def scenario_report_score(path: pathlib.Path) -> tuple[int, int, int, float]:
    try:
        payload = load_json(path)
    except (OSError, json.JSONDecodeError):
        return (-1, -1, -1, 0.0)
    summary = payload.get("summary", {})
    total = int(summary.get("total", len(payload.get("scenarios", [])) or len(payload.get("catalog", [])) or 0) or 0)
    passed = int(summary.get("passed", 0) or 0)
    failed = int(summary.get("failed", 0) or 0)
    mtime = path.stat().st_mtime
    return (passed, total, -failed, mtime)


def discover_scenario_report(repo_root: pathlib.Path) -> pathlib.Path | None:
    root = repo_root / ".tmp" / "bot_scenarios"
    if not root.is_dir():
        return None
    candidates = [path for path in root.rglob("*.json") if path.is_file()]
    scored = [(scenario_report_score(path), path) for path in candidates]
    scored = [item for item in scored if item[0][1] > 0]
    if not scored:
        return None
    return max(scored, key=lambda item: item[0])[1]


def check_scenario_report(
    repo_root: pathlib.Path,
    report_path: pathlib.Path | None,
    *,
    min_implemented_rows: int,
    allow_missing: bool,
) -> CheckResult:
    if report_path is None:
        report_path = discover_scenario_report(repo_root)
    if report_path is None:
        message = "no bot scenario report was found"
        details = ["expected a JSON report under .tmp/bot_scenarios or --scenario-report"]
        if allow_missing:
            return warn("scenario_evidence", message, details)
        return fail("scenario_evidence", message, details)

    try:
        payload = load_json(report_path)
    except (OSError, json.JSONDecodeError) as exc:
        return fail(
            "scenario_evidence",
            "bot scenario report could not be read",
            [str(exc)],
            artifacts={"scenario_report": rel(report_path, repo_root)},
        )

    summary = payload.get("summary", {})
    scenarios = payload.get("scenarios", [])
    scenario_names = {row.get("name") for row in scenarios if row.get("name")}
    missing_required = sorted(set(REQUIRED_SCENARIOS) - scenario_names)
    total = int(summary.get("total", len(scenarios)) or 0)
    passed = int(summary.get("passed", 0) or 0)
    failed = int(summary.get("failed", 0) or 0)
    timeout = int(summary.get("timeout", 0) or 0)
    errors = int(summary.get("error", 0) or 0)
    pending = int(summary.get("pending", 0) or 0)
    overall = summary.get("overall", "")
    movement_rows = sum(1 for row in scenarios if "movement" in row.get("selection_tags", []))

    failures: list[str] = []
    if total < min_implemented_rows:
        failures.append(
            f"scenario report has {total} rows; expected at least {min_implemented_rows}"
        )
    if failed or timeout or errors or pending or overall not in ("pass", "passed"):
        failures.append(
            "scenario summary is not clean: "
            f"overall={overall!r} failed={failed} timeout={timeout} "
            f"error={errors} pending={pending}"
        )
    for name in missing_required:
        failures.append(f"missing required scenario evidence: {name}")

    metrics = {
        "total": total,
        "passed": passed,
        "failed": failed,
        "timeout": timeout,
        "error": errors,
        "pending": pending,
        "movement_rows": movement_rows,
        "required_scenarios": len(REQUIRED_SCENARIOS),
    }
    artifacts = {"scenario_report": rel(report_path, repo_root)}
    if failures:
        return fail(
            "scenario_evidence",
            "bot scenario evidence does not satisfy release gate",
            failures,
            metrics=metrics,
            artifacts=artifacts,
        )
    return ok(
        "scenario_evidence",
        "bot scenario evidence satisfies release gate",
        metrics=metrics,
        artifacts=artifacts,
    )


def run_acceptance(
    repo_root: pathlib.Path,
    *,
    install_dir: pathlib.Path | None = None,
    base_game: str = "basew",
    scenario_report: pathlib.Path | None = None,
    min_implemented_rows: int = 114,
    allow_missing_scenario_report: bool = False,
) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    install_dir = (install_dir or repo_root / ".install").resolve()
    if scenario_report is not None:
        scenario_report = scenario_report.resolve()

    checks = [
        check_surface(repo_root),
        check_profiles(repo_root),
        check_bots_txt(repo_root),
        check_authored_botfiles(repo_root),
        check_staged_botfiles(repo_root, install_dir, base_game),
        check_staged_aas(repo_root, install_dir, base_game),
        check_user_docs(repo_root),
        check_playtest_plan(repo_root),
        check_playtest_triage(repo_root),
        check_perf_tooling(repo_root),
        check_scenario_report(
            repo_root,
            scenario_report,
            min_implemented_rows=min_implemented_rows,
            allow_missing=allow_missing_scenario_report,
        ),
    ]
    failures = [check for check in checks if check.status == "fail"]
    warnings = [check for check in checks if check.status == "warn" or check.warnings]
    return {
        "schema_version": 1,
        "repo_root": str(repo_root),
        "install_dir": str(install_dir),
        "base_game": base_game,
        "summary": {
            "status": "failed" if failures else "passed",
            "checks": len(checks),
            "passed": sum(1 for check in checks if check.status == "pass"),
            "failed": len(failures),
            "warnings": len(warnings),
        },
        "checks": [check.to_json() for check in checks],
    }


def format_text(report: dict[str, Any]) -> str:
    summary = report["summary"]
    lines = [
        f"bot release acceptance: {summary['status']}",
        (
            f"checks={summary['checks']} passed={summary['passed']} "
            f"failed={summary['failed']} warnings={summary['warnings']}"
        ),
    ]
    for check in report["checks"]:
        lines.append("")
        lines.append(f"[{check['status']}] {check['name']}: {check['message']}")
        if check["metrics"]:
            metrics = ", ".join(f"{key}={value}" for key, value in check["metrics"].items())
            lines.append(f"  metrics: {metrics}")
        if check["artifacts"]:
            artifacts = ", ".join(f"{key}={value}" for key, value in check["artifacts"].items())
            lines.append(f"  artifacts: {artifacts}")
        for failure in check["failures"]:
            lines.append(f"  failure: {failure}")
        for warning in check["warnings"]:
            lines.append(f"  warning: {warning}")
    return "\n".join(lines) + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=pathlib.Path, default=REPO_ROOT)
    parser.add_argument("--install-dir", type=pathlib.Path)
    parser.add_argument("--base-game", default="basew")
    parser.add_argument("--scenario-report", type=pathlib.Path)
    parser.add_argument("--min-implemented-rows", type=int, default=114)
    parser.add_argument(
        "--allow-missing-scenario-report",
        action="store_true",
        help="Warn instead of failing when no scenario report artifact exists.",
    )
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--output", type=pathlib.Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    report = run_acceptance(
        args.repo_root,
        install_dir=args.install_dir,
        base_game=args.base_game,
        scenario_report=args.scenario_report,
        min_implemented_rows=args.min_implemented_rows,
        allow_missing_scenario_report=args.allow_missing_scenario_report,
    )
    output = (
        json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.format == "json"
        else format_text(report)
    )
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    else:
        print(output, end="")
    return 0 if report["summary"]["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
