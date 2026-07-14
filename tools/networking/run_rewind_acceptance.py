#!/usr/bin/env python3
"""Run and package deterministic player-rewind acceptance evidence."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


EVIDENCE_SCHEMA = "worr.networking.acceptance-evidence.v1"
RAW_SCHEMA = "worr.networking.rewind-acceptance-raw.v1"
PROBE_SCHEMA = "worr.networking.rewind-acceptance-probe.v1"
MATRIX_SCHEMA = "worr.networking.rewind-player-acceptance-matrix.v1"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_json(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def percentile(values: list[int], fraction: float) -> int:
    if not values:
        return 0
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int((len(ordered) * fraction) - 1e-12)))
    return ordered[index]


def git_value(root: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            ["git", *args], cwd=root, check=True, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        )
        return result.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def probe_once(executable: Path, case: dict[str, Any]) -> tuple[dict[str, Any], int]:
    command = [
        str(executable), "--scenario", case["scenario"],
        "--weapon-id", str(case["weapon_id"]),
        "--weapon-name", case["weapon_name"],
        "--latency-ms", str(case["latency_ms"]),
    ]
    started = time.perf_counter_ns()
    result = subprocess.run(
        command, check=False, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    elapsed = time.perf_counter_ns() - started
    if result.returncode != 0:
        raise RuntimeError(
            f"probe failed ({result.returncode}) for {case}: {result.stderr.strip()}"
        )
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError(f"probe emitted no JSON for {case}")
    report = json.loads(lines[-1])
    if report.get("schema") != PROBE_SCHEMA:
        raise RuntimeError(f"unexpected probe schema for {case}: {report.get('schema')}")
    return report, elapsed


def expectation_failures(
    report: dict[str, Any], expected: dict[str, Any], case_id: str
) -> list[str]:
    failures: list[str] = []
    if report.get("pass") is not True:
        failures.append(f"{case_id}: probe self-check failed")
    for key, value in expected.items():
        if report.get(key) != value:
            failures.append(
                f"{case_id}: {key} expected {value!r}, got {report.get(key)!r}"
            )
    if report.get("journal_count") != 1:
        failures.append(f"{case_id}: observation journal did not retain exactly one record")
    if report.get("authoritative_unchanged") is not True:
        failures.append(f"{case_id}: authoritative pose fingerprint changed")
    return failures


def build_cases(matrix: dict[str, Any]) -> list[dict[str, Any]]:
    cases: list[dict[str, Any]] = []
    normal_expectation = matrix["normal_expectation"]
    for weapon in matrix["weapon_policies"]:
        for latency in matrix["normal_latency_ms"]:
            expected = dict(normal_expectation)
            expected["query_reason"] = 0 if latency == 0 else 1
            expected["applied_time_us"] = 1_000_000 - (latency * 1_000)
            cases.append({
                "case_id": f"normal/{weapon['name']}/{latency}ms",
                "scenario": "normal",
                "weapon_id": weapon["id"],
                "weapon_name": weapon["name"],
                "latency_ms": latency,
                "expected": expected,
            })
    boundary_weapon = matrix["weapon_policies"][0]
    for scenario in matrix["boundary_scenarios"]:
        cases.append({
            "case_id": f"boundary/{scenario['name']}",
            "scenario": scenario["name"],
            "weapon_id": boundary_weapon["id"],
            "weapon_name": boundary_weapon["name"],
            "latency_ms": scenario["latency_ms"],
            "expected": scenario["expect"],
        })
    return cases


def route_tag_failures(root: Path, matrix: dict[str, Any]) -> list[str]:
    sources = [
        root / "src/game/sgame/gameplay/g_weapon.cpp",
        root / "src/game/sgame/player/p_weapon.cpp",
    ]
    text = "\n".join(path.read_text(encoding="utf-8") for path in sources)
    tokens = {
        "machinegun": "WORR_REWIND_WEAPON_MACHINEGUN",
        "chaingun": "WORR_REWIND_WEAPON_CHAINGUN",
        "shotgun": "WORR_REWIND_WEAPON_SHOTGUN",
        "super-shotgun": "WORR_REWIND_WEAPON_SUPER_SHOTGUN",
        "railgun": "WORR_REWIND_WEAPON_RAILGUN",
        "disruptor-convergence": "WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE",
        "plasma-beam": "WORR_REWIND_WEAPON_PLASMA_BEAM",
        "thunderbolt": "WORR_REWIND_WEAPON_THUNDERBOLT",
    }
    return [
        f"production route tag missing for {weapon['name']}"
        for weapon in matrix["weapon_policies"]
        if tokens[weapon["name"]] not in text
    ]


def matrix_contract_failures(matrix: dict[str, Any], case_count: int) -> list[str]:
    failures: list[str] = []
    weapon_ids = [weapon.get("id") for weapon in matrix.get("weapon_policies", [])]
    weapon_names = [weapon.get("name") for weapon in matrix.get("weapon_policies", [])]
    boundary_names = [item.get("name") for item in matrix.get("boundary_scenarios", [])]
    if weapon_ids != list(range(1, 9)) or len(set(weapon_names)) != 8:
        failures.append("matrix must retain the eight registered production weapon policies")
    if matrix.get("normal_latency_ms") != [0, 50, 100, 200]:
        failures.append("matrix must retain the 0/50/100/200 ms normal profiles")
    if boundary_names != [
        "stale", "future", "cap", "history_miss", "teleport",
        "death_respawn", "slot_reuse", "disabled",
    ]:
        failures.append("matrix boundary scenario contract changed")
    if case_count != 40:
        failures.append(f"matrix must expand to exactly 40 cases, got {case_count}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe-exe", type=Path, required=True)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--platform-id", default=sys.platform)
    parser.add_argument("--build-type", default="unknown")
    parser.add_argument("--compiler-id", default="unknown")
    parser.add_argument("--sgame-module", type=Path)
    args = parser.parse_args()
    if args.repeat < 2:
        parser.error("--repeat must be at least 2")

    root = Path(__file__).resolve().parents[2]
    probe = args.probe_exe.resolve()
    matrix_path = args.matrix.resolve()
    output = args.output.resolve()
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    if matrix.get("schema") != MATRIX_SCHEMA:
        raise RuntimeError(f"unexpected matrix schema: {matrix.get('schema')}")

    cases = build_cases(matrix)
    matrix_failures = matrix_contract_failures(matrix, len(cases))
    route_failures = route_tag_failures(root, matrix)
    failures = matrix_failures + route_failures
    raw_cases: list[dict[str, Any]] = []
    durations: list[int] = []
    determinism_mismatches = 0
    mutation_count = 0

    for case in cases:
        reports: list[dict[str, Any]] = []
        repeat_durations: list[int] = []
        for _ in range(args.repeat):
            report, elapsed = probe_once(probe, case)
            reports.append(report)
            repeat_durations.append(elapsed)
            durations.append(elapsed)
            if report.get("authoritative_unchanged") is not True:
                mutation_count += 1
        first_digest = sha256_json(reports[0])
        repeat_digests = [sha256_json(report) for report in reports]
        deterministic = all(digest == first_digest for digest in repeat_digests)
        if not deterministic:
            determinism_mismatches += 1
            failures.append(f"{case['case_id']}: repeat output mismatch")
        failures.extend(expectation_failures(reports[0], case["expected"], case["case_id"]))
        raw_cases.append({
            "case_id": case["case_id"],
            "input": {key: case[key] for key in ("scenario", "weapon_id", "weapon_name", "latency_ms")},
            "expected": case["expected"],
            "report": reports[0],
            "repeat_digests": repeat_digests,
            "repeat_duration_ns": repeat_durations,
            "deterministic": deterministic,
        })

    output.parent.mkdir(parents=True, exist_ok=True)
    raw_path = output.with_name("rewind-acceptance-raw.json")
    raw = {
        "schema": RAW_SCHEMA,
        "matrix_sha256": sha256_file(matrix_path),
        "probe_sha256": sha256_file(probe),
        "repeat": args.repeat,
        "cases": raw_cases,
    }
    raw_path.write_text(json.dumps(raw, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    source_paths = [
        root / "inc/common/net/rewind_observation.h",
        root / "src/common/net/rewind_observation.c",
        root / "src/game/sgame/network/lag_compensation.cpp",
        root / "src/game/sgame/network/lag_compensation.hpp",
        root / "src/game/sgame/gameplay/g_weapon.cpp",
        root / "src/game/sgame/player/p_weapon.cpp",
        root / "tools/networking/rewind_acceptance_probe.c",
        root / "tools/networking/run_rewind_acceptance.py",
        matrix_path,
    ]
    source_manifest = [
        {"path": str(path.relative_to(root)).replace("\\", "/"), "sha256": sha256_file(path)}
        for path in source_paths
    ]
    machine_fingerprint = sha256_json({
        "node": platform.node(), "system": platform.system(),
        "release": platform.release(), "machine": platform.machine(),
    })
    sgame_hash = None
    if args.sgame_module and args.sgame_module.exists():
        sgame_hash = sha256_file(args.sgame_module.resolve())
    revision = git_value(root, "rev-parse", "HEAD")
    dirty = git_value(root, "status", "--porcelain") not in ("", "unknown")
    run_id = dt.datetime.now(dt.timezone.utc).strftime("rewind-%Y%m%dT%H%M%SZ")
    all_passed = not failures and determinism_mismatches == 0 and mutation_count == 0
    evidence = {
        "schema": EVIDENCE_SCHEMA,
        "run_id": run_id,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "task_ids": matrix["task_ids"],
        "scope": matrix["scope"],
        "overall_result": "pass" if all_passed else "fail",
        "release_gate_complete": False,
        "source": {
            "revision": revision,
            "dirty_worktree": dirty,
            "files": source_manifest,
        },
        "platform": {
            "id": args.platform_id,
            "role": "server-game-production-core-probe",
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "machine_fingerprint_sha256": machine_fingerprint,
        },
        "build": {
            "type": args.build_type,
            "compiler": args.compiler_id,
            "probe_path": str(probe),
            "probe_sha256": sha256_file(probe),
            "sgame_module_sha256": sgame_hash,
        },
        "capabilities": {
            "rewind_abi_version": 1,
            "observation_schema_version": 1,
            "transport": "local-process-json",
            "transport_version": 1,
        },
        "scenario_manifest": {
            "schema": matrix["schema"],
            "path": str(matrix_path),
            "sha256": sha256_file(matrix_path),
            "seed": matrix["seed"],
        },
        "workload": {
            "mode": "deathmatch-player-bounds-core",
            "weapon_policy_count": len(matrix["weapon_policies"]),
            "normal_latency_ms": matrix["normal_latency_ms"],
            "boundary_scenarios": [item["name"] for item in matrix["boundary_scenarios"]],
            "case_count": len(cases),
            "repeat": args.repeat,
        },
        "measurements": {
            "invocations": len(durations),
            "duration_ns": {
                "min": min(durations), "median": int(statistics.median(durations)),
                "p50": percentile(durations, 0.50),
                "p95": percentile(durations, 0.95),
                "p99": percentile(durations, 0.99), "max": max(durations),
                "classification": "informational-subprocess-wall-time",
            },
            "determinism_mismatches": determinism_mismatches,
            "authoritative_mutations": mutation_count,
            "failed_assertions": len(failures),
        },
        "thresholds": {
            "minimum_repeat": 2,
            "expected_cases": 40,
            "maximum_determinism_mismatches": 0,
            "maximum_authoritative_mutations": 0,
            "maximum_failed_assertions": 0,
            "latency_is_release_gate": False,
        },
        "gates": {
            "matrix_complete": not matrix_failures,
            "production_weapon_route_tags_present": not route_failures,
            "repeat_determinism": determinism_mismatches == 0,
            "authoritative_state_immutable": mutation_count == 0,
            "expected_outcomes": not failures,
            "live_engine_weapon_damage_gate": False,
        },
        "artifacts": [{
            "kind": "raw-probe-results",
            "path": str(raw_path),
            "sha256": sha256_file(raw_path),
        }],
        "privacy": {
            "contains_player_names": False,
            "contains_network_addresses": False,
            "contains_packet_payloads": False,
        },
        "limitations": [
            matrix["declared_gap"],
            "Subprocess wall-time percentiles are informational and are not a performance release gate.",
            "A second machine/platform and a live client/server weapon-damage harness remain required before FR-10-T14 can close.",
        ],
        "failures": failures,
    }
    output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(output)
    return 0 if all_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
