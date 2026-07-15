#!/usr/bin/env python3
"""Exercise native Vulkan debug primitives and telemetry in a real client."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops


ERROR_PATTERNS = (
    re.compile(r"\bVUID-[A-Za-z0-9_-]+"),
    re.compile(r"validation error", re.IGNORECASE),
    re.compile(r"device lost", re.IGNORECASE),
    re.compile(r"fatal error", re.IGNORECASE),
)


def discover_executable(install_dir: Path, override: Path | None) -> Path:
    if override:
        executable = override.resolve()
        if not executable.is_file():
            raise RuntimeError(f"executable not found: {executable}")
        return executable
    for name in ("worr_x86_64.exe", "worr.exe"):
        candidate = install_dir / name
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"no WORR client executable found under {install_dir}")


def image_difference(baseline: Path, overlay: Path) -> tuple[int, int]:
    with Image.open(baseline) as baseline_image, Image.open(overlay) as overlay_image:
        lhs = baseline_image.convert("RGB")
        rhs = overlay_image.convert("RGB")
        if lhs.size != rhs.size:
            raise RuntimeError(
                f"capture sizes differ: baseline={lhs.size}, overlay={rhs.size}"
            )
        difference = ImageChops.difference(lhs, rhs)
        histogram = difference.histogram()
        changed_channels = sum(histogram[1:256]) + sum(histogram[257:512]) + sum(
            histogram[513:768]
        )
        changed_pixels = sum(
            1 for pixel in difference.getdata() if pixel != (0, 0, 0)
        )
        max_channel_delta = max(
            (index % 256 for index, count in enumerate(histogram) if count),
            default=0,
        )
        if changed_channels == 0:
            changed_pixels = 0
        return changed_pixels, max_channel_delta


def evaluate_log(log: str) -> list[str]:
    failures: list[str] = []
    if not re.search(r"VK_DEBUG_TEST status=queued active_lines=[1-9][0-9]*", log):
        failures.append("missing successful VK_DEBUG_TEST queue record")
    if not re.search(r"VK_STATS frame=[1-9][0-9]* draws=[1-9][0-9]*", log):
        failures.append("missing populated VK_STATS record")
    if not re.search(r"VK_CAPS debug_lines=1 screenshot=1", log):
        failures.append("required Vulkan debug/screenshot capabilities are unavailable")
    for pattern in ERROR_PATTERNS:
        match = pattern.search(log)
        if match:
            failures.append(f"runtime log matched error pattern: {match.group(0)}")
    return failures


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, default=Path(".install"))
    parser.add_argument("--executable", type=Path)
    parser.add_argument(
        "--run-root", type=Path,
        default=Path(".tmp/renderer-parity/fr01-vk-debug"),
    )
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--vulkan-validation", action="store_true")
    parser.add_argument("--json-output", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        install_dir = args.install_dir.resolve()
        run_root = args.run_root.resolve()
        executable = discover_executable(install_dir, args.executable)
        home = run_root / "home"
        screenshots = home / "screenshots"
        screenshots.mkdir(parents=True, exist_ok=True)
        baseline = screenshots / "fr01_vk_debug_baseline.tga"
        overlay = screenshots / "fr01_vk_debug_overlay.tga"
        for capture in (baseline, overlay):
            if capture.exists():
                capture.unlink()

        command = [
            str(executable),
            "+set", "basedir", str(install_dir),
            "+set", "homedir", str(home),
            "+set", "game", "basew",
            "+set", "r_fullscreen", "0",
            "+set", "win_headless", "1",
            "+set", "in_enable", "0",
            "+set", "in_grab", "0",
            "+set", "r_geometry", "960x720",
            "+set", "developer", "1",
            "+set", "bot_enable", "0",
            "+set", "s_enable", "0",
            "+set", "ui_rml_enable", "0",
            "+set", "r_screenshot_dir", str(screenshots),
            "+set", "vid_renderer", "vulkan",
            "+set", "r_renderer", "vulkan",
            "+exec", "renderer_parity/fr01_vk_debug_overlay.cfg",
        ]
        environment = os.environ.copy()
        if args.vulkan_validation:
            environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
        completed = subprocess.run(
            command,
            cwd=install_dir,
            env=environment,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=args.timeout,
            check=False,
            creationflags=(getattr(subprocess, "CREATE_NO_WINDOW", 0)
                           if os.name == "nt" else 0),
        )
        log = "COMMAND\n" + subprocess.list2cmdline(command) + \
              "\n\nSTDOUT\n" + completed.stdout + \
              "\nSTDERR\n" + completed.stderr
        run_root.mkdir(parents=True, exist_ok=True)
        log_path = run_root / "process.log"
        log_path.write_text(log, encoding="utf-8")

        failures = evaluate_log(log)
        if completed.returncode != 0:
            failures.append(f"client exited with status {completed.returncode}")
        for capture in (baseline, overlay):
            if not capture.is_file():
                failures.append(f"missing capture: {capture}")

        changed_pixels = 0
        max_channel_delta = 0
        if baseline.is_file() and overlay.is_file():
            changed_pixels, max_channel_delta = image_difference(baseline, overlay)
            if changed_pixels < 100:
                failures.append(
                    f"debug overlay changed only {changed_pixels} pixels; expected at least 100"
                )
            if max_channel_delta < 32:
                failures.append(
                    f"debug overlay maximum channel delta is {max_channel_delta}; expected at least 32"
                )

        result = {
            "schema_version": 1,
            "passed": not failures,
            "exit_code": completed.returncode,
            "changed_pixels": changed_pixels,
            "max_channel_delta": max_channel_delta,
            "baseline": str(baseline),
            "overlay": str(overlay),
            "log": str(log_path),
            "failures": failures,
        }
        output = args.json_output.resolve() if args.json_output else run_root / "results.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if result["passed"] else 1
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"vk debug smoke: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
