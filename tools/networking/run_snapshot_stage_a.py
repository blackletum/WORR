#!/usr/bin/env python3
"""Build and repeat the isolated FR-10-T06 Stage A snapshot tests."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / ".tmp" / "networking" / "snapshot-stage-a"

SOURCES = (
    ROOT / "inc" / "shared" / "event_abi.h",
    ROOT / "inc" / "shared" / "prediction_abi.h",
    ROOT / "inc" / "shared" / "snapshot_abi.h",
    ROOT / "inc" / "common" / "net" / "snapshot_store.h",
    ROOT / "src" / "common" / "net" / "event_abi.c",
    ROOT / "src" / "common" / "net" / "snapshot_abi.c",
    ROOT / "src" / "common" / "net" / "snapshot_store.c",
    ROOT / "tools" / "networking" / "snapshot_store_test.c",
    ROOT / "tools" / "networking" / "snapshot_schema_layout_c.c",
    ROOT / "tools" / "networking" / "snapshot_schema_layout_cpp.cpp",
    ROOT / "tools" / "networking" / "run_snapshot_stage_a.py",
)


def run(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        rendered = " ".join(command)
        raise RuntimeError(
            f"command failed ({completed.returncode}): {rendered}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compiler_version(compiler: str) -> str:
    completed = run((compiler, "--version"))
    return completed.stdout.splitlines()[0]


def build(cc: str, cxx: str, output: Path) -> dict[str, list[str]]:
    include = str(ROOT / "inc")
    c_flags = [
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-fno-fast-math",
        "-ffp-contract=off",
        f"-I{include}",
    ]
    cxx_flags = [
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-fno-fast-math",
        "-ffp-contract=off",
        f"-I{include}",
    ]
    store_test = output / "snapshot_store_test.exe"
    layout_c = output / "snapshot_schema_layout_c.exe"
    layout_cpp = output / "snapshot_schema_layout_cpp.exe"
    commands = {
        "store": [
            cc,
            *c_flags,
            str(ROOT / "src" / "common" / "net" / "event_abi.c"),
            str(ROOT / "src" / "common" / "net" / "snapshot_abi.c"),
            str(ROOT / "src" / "common" / "net" / "snapshot_store.c"),
            str(ROOT / "tools" / "networking" / "snapshot_store_test.c"),
            "-o",
            str(store_test),
        ],
        "layout_c": [
            cc,
            *c_flags,
            str(ROOT / "tools" / "networking" / "snapshot_schema_layout_c.c"),
            "-o",
            str(layout_c),
        ],
        "layout_cpp": [
            cxx,
            *cxx_flags,
            str(
                ROOT
                / "tools"
                / "networking"
                / "snapshot_schema_layout_cpp.cpp"
            ),
            "-o",
            str(layout_cpp),
        ],
    }
    for command in commands.values():
        run(command)
    return commands


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    if args.repeat < 1:
        parser.error("--repeat must be positive")
    cc = shutil.which(args.cc)
    cxx = shutil.which(args.cxx)
    if not cc or not cxx:
        parser.error("requested C/C++ compiler was not found")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    commands = build(cc, cxx, output)
    executables = {
        "store": output / "snapshot_store_test.exe",
        "layout_c": output / "snapshot_schema_layout_c.exe",
        "layout_cpp": output / "snapshot_schema_layout_cpp.exe",
    }

    repetitions: list[dict[str, str]] = []
    golden_output: str | None = None
    for index in range(args.repeat):
        layout_c = run((str(executables["layout_c"]),))
        layout_cpp = run((str(executables["layout_cpp"]),))
        store = run((str(executables["store"]),))
        golden = run((str(executables["store"]), "--print-golden"))
        if golden_output is None:
            golden_output = golden.stdout
        elif golden.stdout != golden_output:
            raise RuntimeError("snapshot golden output changed between runs")
        repetitions.append(
            {
                "index": str(index + 1),
                "layout_c_stdout": layout_c.stdout.strip(),
                "layout_cpp_stdout": layout_cpp.stdout.strip(),
                "store_stdout": store.stdout.strip(),
                "golden_sha256": hashlib.sha256(
                    golden.stdout.encode("utf-8")
                ).hexdigest(),
            }
        )

    report = {
        "schema": "worr.snapshot-stage-a.evidence.v1",
        "task": "FR-10-T06",
        "scope": "stage-a-only",
        "complete_task_claim": False,
        "compilers": {
            "c": compiler_version(cc),
            "cxx": compiler_version(cxx),
        },
        "commands": commands,
        "source_sha256": {
            str(path.relative_to(ROOT)).replace("\\", "/"): digest(path)
            for path in SOURCES
        },
        "repeat": args.repeat,
        "accepted_snapshots_per_store_run": 100000,
        "repetitions": repetitions,
        "golden_output": (golden_output or "").splitlines(),
    }
    report_path = output / "snapshot-stage-a-report.json"
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(report_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
