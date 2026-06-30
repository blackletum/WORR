#!/usr/bin/env python3
"""Discover BSP maps that can close WORR q2aas/bot movement reference gaps."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import validate_worr_q2aas as validator  # noqa: E402

SCHEMA = "worr-q2aas-reference-candidate-discovery-v1"

HAZARD_CLASSES = {
    "trigger_hurt",
    "trigger_lava",
    "trigger_slime",
    "target_laser",
    "misc_lavaball",
}

CONTENTS_FLAGS = ("water", "slime", "lava", "ladder", "clip")
REASON_WEIGHTS = {
    "crouch_unknown": 1,
    "water_reference": 2,
    "door_reference": 2,
    "runtime_hazard_entity": 6,
    "slime_reference": 8,
    "lava_reference": 8,
}


def _flag_count(header: dict[str, Any], flag: str) -> int:
    return int(header.get("brush_contents", {}).get("flag_counts", {}).get(flag, 0) or 0)


def _entity_class_counts(entities: list[dict[str, str]]) -> Counter[str]:
    return Counter(entity.get("classname", "") for entity in entities if entity.get("classname"))


def candidate_from_metadata(path: Path, header: dict[str, Any], entities: list[dict[str, str]]) -> dict[str, Any]:
    """Build a scored reference-candidate row from parsed BSP metadata."""
    status = "candidate"
    diagnostics: list[str] = []
    if not header.get("valid_q2_bsp", False):
        status = "invalid"
        diagnostics.append("not a valid Quake II IBSP38 map")
    if header.get("lump_errors"):
        diagnostics.extend(str(error) for error in header.get("lump_errors", []))

    flag_counts = {flag: _flag_count(header, flag) for flag in CONTENTS_FLAGS}
    class_counts = _entity_class_counts(entities)
    hazard_entities = sum(class_counts.get(classname, 0) for classname in HAZARD_CLASSES)

    reasons: list[str] = []
    if flag_counts["water"] > 0:
        reasons.append("water_reference")
    if flag_counts["slime"] > 0:
        reasons.append("slime_reference")
    if flag_counts["lava"] > 0:
        reasons.append("lava_reference")
    if hazard_entities > 0:
        reasons.append("runtime_hazard_entity")
    if class_counts.get("func_door", 0) > 0 or class_counts.get("func_door_rotating", 0) > 0:
        reasons.append("door_reference")
    if status == "candidate":
        reasons.append("crouch_unknown")

    score = sum(REASON_WEIGHTS.get(reason, 0) for reason in reasons)
    score += min(flag_counts["slime"] + flag_counts["lava"], 64)
    score += min(hazard_entities * 2, 64)
    score += min(flag_counts["water"], 16)

    return {
        "path": str(path),
        "name": path.name,
        "status": status,
        "score": score if status == "candidate" else 0,
        "reasons": reasons,
        "diagnostics": diagnostics,
        "contents": flag_counts,
        "entities": {
            "total": len(entities),
            "hazard_entities": hazard_entities,
            "doors": class_counts.get("func_door", 0) + class_counts.get("func_door_rotating", 0),
            "teleporters": class_counts.get("trigger_teleport", 0) + class_counts.get("misc_teleporter", 0),
            "selected_class_counts": {
                classname: class_counts[classname]
                for classname in sorted(set(HAZARD_CLASSES) | {"func_door", "func_door_rotating", "trigger_teleport", "misc_teleporter"})
                if class_counts[classname]
            },
        },
    }


def inspect_candidate(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    header = validator.inspect_q2_bsp(path)
    entities = []
    if header.get("valid_q2_bsp") and not header.get("lump_errors"):
        entities = validator.parse_entity_lump(data, header.get("lumps", []))
    return candidate_from_metadata(path, header, entities)


def iter_bsp_files(roots: Iterable[Path], explicit_maps: Iterable[Path], max_files: int | None) -> list[Path]:
    seen: set[Path] = set()
    paths: list[Path] = []

    for map_path in explicit_maps:
        resolved = map_path.resolve()
        if resolved not in seen:
            seen.add(resolved)
            paths.append(resolved)

    for root in roots:
        root_path = root.resolve()
        if not root_path.exists():
            continue
        if root_path.is_file():
            candidates = [root_path]
        else:
            candidates = sorted(root_path.rglob("*.bsp"))
        for candidate in candidates:
            resolved = candidate.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            paths.append(resolved)
            if max_files is not None and len(paths) >= max_files:
                return paths

    return paths


def select_conversion_candidates(candidates: list[dict[str, Any]], count: int) -> list[dict[str, Any]]:
    if count <= 0:
        return []
    eligible = [candidate for candidate in candidates if candidate.get("status") == "candidate" and candidate.get("score", 0) > 0]
    return sorted(
        eligible,
        key=lambda row: (int(row.get("score", 0)), row.get("name", "")),
        reverse=True,
    )[:count]


def _safe_stem(path: Path) -> str:
    stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", path.stem).strip("._")
    digest = hashlib.sha1(str(path).encode("utf-8")).hexdigest()[:8]
    return f"{stem or 'map'}-{digest}"


def run_conversion(candidate: dict[str, Any], output_dir: Path) -> dict[str, Any]:
    map_path = Path(candidate["path"])
    conversion_dir = output_dir / "candidate-conversions" / _safe_stem(map_path)
    conversion_dir.mkdir(parents=True, exist_ok=True)
    report_json = conversion_dir / "report.json"
    command = [
        sys.executable,
        str(SCRIPT_DIR / "validate_worr_q2aas.py"),
        "--map",
        str(map_path),
        "--output",
        str(conversion_dir),
        "--report-json",
        str(report_json),
        "--require-reachability",
        "--require-q2-bsp",
        "--require-clean-bsp-lumps",
        "--require-spawn-coverage",
        "--require-item-coverage",
        "--write-aas-metadata",
    ]
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    result: dict[str, Any] = {
        "returncode": completed.returncode,
        "report_json": str(report_json),
        "output_dir": str(conversion_dir),
        "output_tail": "\n".join(completed.stdout.splitlines()[-20:]),
    }
    if report_json.exists():
        report = json.loads(report_json.read_text(encoding="utf-8"))
        maps = report.get("maps", [])
        if maps:
            map_report = maps[0]
            result.update(
                {
                    "metrics": map_report.get("metrics", {}),
                    "travel_counts": map_report.get("travel_counts", {}),
                    "feature_readiness": map_report.get("feature_readiness", {}),
                    "semantic_readiness": map_report.get("semantic_readiness", {}),
                    "diagnostics": map_report.get("diagnostics", {}),
                }
            )
    return result


def attach_conversion_results(candidates: list[dict[str, Any]], selected: list[dict[str, Any]], output_dir: Path) -> None:
    selected_paths = {candidate["path"] for candidate in selected}
    by_path = {candidate["path"]: candidate for candidate in candidates}
    for path in selected_paths:
        candidate = by_path[path]
        candidate["conversion"] = run_conversion(candidate, output_dir)


def summarize(candidates: list[dict[str, Any]], converted_count: int) -> dict[str, Any]:
    valid = [candidate for candidate in candidates if candidate.get("status") == "candidate"]
    converted = [candidate for candidate in candidates if candidate.get("conversion")]
    passed = [candidate for candidate in converted if candidate["conversion"].get("returncode") == 0]

    def top_for(reason: str) -> list[dict[str, Any]]:
        rows = [candidate for candidate in valid if reason in candidate.get("reasons", [])]
        return [
            {
                "name": candidate["name"],
                "path": candidate["path"],
                "score": candidate["score"],
                "contents": candidate.get("contents", {}),
                "entities": candidate.get("entities", {}),
                "conversion": candidate.get("conversion", {}),
            }
            for candidate in sorted(rows, key=lambda row: int(row.get("score", 0)), reverse=True)[:10]
        ]

    return {
        "scanned": len(candidates),
        "valid_q2_bsp": len(valid),
        "invalid_or_unreadable": len(candidates) - len(valid),
        "selected_for_conversion": converted_count,
        "conversion_passed": len(passed),
        "top": {
            "slime_reference": top_for("slime_reference"),
            "lava_reference": top_for("lava_reference"),
            "runtime_hazard_entity": top_for("runtime_hazard_entity"),
            "water_reference": top_for("water_reference"),
        },
    }


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# q2aas Reference Candidate Discovery",
        "",
        f"- Schema: `{report['schema']}`",
        f"- Scanned BSPs: {report['summary']['scanned']}",
        f"- Valid Quake II BSPs: {report['summary']['valid_q2_bsp']}",
        f"- Selected for conversion: {report['summary']['selected_for_conversion']}",
        f"- Successful conversions: {report['summary']['conversion_passed']}",
        "",
    ]
    for reason, rows in report["summary"]["top"].items():
        lines.append(f"## {reason}")
        if not rows:
            lines.extend(["", "_No candidates found._", ""])
            continue
        lines.extend(["", "| Map | Score | Water | Slime | Lava | Hazard Entities | Crouch Travels | Status |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |"])
        for row in rows:
            contents = row.get("contents", {})
            entities = row.get("entities", {})
            conversion = row.get("conversion", {})
            travel_counts = conversion.get("travel_counts", {})
            status = "converted" if conversion else "prefilter"
            if conversion and conversion.get("returncode") != 0:
                status = f"conversion failed ({conversion.get('returncode')})"
            lines.append(
                "| {name} | {score} | {water} | {slime} | {lava} | {hazards} | {crouch} | {status} |".format(
                    name=row["name"],
                    score=row["score"],
                    water=contents.get("water", 0),
                    slime=contents.get("slime", 0),
                    lava=contents.get("lava", 0),
                    hazards=entities.get("hazard_entities", 0),
                    crouch=travel_counts.get("crouch", 0),
                    status=status,
                )
            )
        lines.append("")
    return "\n".join(lines)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", action="append", default=[], type=Path, help="Directory or BSP file to scan")
    parser.add_argument("--map", action="append", default=[], type=Path, help="Explicit BSP file to include")
    parser.add_argument("--max-files", type=int, default=None, help="Maximum BSP files to inspect")
    parser.add_argument("--convert-top", type=int, default=0, help="Run q2aas validation for the top N candidates")
    parser.add_argument("--output", type=Path, default=Path(".tmp/q2aas/reference-candidates"), help="Output directory")
    parser.add_argument("--json-out", type=Path, default=None, help="JSON report path")
    parser.add_argument("--markdown-out", type=Path, default=None, help="Markdown report path")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    output_dir = args.output
    output_dir.mkdir(parents=True, exist_ok=True)

    paths = iter_bsp_files(args.root, args.map, args.max_files)
    candidates = [inspect_candidate(path) for path in paths]
    selected = select_conversion_candidates(candidates, args.convert_top)
    attach_conversion_results(candidates, selected, output_dir)

    report = {
        "schema": SCHEMA,
        "roots": [str(path) for path in args.root],
        "explicit_maps": [str(path) for path in args.map],
        "summary": summarize(candidates, len(selected)),
        "candidates": sorted(candidates, key=lambda row: (int(row.get("score", 0)), row.get("name", "")), reverse=True),
    }

    json_out = args.json_out or output_dir / "reference-candidates.json"
    markdown_out = args.markdown_out or output_dir / "reference-candidates.md"
    json_out.parent.mkdir(parents=True, exist_ok=True)
    markdown_out.parent.mkdir(parents=True, exist_ok=True)
    json_out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    markdown_out.write_text(render_markdown(report), encoding="utf-8")

    print(f"wrote {json_out}")
    print(f"wrote {markdown_out}")
    print(json.dumps(report["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
