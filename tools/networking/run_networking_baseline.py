#!/usr/bin/env python3
"""Run the production impairment model repeatedly and write stable evidence."""

from __future__ import annotations

import argparse
import difflib
import json
import subprocess
from pathlib import Path


GOLDEN_SCHEMA = "worr.networking.impairment-golden.v1"
DEFAULT_GOLDEN = (
    Path(__file__).resolve().parent
    / "baselines"
    / "impairment-model-golden-v1.json"
)

MODEL_ARGUMENTS = {
    "seed": "--seed",
    "packets": "--packets",
    "step_ms": "--step-ms",
    "upstream_every": "--upstream-every",
    "latency_ms": "--latency-ms",
    "jitter_ms": "--jitter-ms",
    "upstream_stall_ms": "--upstream-stall-ms",
    "rate_bytes_per_sec": "--rate-bytes-per-sec",
    "loss_basis_points": "--loss-bp",
    "burst_start_basis_points": "--burst-bp",
    "burst_length": "--burst-length",
    "reorder_basis_points": "--reorder-bp",
    "duplicate_basis_points": "--duplicate-bp",
    "corrupt_basis_points": "--corrupt-bp",
}

STABLE_MODEL_FIELDS = (
    "schema",
    "seed",
    "packets",
    "step_ms",
    "upstream_every",
    "upstream_packets",
    "upstream_stalled",
    "burst_dropped",
    "corrupted",
    "delay_max_ms",
    "delay_mean_ms",
    "delay_min_ms",
    "delivered",
    "digest",
    "dropped",
    "duplicated",
    "future_ack_rejected",
    "reliable_ack_conflict_rejected",
    "reordered",
    "release_inversions",
    "scheduled_copies",
    "sequence_wrap_fail_closed",
    "stale_ack_rejected",
    "throttled",
)

STABLE_CONFIG_FIELDS = (
    "upstream_stall_ms",
    "burst_length",
    "burst_start_basis_points",
    "corrupt_basis_points",
    "duplicate_basis_points",
    "jitter_ms",
    "latency_ms",
    "loss_basis_points",
    "rate_bytes_per_sec",
    "reorder_basis_points",
)


