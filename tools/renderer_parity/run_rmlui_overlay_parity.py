#!/usr/bin/env python3
"""Capture and compare the guarded RmlUi overlay in native GL and Vulkan."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNTIME_CAPTURE = ROOT / "tools" / "ui_smoke" / "check_rmlui_runtime_capture.py"
COMPARE = ROOT / "tools" / "renderer_parity" / "compare_captures.py"
DEFAULT_MANIFEST = ROOT / "assets" / "renderer_parity" / "fr01_rmlui_overlay_manifest.json"
CAPTURE_ATTEMPTS = 2
CAPTURE_RETRY_DELAY_SECONDS = 1.0
DEFAULT_ROUTES = (
    "core.runtime_smoke",
    "main",
    "performance",
    "quit_confirm",
    "leave_match_confirm",
    "forfeit_confirm",
)
ROUTE_SCENES = {
    "core.runtime_smoke": "rmlui-runtime-overlay",
    "main": "rmlui-main-shell",
    "performance": "rmlui-performance-settings",
    "quit_confirm": "rmlui-quit-confirm-popup",
    "leave_match_confirm": "rmlui-leave-match-confirm-popup",
    "forfeit_confirm": "rmlui-forfeit-confirm-popup",
}


def capture_name(route_id: str) -> str:
    return "rmlui_" + route_id.replace(".", "_")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--capture-root", type=Path,
                        default=ROOT / ".tmp" / "renderer-parity" / "fr01-rmlui-overlay")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--geometry", default="960x720")
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--route-id", action="append", default=None,
                        help="guarded RmlUi route to compare; repeatable")
    return parser.parse_args(argv)


def run_capture(args: argparse.Namespace, renderer: str,
                route_id: str) -> list[subprocess.CompletedProcess[str]]:
    command = [
        sys.executable, str(RUNTIME_CAPTURE),
        "--install-dir", str(args.install_dir),
        "--renderer", renderer,
        "--evidence-dir", str(args.capture_root / renderer),
        "--evidence-id", capture_name(route_id),
        "--route-id", route_id,
        "--geometry", args.geometry,
        "--timeout", str(args.timeout),
        "--run", "--format", "json",
    ]
    environment = os.environ.copy()
    if renderer == "vulkan":
        environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
    results: list[subprocess.CompletedProcess[str]] = []
    for attempt in range(CAPTURE_ATTEMPTS):
        result = subprocess.run(
            command, cwd=ROOT, env=environment, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            creationflags=(getattr(subprocess, "CREATE_NO_WINDOW", 0)
                           if os.name == "nt" else 0), check=False,
        )
        results.append(result)
        if not result.returncode:
            break
        if attempt + 1 < CAPTURE_ATTEMPTS:
            time.sleep(CAPTURE_RETRY_DELAY_SECONDS)
    return results


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    args.install_dir = args.install_dir.resolve()
    args.capture_root = args.capture_root.resolve()
    args.manifest = args.manifest.resolve()
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")
    if not args.manifest.is_file():
        raise SystemExit(f"manifest not found: {args.manifest}")

    args.capture_root.mkdir(parents=True, exist_ok=True)
    routes = args.route_id or DEFAULT_ROUTES
    unknown_routes = set(routes) - set(ROUTE_SCENES)
    if unknown_routes:
        raise SystemExit(
            "unsupported route id(s): " + ", ".join(sorted(unknown_routes))
        )
    failures: list[str] = []
    for route_id in routes:
        for renderer in ("opengl", "vulkan"):
            results = run_capture(args, renderer, route_id)
            result = results[-1]
            log_path = args.capture_root / f"{renderer}-{capture_name(route_id)}-runner.log"
            log_path.write_text(
                "\n\n".join(
                    f"ATTEMPT {attempt + 1}/{len(results)}\nSTDOUT\n"
                    + attempt_result.stdout
                    + "\nSTDERR\n"
                    + attempt_result.stderr
                    for attempt, attempt_result in enumerate(results)
                ),
                encoding="utf-8",
            )
            if result.returncode:
                failures.append(
                    f"{renderer}/{route_id} guarded capture failed; see {log_path}"
                )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1

    compare_command = [
        sys.executable, str(COMPARE), "--manifest", str(args.manifest),
        "--capture-root", str(args.capture_root), "--json-output",
        str(args.capture_root / "comparison.json"),
    ]
    for route_id in routes:
        compare_command.extend(("--scene", ROUTE_SCENES[route_id]))
    compare = subprocess.run(
        compare_command,
        cwd=ROOT, stdin=subprocess.DEVNULL, text=True,
        creationflags=(getattr(subprocess, "CREATE_NO_WINDOW", 0)
                       if os.name == "nt" else 0), check=False,
    )
    return compare.returncode


if __name__ == "__main__":
    raise SystemExit(main())
