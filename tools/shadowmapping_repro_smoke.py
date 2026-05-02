#!/usr/bin/env python3
"""Run repeatable shadowmapping smoke scenes against the staged install."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from typing import Iterable


FILTERS = {
    "pcf": 1,
    "vsm": 2,
    "evsm": 3,
    "pcss": 4,
}


SCENES = {
    "off-pvs-light": ["base1"],
    "moving-bmodel": ["base2"],
    "translated-md2": ["q2dm1"],
    "projectile-self-shadow": ["q2dm1"],
    "sun-cascade": ["q2dm1"],
    "hom-regression": ["base1"],
    "flashlight-owner": ["test/base1_flashlight"],
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def default_exe(install_dir: pathlib.Path) -> pathlib.Path:
    exe = install_dir / "worr_x86_64.exe"
    if exe.exists():
        return exe
    return install_dir / "worr_x86_64"


def iter_jobs(args: argparse.Namespace) -> Iterable[tuple[str, str, str, int]]:
    scenes = args.scene or sorted(SCENES)
    filters = args.filter or ["pcf"]
    for renderer in args.renderer:
        for filter_name in filters:
            filter_value = FILTERS[filter_name]
            for scene in scenes:
                for map_name in SCENES[scene]:
                    yield renderer, scene, map_name, filter_value


def build_command(args: argparse.Namespace, renderer: str, scene: str,
                  map_name: str, filter_value: int) -> list[str]:
    log_name = f"shadow_{renderer}_{scene}_f{filter_value}"
    command = [
        str(args.exe),
        "+set", "basedir", str(args.install_dir),
        "+set", "r_renderer", renderer,
        "+set", "vid_fullscreen", "0",
        "+set", "logfile", "1",
        "+set", "logfile_flush", "1",
        "+set", "logfile_name", log_name,
        "+set", "r_shadowmaps", "1",
        "+set", "r_shadow_filter", str(filter_value),
        "+set", "r_shadow_sun", "1" if scene == "sun-cascade" else "0",
        "+set", "r_shadow_draw_debug", str(args.debug_draw),
    ]
    if scene == "flashlight-owner":
        command += [
            "+set", "cl_shadowlights", "1",
            "+set", "cheats", "1",
        ]
    command += [
        "+map", map_name,
    ]
    if scene == "flashlight-owner":
        command += [
            "+wait", "60",
            "+give", "item_flashlight",
            "+use", "Flashlight",
        ]
    command += [
        "+wait", str(args.wait),
        "+r_shadow_dump",
        "+quit",
    ]
    return command


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run WORR shadowmapping repro/smoke scenes from .install.")
    parser.add_argument("--install-dir", default=".install",
                        help="Staged install root")
    parser.add_argument("--exe", help="Client executable path")
    parser.add_argument("--renderer", action="append",
                        choices=("opengl", "vulkan"),
                        default=None,
                        help="Renderer to test; repeatable")
    parser.add_argument("--scene", action="append", choices=sorted(SCENES),
                        help="Scene family to run; repeatable")
    parser.add_argument("--filter", action="append", choices=sorted(FILTERS),
                        help="Shadow filter family; repeatable")
    parser.add_argument("--wait", type=int, default=180,
                        help="Frames to wait after map load before dump/quit")
    parser.add_argument("--debug-draw", type=int, default=0,
                        help="r_shadow_draw_debug bitmask for overlay repros")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print commands without launching")
    args = parser.parse_args()

    args.install_dir = pathlib.Path(args.install_dir).resolve()
    args.exe = pathlib.Path(args.exe).resolve() if args.exe else default_exe(args.install_dir)
    args.renderer = args.renderer or ["opengl", "vulkan"]

    if not args.exe.exists() and not args.dry_run:
        parser.error(f"client executable not found: {args.exe}")

    cwd = repo_root()
    failed = 0
    for renderer, scene, map_name, filter_value in iter_jobs(args):
        command = build_command(args, renderer, scene, map_name, filter_value)
        print(" ".join(command), flush=True)
        if args.dry_run:
            continue
        result = subprocess.run(command, cwd=cwd)
        if result.returncode != 0:
            failed += 1
            print(f"FAILED {renderer}/{scene}/{map_name}/filter={filter_value}: "
                  f"{result.returncode}", file=sys.stderr)

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
