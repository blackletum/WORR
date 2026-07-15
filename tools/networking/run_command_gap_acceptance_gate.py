#!/usr/bin/env python3
"""Run the headless production command-gap recovery acceptance gate.

The dedicated server executes ``SV_WorrFillCommandGap`` through its operator
self-test.  The test feeds two packet-history-authorized discontinuities that
previously exceeded the 128-slot retention ring.  It intentionally selects the
large-loss ``simulation_budget == 0`` path, so no graphical client, renderer,
or game callback is needed to prove bounded command-stream recovery.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


SCHEMA = "worr.networking.command-gap-acceptance.v1"
EXPECTED_GAPS = (161, 401)
BASELINE_SEQUENCE = 1000

STATUS_RE = re.compile(
    r"^worr_command_gap_selftest: case=gap-(?P<case>\d+) "
    r"status=(?P<status>pass|fail) gap=(?P<gap>\d+) "
    r"synthesized=(?P<synthesized>\d+) skipped=(?P<skipped>\d+) "
    r"received=(?P<received_epoch>\d+):(?P<received_sequence>\d+) "
    r"consumed=(?P<consumed_epoch>\d+):(?P<consumed_sequence>\d+) "
    r"attempts=(?P<attempts>\d+) "
    r"fast_forwards=(?P<fast_forwards>\d+) "
    r"fast_forwarded=(?P<fast_forwarded>\d+) "
    r"rejections=(?P<rejections>\d+) "
    r"policy_rejections=(?P<policy_rejections>\d+) "
    r"stream_valid=(?P<stream_valid>[01]) "
    r"cursor_valid=(?P<cursor_valid>[01])$",
    re.MULTILINE,
)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json_atomic(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def invalidate_previous_outputs(output: Path) -> Path:
    failure = output.with_suffix(".failure.json")
    output.unlink(missing_ok=True)
    failure.unlink(missing_ok=True)
    return failure


def build_command(dedicated_exe: Path) -> list[str]:
    """Build the explicit dedicated-server-only command line."""
    return [
        str(dedicated_exe),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+sv_worr_command_gap_selftest",
        "+quit",
    ]


def parse_statuses(text: str) -> list[dict[str, int | str]]:
    statuses: list[dict[str, int | str]] = []
    for match in STATUS_RE.finditer(text):
        row: dict[str, int | str] = {}
        for name, value in match.groupdict().items():
            row[name] = value if name == "status" else int(value)
        statuses.append(row)
    return statuses


def validate_statuses(
    statuses: list[dict[str, int | str]],
) -> list[dict[str, int | str]]:
    """Require exactly the two retention-exceeding recovery proofs."""
    by_case: dict[int, dict[str, int | str]] = {}
    for status in statuses:
        case = int(status["case"])
        if case in by_case:
            raise RuntimeError(f"command-gap gate emitted duplicate case {case}")
        by_case[case] = status

    if tuple(sorted(by_case)) != EXPECTED_GAPS:
        raise RuntimeError(
            "command-gap gate cases do not match the required retention "
            f"boundaries: observed={sorted(by_case)} expected={list(EXPECTED_GAPS)}"
        )

    validated: list[dict[str, int | str]] = []
    for gap in EXPECTED_GAPS:
        status = by_case[gap]
        expected_sequence = BASELINE_SEQUENCE + gap
        if status["status"] != "pass":
            raise RuntimeError(f"command-gap case {gap} reported failure")
        for field, expected in (
            ("gap", gap),
            ("synthesized", 0),
            ("skipped", gap),
            ("received_epoch", 1),
            ("received_sequence", expected_sequence),
            ("consumed_epoch", 1),
            ("consumed_sequence", expected_sequence),
            ("attempts", 1),
            ("fast_forwards", 1),
            ("fast_forwarded", gap),
            ("rejections", 0),
            ("policy_rejections", 0),
            ("stream_valid", 1),
            ("cursor_valid", 1),
        ):
            if status[field] != expected:
                raise RuntimeError(
                    f"command-gap case {gap} violated {field}: "
                    f"observed={status[field]} expected={expected}"
                )
        validated.append(status)
    return validated


def artifact_manifest(root: Path) -> list[dict[str, object]]:
    artifacts: list[dict[str, object]] = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        artifacts.append(
            {
                "path": str(path),
                "bytes": path.stat().st_size,
                "sha256": file_sha256(path),
            }
        )
    return artifacts


def creation_flags() -> int:
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dedicated-exe", required=True, type=Path)
    parser.add_argument("--working-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    args.dedicated_exe = args.dedicated_exe.resolve()
    args.working_dir = args.working_dir.resolve()
    args.output = args.output.resolve()
    if not args.dedicated_exe.is_file():
        parser.error(f"dedicated executable is missing: {args.dedicated_exe}")
    if not args.working_dir.is_dir():
        parser.error(f"working directory is missing: {args.working_dir}")

    failure_output = invalidate_previous_outputs(args.output)
    started = datetime.now(timezone.utc)
    run_id = started.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    run_root = args.output.parent / f"{args.output.stem}.runs" / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    stdout_path = run_root / "dedicated.stdout.log"
    stderr_path = run_root / "dedicated.stderr.log"
    command = build_command(args.dedicated_exe)

    try:
        result = subprocess.run(
            command,
            cwd=args.working_dir,
            capture_output=True,
            text=True,
            timeout=args.timeout,
            creationflags=creation_flags(),
            check=False,
        )
        stdout_path.write_text(result.stdout, encoding="utf-8", newline="\n")
        stderr_path.write_text(result.stderr, encoding="utf-8", newline="\n")
        if result.returncode != 0:
            raise RuntimeError(
                f"dedicated command-gap self-test returned {result.returncode}"
            )
        if result.stderr:
            raise RuntimeError("dedicated command-gap self-test wrote stderr")
        statuses = validate_statuses(parse_statuses(result.stdout))
        artifacts = artifact_manifest(run_root)
        report: dict[str, object] = {
            "schema": SCHEMA,
            "run_id": run_id,
            "started_at_utc": started.isoformat(),
            "completed_at_utc": datetime.now(timezone.utc).isoformat(),
            "dedicated_executable": str(args.dedicated_exe),
            "dedicated_sha256": file_sha256(args.dedicated_exe),
            "working_directory": str(args.working_dir),
            "command": command,
            "simulation_budget": 0,
            "expected_gaps": list(EXPECTED_GAPS),
            "cases": statuses,
            "stdout": str(stdout_path),
            "stderr": str(stderr_path),
            "stdout_sha256": file_sha256(stdout_path),
            "stderr_sha256": file_sha256(stderr_path),
            "artifacts": artifacts,
        }
        write_json_atomic(run_root / "report.json", report)
        write_json_atomic(args.output, report)
    except Exception as error:
        failure: dict[str, object] = {
            "schema": SCHEMA + ".failure",
            "run_id": run_id,
            "started_at_utc": started.isoformat(),
            "failed_at_utc": datetime.now(timezone.utc).isoformat(),
            "dedicated_executable": str(args.dedicated_exe),
            "working_directory": str(args.working_dir),
            "command": command,
            "error_type": type(error).__name__,
            "error": str(error),
            "artifacts": artifact_manifest(run_root),
        }
        write_json_atomic(run_root / "failure.json", failure)
        write_json_atomic(failure_output, failure)
        print(f"command-gap acceptance failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 1

    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
