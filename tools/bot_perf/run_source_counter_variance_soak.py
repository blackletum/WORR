#!/usr/bin/env python3
"""Run repeated source-counter soaks and compare variance budgets."""

from __future__ import annotations

import argparse
import copy
import datetime as _dt
import json
import pathlib
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Any


SCHEMA = "worr-bot-source-counter-variance-soak-v1"
DEFAULT_SCENARIO = "high_bot_soak_degradation"


@dataclass(frozen=True)
class RunnerConfig:
    repo_root: pathlib.Path
    python_executable: str
    scenario_runner: pathlib.Path
    perf_analyzer: pathlib.Path
    binary: pathlib.Path
    install_dir: pathlib.Path
    budget: pathlib.Path
    variance_budget: pathlib.Path
    artifact_dir: pathlib.Path
    json_out: pathlib.Path
    markdown_out: pathlib.Path
    scenario: str
    game: str
    map_name: str
    runs: int
    timeout: int
    base_port: int
    dry_run: bool
    output_format: str


def utc_timestamp() -> str:
    return _dt.datetime.now(_dt.UTC).strftime("%Y%m%dT%H%M%SZ")


def resolve_path(repo_root: pathlib.Path, value: str | pathlib.Path) -> pathlib.Path:
    path = pathlib.Path(value)
    if not path.is_absolute():
        path = repo_root / path
    return path.resolve()


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run repeated high-bot source-counter soaks, merge their scenario "
            "reports, and evaluate strict per-run plus repeated-run variance budgets."
        )
    )
    parser.add_argument("--repo-root", default=".", help="WORR repository root")
    parser.add_argument(
        "--python",
        default=sys.executable,
        help="Python executable used for child scenario/analyzer commands",
    )
    parser.add_argument(
        "--scenario-runner",
        default="tools/bot_scenarios/run_bot_scenarios.py",
        help="Scenario harness script",
    )
    parser.add_argument(
        "--perf-analyzer",
        default="tools/bot_perf/analyze_bot_perf.py",
        help="Bot perf analyzer script",
    )
    parser.add_argument("--binary", default=".install/worr_ded_x86_64.exe")
    parser.add_argument("--install-dir", default=".install")
    parser.add_argument(
        "--budget",
        default="tools/bot_perf/source_counter_soak_budget.json",
        help="Strict per-run source-counter budget",
    )
    parser.add_argument(
        "--variance-budget",
        default="tools/bot_perf/source_counter_variance_budget.json",
        help="Repeated-run variance budget",
    )
    parser.add_argument(
        "--artifact-dir",
        default=".tmp/bot_perf/source_counter_variance_soak",
        help="Output root for run artifacts",
    )
    parser.add_argument("--json-out", help="Machine-readable orchestrator report")
    parser.add_argument("--markdown-out", help="Analyzer Markdown comparison report")
    parser.add_argument("--scenario", default=DEFAULT_SCENARIO)
    parser.add_argument("--game", default="basew")
    parser.add_argument("--map", default="mm-rage", dest="map_name")
    parser.add_argument("--runs", type=int, default=2, help="Repeated soak count")
    parser.add_argument("--timeout", type=int, default=720, help="Per-run timeout in seconds")
    parser.add_argument("--base-port", type=int, default=28000)
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Console output format",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Write the planned command/report shape without launching soaks",
    )
    args = parser.parse_args(argv)

    if args.runs < 2:
        parser.error("--runs must be at least 2 for a variance comparison")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def build_config(args: argparse.Namespace) -> RunnerConfig:
    repo_root = pathlib.Path(args.repo_root).resolve()
    artifact_dir = resolve_path(repo_root, args.artifact_dir)
    json_out = (
        resolve_path(repo_root, args.json_out)
        if args.json_out
        else artifact_dir / "source_counter_variance_soak.json"
    )
    markdown_out = (
        resolve_path(repo_root, args.markdown_out)
        if args.markdown_out
        else artifact_dir / "source_counter_variance_soak.md"
    )

    return RunnerConfig(
        repo_root=repo_root,
        python_executable=str(args.python),
        scenario_runner=resolve_path(repo_root, args.scenario_runner),
        perf_analyzer=resolve_path(repo_root, args.perf_analyzer),
        binary=resolve_path(repo_root, args.binary),
        install_dir=resolve_path(repo_root, args.install_dir),
        budget=resolve_path(repo_root, args.budget),
        variance_budget=resolve_path(repo_root, args.variance_budget),
        artifact_dir=artifact_dir,
        json_out=json_out,
        markdown_out=markdown_out,
        scenario=args.scenario,
        game=args.game,
        map_name=args.map_name,
        runs=args.runs,
        timeout=args.timeout,
        base_port=args.base_port,
        dry_run=bool(args.dry_run),
        output_format=args.format,
    )


