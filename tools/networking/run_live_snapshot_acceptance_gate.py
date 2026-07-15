#!/usr/bin/env python3
"""Run a target-count live canonical snapshot acceptance gate.

The gate uses a real local client/server session.  It advances client physics at
high frequency while rendering at low frequency, so canonical snapshot
projection, publication, legacy comparison, and consumer acceptance still run
through their production engine paths without making the renderer the limiting
factor.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import run_staged_impairment_smoke as smoke


WAIT_LIMIT = 1000
DEFAULT_SCHEDULE_PERCENT = 150
# Deterministic impairment can spend several hundred high-frequency physics
# frames completing the loopback handshake before snapshot production begins.
# The allowance is deliberately generous; the parsed terminal count, not this
# schedule estimate, remains the acceptance condition.
DEFAULT_STARTUP_FRAMES = 2048
# Snapshot acceptance is deliberately accelerated relative to wall time.  A
# wall-clock bandwidth shaper therefore accumulates an artificial, unbounded
# backlog during long target-count runs even when the simulated link rate is
# healthy.  Keep rate shaping disabled in this gate; the staged impairment
# smoke retains its live 1,024-kbps configuration, while deterministic model
# tests prove actual throttle scheduling.  Loss, burst loss, latency, jitter,
# reordering, duplication, and upstream stalls remain active here.
LIVE_IMPAIR_RATE_KBPS = 0

IMPAIRED_REQUIRED_COUNTERS = (
    "seen",
    "dropped",
    "reordered",
    "duplicated",
    "upstream_stalled",
)
RAW_ROUTE_COUNTERS = IMPAIRED_REQUIRED_COUNTERS + (
    "throttled",
    "overflow",
)

WINDOWS_RUNTIME_COMPONENTS = (
    ("engine", Path("worr_engine_x86_64.dll")),
    ("cgame", Path("basew/cgame_x86_64.dll")),
    ("sgame", Path("basew/sgame_x86_64.dll")),
    ("renderer", Path("worr_opengl_x86_64.dll")),
)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def runtime_component_manifest(
    executable: Path,
    working_dir: Path,
) -> list[dict[str, object]]:
    """Hash the exact staged modules that participate in the live gate."""
    components = [("client", executable)]
    if executable.suffix.lower() == ".exe":
        components.extend(
            (role, working_dir / relative)
            for role, relative in WINDOWS_RUNTIME_COMPONENTS
        )
    else:
        raise RuntimeError(
            "live snapshot build manifest has no component map for "
            f"{executable.name!r}"
        )

    manifest: list[dict[str, object]] = []
    for role, path in components:
        resolved = path.resolve()
        if not resolved.is_file():
            raise FileNotFoundError(
                f"required live snapshot component is missing: {resolved}"
            )
        before = resolved.stat()
        sha256 = file_sha256(resolved)
        after = resolved.stat()
        if (
            before.st_size != after.st_size
            or before.st_mtime_ns != after.st_mtime_ns
        ):
            raise RuntimeError(
                "live snapshot component changed while it was hashed: "
                f"{resolved}"
            )
        manifest.append(
            {
                "role": role,
                "path": str(resolved),
                "bytes": after.st_size,
                "mtime_ns": after.st_mtime_ns,
                "sha256": sha256,
            }
        )
    return manifest


def manifest_sha256(
    manifest: list[dict[str, object]], role: str
) -> str | None:
    for component in manifest:
        if component.get("role") == role:
            value = component.get("sha256")
            return value if isinstance(value, str) else None
    return None


def write_json_atomic(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def invalidate_previous_outputs(output: Path) -> Path:
    failure_output = output.with_suffix(".failure.json")
    output.unlink(missing_ok=True)
    failure_output.unlink(missing_ok=True)
    return failure_output


def artifact_manifest(root: Path) -> list[dict[str, object]]:
    artifacts: list[dict[str, object]] = []
    if not root.exists():
        return artifacts
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        artifacts.append(
            {
                "path": str(path),
                "bytes": path.stat().st_size,
                "sha256": file_sha256(path),
            }
        )
    return artifacts


def safe_artifact_manifest(
    root: Path,
) -> tuple[list[dict[str, object]], str | None]:
    try:
        return artifact_manifest(root), None
    except Exception as error:  # Preserve the primary gate failure.
        return [], f"{type(error).__name__}: {error}"


def wait_commands(frame_count: int) -> list[str]:
    """Return command-line ``wait`` chunks accepted by Cmd_Wait_f."""
    if frame_count <= 0:
        raise ValueError("frame_count must be positive")

    commands: list[str] = []
    remaining = frame_count
    while remaining:
        chunk = min(remaining, WAIT_LIMIT)
        commands.extend(("+wait", str(chunk)))
        remaining -= chunk
    return commands


def client_command(
    executable: Path,
    *,
    impaired: bool,
    target: int,
    schedule_percent: int = DEFAULT_SCHEDULE_PERCENT,
    startup_frames: int = DEFAULT_STARTUP_FRAMES,
    completion_marker: str = "worr_live_snapshot_gate_complete",
) -> tuple[list[str], int]:
    if target <= 0:
        raise ValueError("target must be positive")
    if schedule_percent < 100:
        raise ValueError("schedule_percent must be at least 100")
    if startup_frames < 0:
        raise ValueError("startup_frames must not be negative")
    if not completion_marker or any(
        character.isspace() or character in ";\""
        for character in completion_marker
    ):
        raise ValueError("completion_marker must be a single safe token")

    scaled_target = (
        target * schedule_percent + 99
    ) // 100
    frame_count = scaled_target + startup_frames
    command = [
        str(executable),
        "+set", "game", "basew",
        "+set", "developer", "1",
        # This is a real client/server session, but the client must be a
        # hidden automation surface and must not initialize mouse input.
        "+set", "win_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "r_renderer", "opengl",
        "+set", "r_fullscreen", "0",
        "+set", "r_geometry", "640x480+0+0",
        "+set", "gl_swapinterval", "0",
        "+set", "cl_async", "1",
        "+set", "cl_maxfps", "1000",
        "+set", "r_maxfps", "10",
        "+set", "fixedtime", "25",
        "+set", "net_dropsim", "0",
        "+set", "net_impair_enable", "1" if impaired else "0",
        "+set", "cl_adaptive_input", "1" if impaired else "0",
        "+set", "cl_snapshot_recovery", "1" if impaired else "0",
    ]
    if impaired:
        command.extend(
            [
                "+set", "net_impair_seed", "424242",
                "+set", "net_impair_latency_ms", "25",
                "+set", "net_impair_jitter_ms", "5",
                "+set", "net_impair_loss_pct", "1",
                "+set", "net_impair_burst_loss_pct", "0.1",
                "+set", "net_impair_burst_length", "3",
                "+set", "net_impair_reorder_pct", "0.5",
                "+set", "net_impair_duplicate_pct", "0.5",
                "+set", "net_impair_upstream_stall_ms", "20",
                "+set", "net_impair_rate_kbps", str(LIVE_IMPAIR_RATE_KBPS),
            ]
        )

    command.extend(("+map", "mm-rage"))
    command.extend(wait_commands(frame_count))
    command.extend(
        (
            "+cl_adaptive_input_status",
            "+cl_snapshot_shadow_status",
            "+cl_snapshot_recovery_status",
            "+net_impair_status",
            "+echo", completion_marker,
            # Keep the process alive until the harness has flushed and parsed
            # the terminal status block.
            "+wait", "1000",
        )
    )
    return command, frame_count


def validate_target(
    telemetry: dict[str, int],
    *,
    target: int,
) -> None:
    for name in (
        "attempts",
        "projected",
        "published",
        "promotion_eligible",
        "comparisons",
        "consumer_attempts",
        "consumer_accepts",
    ):
        if telemetry[name] < target:
            raise RuntimeError(
                f"live snapshot target missed: {name}={telemetry[name]} "
                f"target={target}"
            )

    expected = telemetry["attempts"]
    for name in (
        "projected",
        "published",
        "promotion_eligible",
        "comparisons",
        "consumer_attempts",
        "consumer_accepts",
    ):
        if telemetry[name] != expected:
            raise RuntimeError(
                "live snapshot pipeline did not retain every attempted frame: "
                f"attempts={expected} {name}={telemetry[name]}"
            )


def validate_impairment_counters(
    counters: dict[str, int],
    *,
    impaired: bool,
) -> None:
    if impaired:
        for counter in IMPAIRED_REQUIRED_COUNTERS:
            if counters[counter] <= 0:
                raise RuntimeError(
                    "impaired live acceptance profile produced no "
                    f"{counter} events"
                )
        if counters["overflow"] != 0:
            raise RuntimeError(
                "impaired live acceptance profile overflowed the packet queue"
            )
        if counters["throttled"] != 0:
            raise RuntimeError(
                "accelerated live acceptance profile unexpectedly enabled "
                "wall-clock rate shaping"
            )
        return

    if any(counters[name] != 0 for name in RAW_ROUTE_COUNTERS):
        raise RuntimeError(
            "clean live acceptance profile did not preserve raw routing"
        )


def run_profile(
    *,
    executable: Path,
    working_dir: Path,
    output_stem: Path,
    impaired: bool,
    target: int,
    schedule_percent: int,
    startup_frames: int,
    timeout: float,
    creation_flags: int,
    run_id: str,
    build_manifest: list[dict[str, object]],
) -> dict[str, object]:
    profile_name = "impaired" if impaired else "clean"
    stdout_path = output_stem.with_suffix(f".{profile_name}.stdout.log")
    stderr_path = output_stem.with_suffix(f".{profile_name}.stderr.log")
    completion_marker = (
        f"worr_live_snapshot_gate_complete_{run_id}_{profile_name}"
    )
    command, frame_count = client_command(
        executable,
        impaired=impaired,
        target=target,
        schedule_percent=schedule_percent,
        startup_frames=startup_frames,
        completion_marker=completion_marker,
    )

    started = time.monotonic()
    text, stderr_text, counters, terminated = smoke.run_client(
        command,
        working_dir,
        stdout_path,
        stderr_path,
        timeout,
        creation_flags,
        completion_marker=completion_marker,
        failure_markers=(
            "invalid canonical command stream",
            "was dropped:",
            "Server disconnected",
        ),
    )
    elapsed = time.monotonic() - started

    post_run_manifest = runtime_component_manifest(executable, working_dir)
    if post_run_manifest != build_manifest:
        raise RuntimeError(
            "staged runtime components changed during the live profile"
        )

    smoke.validate_common(text, stderr_text)
    adaptive, recovery, adaptive_telemetry, shadow, shadow_telemetry = (
        smoke.validate_adapter_status(text, enabled=impaired)
    )
    validate_target(shadow_telemetry, target=target)

    validate_impairment_counters(counters, impaired=impaired)

    return {
        "profile": profile_name,
        "target": target,
        "scheduled_physics_frames": frame_count,
        "schedule_percent": schedule_percent,
        "startup_frames": startup_frames,
        "elapsed_seconds": round(elapsed, 3),
        "acceleration": {
            "cl_async": 1,
            "cl_maxfps": 1000,
            "r_maxfps": 10,
            "fixedtime_ms": 25,
        },
        "impairment_rate_kbps": LIVE_IMPAIR_RATE_KBPS if impaired else 0,
        "counters": counters,
        "adaptive_input": adaptive,
        "adaptive_input_telemetry": adaptive_telemetry,
        "snapshot_recovery": recovery,
        "snapshot_shadow": shadow,
        "snapshot_shadow_telemetry": shadow_telemetry,
        "terminated_by_harness": terminated,
        "completion_marker": completion_marker,
        "command": command,
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "stdout_sha256": file_sha256(stdout_path),
        "stderr_sha256": file_sha256(stderr_path),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client-exe", required=True, type=Path)
    parser.add_argument("--working-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target", type=int, default=100_000)
    parser.add_argument(
        "--profile",
        choices=("clean", "impaired", "both"),
        default="impaired",
    )
    parser.add_argument(
        "--schedule-percent", type=int, default=DEFAULT_SCHEDULE_PERCENT
    )
    parser.add_argument(
        "--startup-frames", type=int, default=DEFAULT_STARTUP_FRAMES
    )
    parser.add_argument("--timeout", type=float, default=1800.0)
    args = parser.parse_args()

    if args.target <= 0:
        parser.error("--target must be positive")
    if args.schedule_percent < 100:
        parser.error("--schedule-percent must be at least 100")
    if args.startup_frames < 0:
        parser.error("--startup-frames must not be negative")

    args.output = args.output.resolve()
    args.client_exe = args.client_exe.resolve()
    args.working_dir = args.working_dir.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    failure_output = invalidate_previous_outputs(args.output)
    started_at = datetime.now(timezone.utc)
    run_id = (
        started_at.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    )
    run_root = (
        args.output.parent / f"{args.output.stem}.runs" / run_id
    )
    run_root.mkdir(parents=True, exist_ok=False)
    output_stem = run_root / args.output.stem
    creation_flags = subprocess_creation_flags()
    selected = (
        (False, True)
        if args.profile == "both"
        else (args.profile == "impaired",)
    )

    profiles: list[dict[str, object]] = []
    active_impaired: bool | None = None
    active_build_manifest: list[dict[str, object]] = []
    reference_build_manifest: list[dict[str, object]] | None = None
    try:
        for impaired in selected:
            active_impaired = impaired
            active_build_manifest = runtime_component_manifest(
                args.client_exe, args.working_dir
            )
            if reference_build_manifest is None:
                reference_build_manifest = active_build_manifest
            elif active_build_manifest != reference_build_manifest:
                raise RuntimeError(
                    "staged runtime changed between acceptance profiles"
                )
            profiles.append(
                run_profile(
                    executable=args.client_exe,
                    working_dir=args.working_dir,
                    output_stem=output_stem,
                    impaired=impaired,
                    target=args.target,
                    schedule_percent=args.schedule_percent,
                    startup_frames=args.startup_frames,
                    timeout=args.timeout,
                    creation_flags=creation_flags,
                    run_id=run_id,
                    build_manifest=active_build_manifest,
                )
            )

        if reference_build_manifest is None:
            raise RuntimeError("live snapshot gate selected no profiles")
        success_artifacts = artifact_manifest(run_root)
        report = {
            "schema": "worr.networking.live-snapshot-acceptance.v1",
            "run_id": run_id,
            "started_at_utc": started_at.isoformat(),
            "completed_at_utc": datetime.now(timezone.utc).isoformat(),
            "connected_protocol": 1038,
            "game_api": 2025,
            "map": "mm-rage",
            "target": args.target,
            "client_executable": str(args.client_exe),
            "client_sha256": manifest_sha256(
                reference_build_manifest, "client"
            ),
            "build_manifest_captured_before_launch":
                reference_build_manifest,
            "working_directory": str(args.working_dir),
            "profiles": profiles,
            "artifacts": success_artifacts,
        }
        # Both writes are part of the acceptance transaction.  Any failure
        # enters the failure-manifest path while the canonical pass pointer
        # remains absent or atomically complete.
        write_json_atomic(run_root / "report.json", report)
        write_json_atomic(args.output, report)
    except Exception as error:
        if active_impaired is None:
            active_profile = "not-started"
        else:
            active_profile = "impaired" if active_impaired else "clean"
        completion_marker = (
            f"worr_live_snapshot_gate_complete_{run_id}_{active_profile}"
        )
        failed_command: list[str] | None = None
        failed_command_error: str | None = None
        if active_impaired is not None:
            try:
                failed_command, _ = client_command(
                    args.client_exe,
                    impaired=active_impaired,
                    target=args.target,
                    schedule_percent=args.schedule_percent,
                    startup_frames=args.startup_frames,
                    completion_marker=completion_marker,
                )
            except Exception as command_error:
                failed_command_error = (
                    f"{type(command_error).__name__}: {command_error}"
                )
        failure_artifacts, artifact_error = safe_artifact_manifest(run_root)
        failure = {
            "schema": "worr.networking.live-snapshot-acceptance-failure.v1",
            "run_id": run_id,
            "started_at_utc": started_at.isoformat(),
            "failed_at_utc": datetime.now(timezone.utc).isoformat(),
            "requested_output": str(args.output),
            "requested_output_invalidated": not args.output.exists(),
            "target": args.target,
            "profile_selection": args.profile,
            "active_profile": active_profile,
            "schedule_percent": args.schedule_percent,
            "startup_frames": args.startup_frames,
            "timeout_seconds": args.timeout,
            "client_executable": str(args.client_exe),
            "client_sha256": manifest_sha256(
                active_build_manifest, "client"
            ),
            "build_manifest_captured_before_launch":
                active_build_manifest,
            "working_directory": str(args.working_dir),
            "argv": sys.argv,
            "active_command": failed_command,
            "active_command_error": failed_command_error,
            "completed_profiles": profiles,
            "error_type": type(error).__name__,
            "error": str(error),
            "artifacts": failure_artifacts,
            "artifact_manifest_error": artifact_error,
        }
        write_json_atomic(run_root / "failure.json", failure)
        write_json_atomic(failure_output, failure)
        raise

    print(args.output)
    return 0


def subprocess_creation_flags() -> int:
    return 0x08000000 if os.name == "nt" else 0


if __name__ == "__main__":
    raise SystemExit(main())
