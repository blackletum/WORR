#!/usr/bin/env python3
"""Run and qualify the deterministic offline FR-10-T06 parity corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: top-level JSON value must be an object")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_probe(probe: Path, manifest: dict[str, Any]) -> dict[str, Any]:
    command = [
        str(probe),
        "--snapshots",
        str(manifest["snapshot_count"]),
        "--seed",
        str(manifest["seed"]),
        "--retention-slots",
        str(manifest["retention_slots"]),
        "--boundary-cases",
        str(manifest["wire_sequence_boundary_cases"]),
    ]
    completed = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        # The 100k-snapshot parity probe runs ~98 s idle but is short enough
        # that host contention easily doubles it (measured 180-215 s under a
        # busy machine).  Size the guard for a loaded host; the probe is
        # deterministic and a genuine hang is still caught well under this.
        timeout=480,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"probe failed with exit code {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(
            f"probe did not emit one JSON document: {error}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        ) from error
    if not isinstance(result, dict):
        raise RuntimeError("probe JSON must be an object")
    return result


def require_integer(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer")
    return value


def validate_manifest(manifest: dict[str, Any]) -> None:
    if manifest.get("schema_version") != 1:
        raise ValueError("manifest schema_version must be 1")
    if manifest.get("classification") != "offline_deterministic_parity_corpus":
        raise ValueError("manifest must retain the offline corpus classification")
    if require_integer(manifest.get("snapshot_count"), "snapshot_count") != 100000:
        raise ValueError("the acceptance corpus must contain exactly 100,000 snapshots")
    if require_integer(manifest.get("repeat"), "repeat") < 2:
        raise ValueError("repeat must be at least 2 to prove deterministic output")
    for key in (
        "seed",
        "retention_slots",
        "wire_sequence_boundary_cases",
        "required_coverage",
        "required_exact_counts",
        "wire_sequence_domain",
    ):
        if key not in manifest:
            raise ValueError(f"manifest is missing {key}")


def validate_result(result: dict[str, Any], manifest: dict[str, Any]) -> None:
    if result.get("schema_version") != 1:
        raise ValueError("probe schema_version must be 1")
    if result.get("classification") != manifest["classification"]:
        raise ValueError("probe classification differs from manifest")
    if result.get("snapshot_count") != manifest["snapshot_count"]:
        raise ValueError("probe snapshot_count differs from manifest")
    if result.get("seed") != manifest["seed"]:
        raise ValueError("probe seed differs from manifest")
    if result.get("retention_slots") != manifest["retention_slots"]:
        raise ValueError("probe retention_slots differs from manifest")
    if not isinstance(result.get("corpus_digest"), str) or len(result["corpus_digest"]) != 16:
        raise ValueError("probe corpus_digest must be a 16-character hexadecimal string")

    coverage = result.get("coverage")
    if not isinstance(coverage, dict):
        raise ValueError("probe coverage must be an object")
    for key, minimum_value in manifest["required_coverage"].items():
        actual = require_integer(coverage.get(key), f"coverage.{key}")
        if actual < require_integer(minimum_value, f"required_coverage.{key}"):
            raise ValueError(
                f"coverage.{key}={actual} is below required minimum {minimum_value}"
            )
    for key, expected_value in manifest["required_exact_counts"].items():
        actual = require_integer(coverage.get(key), f"coverage.{key}")
        if actual != require_integer(expected_value, f"required_exact_counts.{key}"):
            raise ValueError(
                f"coverage.{key}={actual} differs from exact requirement {expected_value}"
            )

    wire_domain = result.get("wire_sequence_domain")
    if not isinstance(wire_domain, dict):
        raise ValueError("probe wire_sequence_domain must be an object")
    for key in (
        "minimum_serverframe",
        "maximum_serverframe",
        "maximum_snapshot_sequence",
        "wrap_supported",
    ):
        if wire_domain.get(key) != manifest["wire_sequence_domain"].get(key):
            raise ValueError(f"probe wire_sequence_domain.{key} differs from manifest")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe-exe", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--platform-id", required=True)
    parser.add_argument("--build-type", required=True)
    parser.add_argument("--compiler-id", required=True)
    args = parser.parse_args()

    probe = args.probe_exe.resolve()
    manifest_path = args.manifest.resolve()
    if not probe.is_file():
        raise FileNotFoundError(f"probe executable not found: {probe}")
    manifest = load_json(manifest_path)
    validate_manifest(manifest)

    runs: list[dict[str, Any]] = []
    for _ in range(manifest["repeat"]):
        result = run_probe(probe, manifest)
        validate_result(result, manifest)
        runs.append(result)
    if any(run != runs[0] for run in runs[1:]):
        raise ValueError("repeated corpus runs produced different JSON evidence")

    evidence = {
        "schema_version": 1,
        "classification": "offline_deterministic_parity_corpus",
        "live_acceptance": False,
        "project_task": "FR-10-T06",
        "manifest_name": manifest["name"],
        "manifest_sha256": sha256(manifest_path),
        "probe_sha256": sha256(probe),
        "platform_id": args.platform_id,
        "build_type": args.build_type,
        "compiler_id": args.compiler_id,
        "repeat_count": manifest["repeat"],
        "repeatable": True,
        "result": runs[0],
        "limitations": [
            "Uses public q2proto/canonical APIs without serializing datagrams.",
            "Does not execute the live client parser, cgame consumer, packet scheduler, or renderer.",
            "Legacy wire snapshot identifiers do not wrap in their public signed-int32 domain; the corpus reaches the maximum boundary and reports zero wrap cases.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "snapshot final-emission/projector parity: "
        f"{runs[0]['snapshot_count']} snapshots, "
        f"digest {runs[0]['corpus_digest']}, "
        f"{manifest['repeat']} identical runs"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