def validate_config(config: RunnerConfig) -> None:
    checks = (
        ("repository root", config.repo_root, "is_dir"),
        ("scenario runner", config.scenario_runner, "is_file"),
        ("perf analyzer", config.perf_analyzer, "is_file"),
        ("dedicated server binary", config.binary, "is_file"),
        ("install dir", config.install_dir, "is_dir"),
        ("budget", config.budget, "is_file"),
        ("variance budget", config.variance_budget, "is_file"),
    )
    for label, path, predicate in checks:
        if not getattr(path, predicate)():
            raise SystemExit(f"{label} not found: {path}")


def scenario_report_path(run_dir: pathlib.Path) -> pathlib.Path:
    return run_dir / "scenario-report.json"


def scenario_markdown_path(run_dir: pathlib.Path) -> pathlib.Path:
    return run_dir / "scenario-report.md"


def build_scenario_command(config: RunnerConfig, run_index: int, run_dir: pathlib.Path) -> list[str]:
    return [
        config.python_executable,
        str(config.scenario_runner),
        "--repo-root",
        str(config.repo_root),
        "--binary",
        str(config.binary),
        "--install-dir",
        str(config.install_dir),
        "--game",
        config.game,
        "--map",
        config.map_name,
        "--scenario",
        config.scenario,
        "--timeout",
        str(config.timeout),
        "--base-port",
        str(config.base_port + run_index - 1),
        "--artifact-dir",
        str(run_dir / "scenario-artifacts"),
        "--format",
        "text",
        "--json-out",
        str(scenario_report_path(run_dir)),
        "--markdown-out",
        str(scenario_markdown_path(run_dir)),
    ]


def build_analyzer_command(
    config: RunnerConfig,
    stdout_paths: list[pathlib.Path],
    combined_report: pathlib.Path,
) -> list[str]:
    return [
        config.python_executable,
        str(config.perf_analyzer),
        "--format",
        "json",
        "--budget",
        str(config.budget),
        "--variance-budget",
        str(config.variance_budget),
        "--markdown-out",
        str(config.markdown_out),
        "--scenario-report",
        str(combined_report),
        *[str(path) for path in stdout_paths],
    ]