def run_model(executable: Path, profile: dict[str, object]) -> dict[str, object]:
    command = [str(executable), "--json"]
    for key, option in MODEL_ARGUMENTS.items():
        if key in profile:
            command.extend((option, str(profile[key])))
    completed = subprocess.run(
        command,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    report = json.loads(completed.stdout)
    if report.get("schema") != "worr.networking.impairment-baseline.v1":
        raise RuntimeError("unexpected impairment report schema")
    return report


def validate_report(profile: dict[str, object], report: dict[str, object]) -> None:
    name = str(profile["name"])
    if report["packets"] != profile.get("packets", 10000):
        raise RuntimeError(f"{name}: packet count mismatch")

    config = report["config"]
    feature_counters = {
        "loss_basis_points": "dropped",
        "burst_start_basis_points": "burst_dropped",
        "reorder_basis_points": "reordered",
        "duplicate_basis_points": "duplicated",
        "corrupt_basis_points": "corrupted",
        "rate_bytes_per_sec": "throttled",
        "upstream_stall_ms": "upstream_stalled",
    }
    for setting, counter in feature_counters.items():
        if int(profile.get(setting, 0)) > 0 and int(report[counter]) <= 0:
            raise RuntimeError(f"{name}: {setting} produced no {counter} events")

    if int(profile.get("reorder_basis_points", 0)) > 0:
        if int(report["release_inversions"]) <= 0:
            raise RuntimeError(f"{name}: reordering did not alter release order")
    if int(profile.get("duplicate_basis_points", 0)) > 0:
        if int(report["scheduled_copies"]) <= int(report["delivered"]):
            raise RuntimeError(f"{name}: duplication scheduled no extra copies")
    if int(report["scheduled_copies"]) != (
        int(report["delivered"]) + int(report["duplicated"])
    ):
        raise RuntimeError(f"{name}: scheduled-copy accounting mismatch")

    latency = int(profile.get("latency_ms", 0))
    no_variable_delay = not any(
        int(profile.get(key, 0))
        for key in (
            "jitter_ms",
            "upstream_stall_ms",
            "rate_bytes_per_sec",
            "reorder_basis_points",
        )
    )
    if no_variable_delay and int(report["delivered"]) > 0:
        if int(report["delay_min_ms"]) != latency:
            raise RuntimeError(f"{name}: exact latency floor mismatch")
        if int(report["delay_max_ms"]) != latency:
            raise RuntimeError(f"{name}: exact latency ceiling mismatch")

    if int(profile.get("jitter_ms", 0)) > 0:
        jitter = int(profile["jitter_ms"])
        if int(report["delay_min_ms"]) < max(0, latency - jitter):
            raise RuntimeError(f"{name}: jitter escaped lower bound")
        if int(report["delay_max_ms"]) > latency + jitter:
            raise RuntimeError(f"{name}: jitter escaped upper bound")

    for key, value in profile.items():
        if key in config and int(config[key]) != int(value):
            raise RuntimeError(f"{name}: model normalized {key} unexpectedly")


def stable_model_report(report: dict[str, object]) -> dict[str, object]:
    """Select only intentional cross-version regression contract fields."""
    try:
        stable = {key: report[key] for key in STABLE_MODEL_FIELDS}
        config = report["config"]
        if not isinstance(config, dict):
            raise TypeError("config is not an object")
        stable["config"] = {key: config[key] for key in STABLE_CONFIG_FIELDS}
    except (KeyError, TypeError) as error:
        raise RuntimeError(
            f"impairment report is missing a stable golden field: {error}"
        ) from error
    return stable


def build_golden(
    matrix_schema: object, profile_reports: list[dict[str, object]]
) -> dict[str, object]:
    return {
        "schema": GOLDEN_SCHEMA,
        "matrix_schema": matrix_schema,
        "profiles": [
            {
                "name": profile_report["name"],
                "model": stable_model_report(profile_report["model"]),
            }
            for profile_report in profile_reports
        ],
    }


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def verify_golden(path: Path, current: dict[str, object]) -> None:
    if not path.is_file():
        raise RuntimeError(
            f"golden impairment baseline is missing: {path}\n"
            "Create it only after reviewing deterministic output with --rebaseline."
        )

    expected = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(expected, dict) or expected.get("schema") != GOLDEN_SCHEMA:
        raise RuntimeError(f"unexpected impairment golden schema in {path}")
    if expected == current:
        return

    expected_lines = json.dumps(expected, indent=2, sort_keys=True).splitlines()
    current_lines = json.dumps(current, indent=2, sort_keys=True).splitlines()
    difference = "\n".join(
        difflib.unified_diff(
            expected_lines,
            current_lines,
            fromfile="checked-in-golden",
            tofile="current-model-output",
            lineterm="",
        )
    )
    raise RuntimeError(
        "deterministic impairment output drifted from the checked-in golden "
        f"baseline:\n{difference}\n"
        "Review the model/matrix change, then run this command with "
        "--rebaseline only when the new behavior is intentional."
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-exe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--scenarios",
        type=Path,
        default=Path(__file__).with_name("scenarios") / "impairment_matrix.json",
    )
    parser.add_argument(
        "--golden",
        type=Path,
        default=DEFAULT_GOLDEN,
        help="versioned expected model output (verified by default)",
    )
    parser.add_argument(
        "--rebaseline",
        action="store_true",
        help=(
            "replace --golden after all profiles pass validation and repeated "
            "determinism checks"
        ),
    )
    parser.add_argument("--repeat", type=int, default=3)
    args = parser.parse_args()

    if args.repeat < 2:
        parser.error("--repeat must be at least 2")

    matrix = json.loads(args.scenarios.read_text(encoding="utf-8"))
    if matrix.get("schema") != "worr.networking.impairment-matrix.v1":
        raise RuntimeError("unexpected impairment matrix schema")

    profile_reports: list[dict[str, object]] = []
    profile_names: set[str] = set()
    for profile in matrix.get("profiles", []):
        if not isinstance(profile, dict) or not profile.get("name"):
            raise RuntimeError("invalid unnamed impairment profile")
        name = str(profile["name"])
        if name in profile_names:
            raise RuntimeError(f"duplicate impairment profile name: {name}")
        profile_names.add(name)
        reports = [run_model(args.model_exe, profile) for _ in range(args.repeat)]
        canonical = reports[0]
        if any(report != canonical for report in reports[1:]):
            raise RuntimeError(
                f"{profile['name']}: impairment output was not deterministic"
            )
        validate_report(profile, canonical)
        profile_reports.append({"name": name, "model": canonical})

    if not profile_reports:
        raise RuntimeError("impairment matrix contains no profiles")

    golden = build_golden(matrix["schema"], profile_reports)
    if args.rebaseline:
        write_json(args.golden, golden)
        print(f"rebaselined {args.golden}")
    else:
        verify_golden(args.golden, golden)

    output = {
        "schema": "worr.networking.baseline-evidence.v1",
        "repeat_runs": args.repeat,
        "deterministic": True,
        "golden_schema": GOLDEN_SCHEMA,
        "golden_verified": True,
        "matrix_schema": matrix["schema"],
        "profiles": profile_reports,
    }
    write_json(args.output, output)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