def run_command(command: list[str], cwd: pathlib.Path, stdout_path: pathlib.Path, stderr_path: pathlib.Path) -> int:
    started = time.monotonic()
    completed = subprocess.run(
        command,
        cwd=str(cwd),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    duration = time.monotonic() - started
    stdout_path.write_text(completed.stdout, encoding="utf-8")
    stderr_path.write_text(completed.stderr, encoding="utf-8")
    timing_path = stdout_path.with_suffix(stdout_path.suffix + ".timing.json")
    timing_path.write_text(
        json.dumps({"duration_seconds": round(duration, 3)}, indent=2),
        encoding="utf-8",
    )
    return int(completed.returncode)


def load_json_object(path: pathlib.Path, label: str) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise SystemExit(f"Unable to read {label} {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"Invalid {label} JSON {path}: {exc}") from exc
    if not isinstance(payload, dict):
        raise SystemExit(f"Invalid {label} {path}: expected JSON object")
    return payload


def scenario_stdout_path(report_path: pathlib.Path, scenario_name: str) -> pathlib.Path:
    payload = load_json_object(report_path, "scenario report")
    scenarios = payload.get("scenarios")
    if not isinstance(scenarios, list):
        raise SystemExit(f"Invalid scenario report {report_path}: expected scenarios array")

    fallback: pathlib.Path | None = None
    for scenario in scenarios:
        if not isinstance(scenario, dict):
            continue
        stdout_path = scenario.get("stdout_path")
        if not isinstance(stdout_path, str) or not stdout_path:
            continue
        if fallback is None:
            fallback = pathlib.Path(stdout_path)
        if scenario.get("name") == scenario_name:
            return pathlib.Path(stdout_path)

    if fallback is not None:
        return fallback
    raise SystemExit(f"Scenario report {report_path} does not contain stdout_path entries")


def merged_scenario_report_payload(
    report_paths: list[pathlib.Path],
    repo_root: pathlib.Path,
    artifact_dir: pathlib.Path,
    started_utc: str,
) -> dict[str, Any]:
    scenarios: list[dict[str, Any]] = []
    input_reports: list[str] = []

    for run_index, report_path in enumerate(report_paths, start=1):
        payload = load_json_object(report_path, "scenario report")
        input_reports.append(str(report_path))
        for scenario in payload.get("scenarios", []):
            if not isinstance(scenario, dict):
                continue
            copied = copy.deepcopy(scenario)
            copied["source_run_index"] = run_index
            copied["source_report_path"] = str(report_path)
            scenarios.append(copied)

    return {
        "schema_version": 1,
        "schema": f"{SCHEMA}-combined-scenario-report",
        "started_utc": started_utc,
        "repo_root": str(repo_root),
        "artifact_dir": str(artifact_dir),
        "input_reports": input_reports,
        "scenarios": scenarios,
    }


def write_merged_scenario_report(
    report_paths: list[pathlib.Path],
    output_path: pathlib.Path,
    repo_root: pathlib.Path,
    artifact_dir: pathlib.Path,
    started_utc: str,
) -> dict[str, Any]:
    payload = merged_scenario_report_payload(report_paths, repo_root, artifact_dir, started_utc)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return payload


def report_status(report: dict[str, Any]) -> str:
    if report.get("dry_run"):
        return "dry-run"
    if report.get("failures"):
        return "fail"
    analysis = report.get("analysis")
    if isinstance(analysis, dict):
        comparison = analysis.get("comparison")
        if isinstance(comparison, dict):
            variance = comparison.get("variance_budget")
            if isinstance(variance, dict):
                return "pass" if variance.get("pass") else "fail"
    return "pass"


def print_text_summary(report: dict[str, Any]) -> None:
    print(f"source-counter variance soak: {report_status(report)}")
    print(f"scenario: {report['scenario']} runs={report['runs_requested']}")
    print(f"artifacts: {report['artifact_dir']}")
    print(f"json: {report['json_out']}")
    print(f"markdown: {report['markdown_out']}")
    for run in report["runs"]:
        stdout_path = run.get("stdout_path", "n/a")
        print(
            f"run {run['index']:02d}: returncode={run.get('returncode', 'planned')} "
            f"stdout={stdout_path}"
        )
    analysis = report.get("analysis")
    if isinstance(analysis, dict):
        comparison = analysis.get("comparison", {})
        variance = comparison.get("variance_budget", {})
        if isinstance(variance, dict):
            print(
                "variance_budget: "
                f"status={variance.get('status')} checks={variance.get('check_count')} "
                f"failures={len(variance.get('failures', []))}"
            )
    for failure in report.get("failures", []):
        print(f"failure: {failure}")


def write_report(config: RunnerConfig, report: dict[str, Any]) -> None:
    report["status"] = report_status(report)
    config.json_out.parent.mkdir(parents=True, exist_ok=True)
    config.json_out.write_text(json.dumps(report, indent=2), encoding="utf-8")


def planned_report(config: RunnerConfig, started_utc: str) -> dict[str, Any]:
    runs: list[dict[str, Any]] = []
    for run_index in range(1, config.runs + 1):
        run_dir = config.artifact_dir / f"run-{run_index:02d}"
        command = build_scenario_command(config, run_index, run_dir)
        runs.append({
            "index": run_index,
            "run_dir": str(run_dir),
            "scenario_report": str(scenario_report_path(run_dir)),
            "scenario_markdown": str(scenario_markdown_path(run_dir)),
            "command": command,
        })

    combined_report = config.artifact_dir / "combined-scenario-report.json"
    analyzer_command = build_analyzer_command(
        config,
        [
            pathlib.Path(f"<run-{run_index:02d}-stdout-path>")
            for run_index in range(1, config.runs + 1)
        ],
        combined_report,
    )
    return {
        "schema": SCHEMA,
        "started_utc": started_utc,
        "dry_run": True,
        "repo_root": str(config.repo_root),
        "artifact_dir": str(config.artifact_dir),
        "scenario": config.scenario,
        "game": config.game,
        "map": config.map_name,
        "runs_requested": config.runs,
        "timeout_seconds": config.timeout,
        "base_port": config.base_port,
        "budget": str(config.budget),
        "variance_budget": str(config.variance_budget),
        "json_out": str(config.json_out),
        "markdown_out": str(config.markdown_out),
        "runs": runs,
        "combined_scenario_report": str(combined_report),
        "analysis_command": analyzer_command,
        "failures": [],
    }


def execute(config: RunnerConfig) -> dict[str, Any]:
    started_utc = utc_timestamp()
    config.artifact_dir.mkdir(parents=True, exist_ok=True)

    if config.dry_run:
        return planned_report(config, started_utc)

    validate_config(config)

    runs: list[dict[str, Any]] = []
    report_paths: list[pathlib.Path] = []
    stdout_paths: list[pathlib.Path] = []
    failures: list[str] = []

    for run_index in range(1, config.runs + 1):
        run_dir = config.artifact_dir / f"run-{run_index:02d}"
        run_dir.mkdir(parents=True, exist_ok=True)
        command = build_scenario_command(config, run_index, run_dir)
        stdout_capture = run_dir / "scenario-runner.stdout.txt"
        stderr_capture = run_dir / "scenario-runner.stderr.txt"
        returncode = run_command(command, config.repo_root, stdout_capture, stderr_capture)
        run_report: dict[str, Any] = {
            "index": run_index,
            "run_dir": str(run_dir),
            "command": command,
            "returncode": returncode,
            "scenario_report": str(scenario_report_path(run_dir)),
            "scenario_markdown": str(scenario_markdown_path(run_dir)),
            "runner_stdout": str(stdout_capture),
            "runner_stderr": str(stderr_capture),
        }
        if returncode != 0:
            failures.append(
                f"scenario run {run_index} exited {returncode}; see {stdout_capture} and {stderr_capture}"
            )
            runs.append(run_report)
            break

        report_path = scenario_report_path(run_dir)
        stdout_path = scenario_stdout_path(report_path, config.scenario)
        run_report["stdout_path"] = str(stdout_path)
        runs.append(run_report)
        report_paths.append(report_path)
        stdout_paths.append(stdout_path)

    combined_report = config.artifact_dir / "combined-scenario-report.json"
    analysis_stdout = config.artifact_dir / "analyze.stdout.json"
    analysis_stderr = config.artifact_dir / "analyze.stderr.txt"
    analysis_command: list[str] = []
    analysis_payload: dict[str, Any] | None = None

    if not failures and len(report_paths) == config.runs:
        write_merged_scenario_report(
            report_paths,
            combined_report,
            config.repo_root,
            config.artifact_dir,
            started_utc,
        )
        analysis_command = build_analyzer_command(config, stdout_paths, combined_report)
        analysis_returncode = run_command(
            analysis_command,
            config.repo_root,
            analysis_stdout,
            analysis_stderr,
        )
        if analysis_returncode != 0:
            failures.append(
                f"perf analyzer exited {analysis_returncode}; see {analysis_stdout} and {analysis_stderr}"
            )
        try:
            analysis_payload = json.loads(analysis_stdout.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            failures.append(f"unable to read analyzer JSON output {analysis_stdout}: {exc}")
            analysis_payload = None

    report: dict[str, Any] = {
        "schema": SCHEMA,
        "started_utc": started_utc,
        "dry_run": False,
        "repo_root": str(config.repo_root),
        "artifact_dir": str(config.artifact_dir),
        "scenario": config.scenario,
        "game": config.game,
        "map": config.map_name,
        "runs_requested": config.runs,
        "timeout_seconds": config.timeout,
        "base_port": config.base_port,
        "budget": str(config.budget),
        "variance_budget": str(config.variance_budget),
        "json_out": str(config.json_out),
        "markdown_out": str(config.markdown_out),
        "runs": runs,
        "combined_scenario_report": str(combined_report),
        "analysis_command": analysis_command,
        "analysis_stdout": str(analysis_stdout),
        "analysis_stderr": str(analysis_stderr),
        "analysis": analysis_payload,
        "failures": failures,
    }
    return report


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    config = build_config(args)
    report = execute(config)
    write_report(config, report)

    if config.output_format == "json":
        print(json.dumps(report, indent=2))
    else:
        print_text_summary(report)

    return 0 if report_status(report) in ("pass", "dry-run") else 1


if __name__ == "__main__":
    raise SystemExit(main())
