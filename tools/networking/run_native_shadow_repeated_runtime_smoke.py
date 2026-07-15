#!/usr/bin/env python3
"""Prove repeated native command shadowing through two staged processes.

The client and dedicated server communicate over real IPv4 UDP.  After the
private readiness exchange settles, a stuffed relative command opens the
client's native sampling gate for a bounded interval, closes it again, and
allows the final stop-and-wait command to drain before both endpoints publish
stable V1 status rows.  Legacy MOVE/BATCH_MOVE remains authoritative.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Mapping, Sequence


BASE_PATH = Path(__file__).with_name("run_native_shadow_runtime_smoke.py")
BASE_SPEC = importlib.util.spec_from_file_location(
    "native_shadow_runtime_base", BASE_PATH
)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError("could not load the native-shadow runtime helper")
BASE = importlib.util.module_from_spec(BASE_SPEC)
sys.modules[BASE_SPEC.name] = BASE
BASE_SPEC.loader.exec_module(BASE)


SCHEMA = "worr.networking.native-shadow-repeated-runtime.v1"
FAILURE_SCHEMA = "worr.networking.native-shadow-repeated-runtime-failure.v1"
MINIMUM_COMMANDS = 32
RATE_BYTES_PER_SECOND = 1_000_000
SERVER_QUEUE_WAIT_FRAMES = 400       # 10 s for bootstrap/readiness settling.
CLIENT_RELEASE_WAIT_FRAMES = 30      # Relative to the stuffed control.
CLIENT_SAMPLE_WAIT_FRAMES = 600      # Roughly 10 s of stop-and-wait sampling.
CLIENT_DRAIN_WAIT_FRAMES = 300       # Hold closed while the last ACK drains.
SERVER_FINAL_WAIT_FRAMES = 850       # 21.25 s after sending client control.
TRIAL_TIMEOUT_SECONDS = 45.0
MAX_TIMEOUT_SECONDS = 60.0
LOG_ROLES = BASE.ENDPOINT_LOG_ROLES


def _safe_marker(marker: str) -> bool:
    return bool(marker) and not any(
        character.isspace() or character in ';"' for character in marker
    )


def client_control_payload(completion_marker: str) -> str:
    if not _safe_marker(completion_marker):
        raise ValueError("completion marker must be one safe token")
    return (
        f"wait {CLIENT_RELEASE_WAIT_FRAMES}; "
        "cl_worr_native_shadow_probe_hold 0; "
        f"wait {CLIENT_SAMPLE_WAIT_FRAMES}; "
        "cl_worr_native_shadow_probe_hold 1; "
        f"wait {CLIENT_DRAIN_WAIT_FRAMES}; "
        "cl_worr_native_shadow_status; "
        "net_impair_status; "
        f"echo {completion_marker};"
    )


def server_command(
    executable: Path,
    *,
    port: int,
    client_completion_marker: str,
    server_completion_marker: str,
) -> list[str]:
    if not _safe_marker(server_completion_marker):
        raise ValueError("server completion marker must be one safe token")
    command = [
        str(executable),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+set", "net_enable_ipv6", "0",
        "+set", "net_ip", BASE.ADDRESS,
        "+set", "net_port", str(port),
        "+set", "net_maxmsglen", str(BASE.NET_MAXMSGLEN),
        "+set", "deathmatch", "0",
        "+set", "coop", "0",
        "+set", "maxclients", "1",
        "+set", "g_owner_auto_join", "1",
        "+set", "match_auto_join", "1",
        "+set", "match_force_join", "1",
        "+set", "sv_lan_force_rate", "0",
        "+set", "sv_fps", str(BASE.SERVER_FPS),
        "+set", "sv_min_rate", str(RATE_BYTES_PER_SECOND),
        "+set", "sv_max_rate", str(RATE_BYTES_PER_SECOND),
        "+set", "sv_worr_native_shadow", "1",
    ]
    command.extend(BASE._common_impairment_settings(BASE.SERVER_IMPAIR_SEED))
    command.extend(("+map", BASE.MAP_NAME))
    command.extend(BASE.wait_commands(SERVER_QUEUE_WAIT_FRAMES))
    command.extend(
        (
            "+stuffall",
            f'"{client_control_payload(client_completion_marker)}"',
        )
    )
    command.extend(BASE.wait_commands(SERVER_FINAL_WAIT_FRAMES))
    command.extend(
        (
            "+sv_worr_native_shadow_status",
            "+net_impair_status",
            "+echo", server_completion_marker,
            "+wait", "1000",
        )
    )
    BASE._validate_command_line(command, "repeated server")
    return command


def client_command(executable: Path, *, port: int) -> list[str]:
    return BASE.client_command(
        executable,
        port=port,
        rate_bytes_per_second=RATE_BYTES_PER_SECOND,
    )


def _require_exact(
    status: Mapping[str, int], exact: Mapping[str, int], endpoint: str
) -> None:
    for name, expected in exact.items():
        if status.get(name) != expected:
            raise RuntimeError(
                f"{endpoint} repeated status mismatch: "
                f"{name}={status.get(name)!r} expected={expected}"
            )


def validate_status_pair(
    client: Mapping[str, int], server: Mapping[str, int]
) -> int:
    _require_exact(
        client,
        {
            "schema": 1,
            "enabled": 1,
            "mode": 2,
            "hooks": 1,
            "capability_confirmed": 1,
            "readiness_phase": 5,
            "protocol": BASE.PROTOCOL,
            "public_mask": BASE.PUBLIC_MASK,
            "private_mask": BASE.PRIVATE_MASK,
            "probe_hold": 1,
            "cancelled_through_epoch": 0,
            "cancellation_barriers": 1,
            "cancelled_transports": 0,
            "cancelled_command_tx": 0,
            "cancelled_event_rx": 0,
            "cancelled_event_receipts": 0,
            "stale_cancelled_carriers": 0,
            "stale_cancelled_readiness_records": 0,
            "client_ready_queued": 1,
            "retained": 0,
            "retained_highwater": 1,
            "drains": 0,
            "failures": 0,
            "last_failure": 0,
        },
        "client",
    )
    _require_exact(
        server,
        {
            "schema": 1,
            "protocol": BASE.PROTOCOL,
            "enabled": 1,
            "lifecycle": 2,
            "hooks": 1,
            "readiness_phase": 4,
            "cancelled_through_epoch": 0,
            "public_mask": BASE.PUBLIC_MASK,
            "private_mask": BASE.PRIVATE_MASK,
            "wire_committed": 1,
            "challenges_queued": 1,
            "client_ready": 1,
            "server_active": 1,
            "command_mismatches": 0,
            "sample_mismatches": 0,
            "ack_eligible": 0,
            "rx_rejections": 0,
            "tx_ack_rejections": 0,
            "rx_drained": 0,
            "drains": 0,
            "failures": 0,
            "cancellation_barriers": 1,
            "cancelled_transports": 0,
            "cancelled_rx_messages": 0,
            "cancelled_receipts": 0,
            "cancelled_event_records": 0,
            "stale_cancelled_carriers": 0,
            "stale_cancelled_readiness_records": 0,
            "last_failure": 0,
        },
        "server",
    )
    for name in (
        "official_epoch", "transport_epoch", "protocol",
        "public_mask", "private_mask",
    ):
        if client.get(name) != server.get(name):
            raise RuntimeError(f"repeated endpoints disagree on {name}")
    if server.get("slot", -1) < 0:
        raise RuntimeError("server repeated status has an invalid slot")
    if server.get("wire_committed_transport_epoch") != server.get(
        "transport_epoch"
    ):
        raise RuntimeError(
            "server repeated status wire-committed epoch is not active"
        )

    command_count = client.get("proof_enqueued", 0)
    if command_count < MINIMUM_COMMANDS:
        raise RuntimeError(
            f"repeated command count {command_count} is below "
            f"the {MINIMUM_COMMANDS}-command floor"
        )
    equal_counters = {
        "client retained releases": client.get("retained_releases"),
        "client first sends": client.get("tx_first_sends"),
        "client acknowledged": client.get("acknowledged_reliable"),
        "server RX commits": server.get("rx_commits"),
        "server legacy joins": server.get("legacy_joins"),
        "server command matches": server.get("command_matches"),
    }
    for label, value in equal_counters.items():
        if value != command_count:
            raise RuntimeError(
                f"{label}={value!r} does not equal repeated "
                f"command count {command_count}"
            )
    if client.get("tx_handoffs", 0) < command_count:
        raise RuntimeError("client repeated DATA handoffs are incomplete")
    if client.get("tx_retries", 0) > client.get("tx_handoffs", 0):
        raise RuntimeError("client repeated retry accounting is inconsistent")
    if client.get("ack_carriers", 0) < command_count:
        raise RuntimeError("client received fewer ACK carriers than commands")
    if server.get("rx_carriers", 0) < command_count:
        raise RuntimeError("server received fewer DATA carriers than commands")
    if server.get("ack_prepares", 0) < server.get("ack_handoffs", 0):
        raise RuntimeError("server repeated ACK handoffs exceed prepares")
    if server.get("ack_handoffs", 0) < command_count:
        raise RuntimeError("server handed off fewer ACKs than commands")
    if server.get("async_wake_attempts", 0) != (
        server.get("async_ack_handoffs", 0)
        + server.get("async_wake_no_handoff", 0)
    ):
        raise RuntimeError("server repeated async wake accounting is inconsistent")
    return command_count


def validate_runtime_text(
    client_text: str,
    server_text: str,
    *,
    client_marker: str,
    server_marker: str,
    port: int,
) -> dict[str, object]:
    BASE.validate_terminal_order(
        client_text,
        status_prefix=BASE.CLIENT_STATUS_PREFIX,
        completion_marker=client_marker,
    )
    BASE.validate_terminal_order(
        server_text,
        status_prefix=BASE.SERVER_STATUS_PREFIX,
        completion_marker=server_marker,
    )
    if f"Connected to {BASE.ADDRESS}:{port}" not in client_text:
        raise RuntimeError("client log does not prove the requested UDP peer")
    client_status = BASE.parse_client_status(client_text)
    server_status = BASE.parse_server_status(server_text)
    command_count = validate_status_pair(client_status, server_status)
    client_config, client_counters = BASE.parse_impairment_status(client_text)
    server_config, server_counters = BASE.parse_impairment_status(server_text)
    BASE.validate_impairment_status(
        client_config, client_counters,
        expected_seed=BASE.CLIENT_IMPAIR_SEED,
    )
    BASE.validate_impairment_status(
        server_config, server_counters,
        expected_seed=BASE.SERVER_IMPAIR_SEED,
    )
    return {
        "command_count": command_count,
        "client_status": client_status,
        "server_status": server_status,
        "client_impairment": {
            "config": client_config,
            "counters": client_counters,
        },
        "server_impairment": {
            "config": server_config,
            "counters": server_counters,
        },
    }


def build_profile(port: int) -> dict[str, object]:
    return {
        "transport": "udp_ipv4",
        "address": BASE.ADDRESS,
        "port": port,
        "protocol": BASE.PROTOCOL,
        "map": BASE.MAP_NAME,
        "net_maxmsglen": BASE.NET_MAXMSGLEN,
        "rate_bytes_per_second": RATE_BYTES_PER_SECOND,
        "server_fps": BASE.SERVER_FPS,
        "client_native_shadow": 1,
        "server_native_shadow": 1,
        "client_probe_hold_initial": 1,
        "client_probe_hold_final": 1,
        "client_release_wait_frames": CLIENT_RELEASE_WAIT_FRAMES,
        "client_sample_wait_frames": CLIENT_SAMPLE_WAIT_FRAMES,
        "client_drain_wait_frames": CLIENT_DRAIN_WAIT_FRAMES,
        "server_queue_wait_frames": SERVER_QUEUE_WAIT_FRAMES,
        "server_final_wait_frames": SERVER_FINAL_WAIT_FRAMES,
        "minimum_commands": MINIMUM_COMMANDS,
        "impairment_latency_ms": BASE.IMPAIR_LATENCY_MS,
        "client_impairment_seed": BASE.CLIENT_IMPAIR_SEED,
        "server_impairment_seed": BASE.SERVER_IMPAIR_SEED,
    }


def validate_profile(profile: object) -> None:
    if not isinstance(profile, dict):
        raise RuntimeError("repeated profile must be an object")
    port = profile.get("port")
    if not isinstance(port, int) or port <= 0 or port > 65535:
        raise RuntimeError("repeated profile has an invalid UDP port")
    expected = build_profile(port)
    if profile != expected:
        raise RuntimeError("repeated profile does not match the fixed contract")


def validate_report(report: object) -> None:
    if not isinstance(report, dict) or report.get("schema") != SCHEMA:
        raise RuntimeError("repeated report schema mismatch")
    if report.get("passed") is not True:
        raise RuntimeError("repeated report is not passing")
    validate_profile(report.get("profile"))
    profile = report["profile"]
    assert isinstance(profile, dict)
    run_id = report.get("run_id")
    if not isinstance(run_id, str) or not _safe_marker(run_id):
        raise RuntimeError("repeated report run ID is invalid")
    statuses = report.get("statuses")
    if not isinstance(statuses, dict):
        raise RuntimeError("repeated report statuses are missing")
    client = statuses.get("client")
    server = statuses.get("server")
    if not isinstance(client, dict) or not isinstance(server, dict):
        raise RuntimeError("repeated endpoint status is missing")
    command_count = validate_status_pair(client, server)
    if report.get("command_count") != command_count:
        raise RuntimeError("repeated report command count disagrees with status")
    process = report.get("process")
    if (
        not isinstance(process, dict)
        or process.get("client_terminated_by_harness") is not True
        or process.get("server_terminated_by_harness") is not True
    ):
        raise RuntimeError("repeated endpoints were not harness-terminated")
    elapsed = process.get("elapsed_seconds")
    if (
        not isinstance(elapsed, (int, float))
        or isinstance(elapsed, bool)
        or elapsed <= 0
        or elapsed > TRIAL_TIMEOUT_SECONDS + 1.0
    ):
        raise RuntimeError("repeated process elapsed time is invalid")
    commands = report.get("commands")
    if not isinstance(commands, dict):
        raise RuntimeError("repeated command hashes are missing")
    for endpoint in ("client", "server"):
        argc = commands.get(f"{endpoint}_argc")
        digest = commands.get(f"{endpoint}_argv_sha256")
        if not isinstance(argc, int) or argc <= 0:
            raise RuntimeError("repeated command argc is invalid")
        if not isinstance(digest, str) or not BASE.SHA256_RE.fullmatch(digest):
            raise RuntimeError("repeated command SHA-256 is invalid")
    impairment = report.get("impairment")
    if not isinstance(impairment, dict):
        raise RuntimeError("repeated impairment evidence is missing")
    for endpoint, seed in (
        ("client", BASE.CLIENT_IMPAIR_SEED),
        ("server", BASE.SERVER_IMPAIR_SEED),
    ):
        evidence = impairment.get(endpoint)
        if not isinstance(evidence, dict):
            raise RuntimeError("repeated endpoint impairment is missing")
        BASE.validate_impairment_status(
            evidence.get("config"), evidence.get("counters"),
            expected_seed=seed,
        )
    runtime_records = report.get("runtime_components")
    BASE.validate_file_records(runtime_records, BASE.RUNTIME_ROLES)
    assert isinstance(runtime_records, list)
    runtime_paths = {
        record["role"]: Path(record["path"])
        for record in runtime_records
        if isinstance(record, dict)
    }
    port = int(profile["port"])
    expected_commands = {
        "client": client_command(
            runtime_paths["client_executable"], port=port
        ),
        "server": server_command(
            runtime_paths["dedicated_executable"],
            port=port,
            client_completion_marker=(
                f"worr_native_repeated_client_complete_{run_id}"
            ),
            server_completion_marker=(
                f"worr_native_repeated_server_complete_{run_id}"
            ),
        ),
    }
    for endpoint, expected_argv in expected_commands.items():
        if (
            commands.get(f"{endpoint}_argc") != len(expected_argv)
            or commands.get(f"{endpoint}_argv_sha256") !=
                BASE.argv_sha256(expected_argv)
        ):
            raise RuntimeError(
                f"repeated {endpoint} argv binding mismatch"
            )
    log_records = report.get("logs")
    BASE.validate_file_records(log_records, LOG_ROLES)
    assert isinstance(log_records, list)
    log_paths = {
        record["role"]: Path(record["path"])
        for record in log_records
        if isinstance(record, dict)
    }
    if (
        BASE._read_text(log_paths["client_stderr"]).strip()
        or BASE._read_text(log_paths["server_stderr"]).strip()
    ):
        raise RuntimeError("repeated report contains unexpected stderr")
    bound_evidence = validate_runtime_text(
        BASE._read_text(log_paths["client_stdout"]),
        BASE._read_text(log_paths["server_stdout"]),
        client_marker=f"worr_native_repeated_client_complete_{run_id}",
        server_marker=f"worr_native_repeated_server_complete_{run_id}",
        port=port,
    )
    if (
        bound_evidence["command_count"] != report.get("command_count")
        or bound_evidence["client_status"] != client
        or bound_evidence["server_status"] != server
        or bound_evidence["client_impairment"] != impairment.get("client")
        or bound_evidence["server_impairment"] != impairment.get("server")
    ):
        raise RuntimeError(
            "repeated report evidence disagrees with its hashed logs"
        )
    limitations = report.get("limitations")
    if (
        not isinstance(limitations, list)
        or len(limitations) < 4
        or any(not isinstance(item, str) or not item for item in limitations)
    ):
        raise RuntimeError("repeated report limitations are incomplete")


def default_paths() -> tuple[Path, Path, Path, Path]:
    repository = Path(__file__).resolve().parents[2]
    working = repository / ".install"
    return (
        working / "worr_x86_64.exe",
        working / "worr_ded_x86_64.exe",
        working,
        repository / ".tmp/networking/native-shadow-repeated-runtime.json",
    )


def main() -> int:
    default_client, default_dedicated, default_working, default_output = (
        default_paths()
    )
    parser = argparse.ArgumentParser()
    parser.add_argument("--client-exe", type=Path, default=default_client)
    parser.add_argument("--dedicated-exe", type=Path, default=default_dedicated)
    parser.add_argument("--working-dir", type=Path, default=default_working)
    parser.add_argument("--output", type=Path, default=default_output)
    parser.add_argument("--timeout", type=float, default=TRIAL_TIMEOUT_SECONDS)
    args = parser.parse_args()
    if args.timeout < 30.0 or args.timeout > MAX_TIMEOUT_SECONDS:
        parser.error("--timeout must be between 30 and 60 seconds")
    args.client_exe = args.client_exe.resolve()
    args.dedicated_exe = args.dedicated_exe.resolve()
    args.working_dir = args.working_dir.resolve()
    args.output = args.output.resolve()
    BASE.validate_staged_paths(
        args.client_exe, args.dedicated_exe, args.working_dir
    )
    failure_output = BASE.invalidate_previous_outputs(args.output)
    started_at = datetime.now(timezone.utc)
    run_id = started_at.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    run_root = args.output.parent / f"{args.output.stem}.runs" / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    port = 0
    client_argv: list[str] = []
    server_argv: list[str] = []
    try:
        before_manifest = BASE.runtime_manifest(args.working_dir)
        port = BASE.reserve_udp_port()
        client_marker = f"worr_native_repeated_client_complete_{run_id}"
        server_marker = f"worr_native_repeated_server_complete_{run_id}"
        client_argv = client_command(args.client_exe, port=port)
        server_argv = server_command(
            args.dedicated_exe,
            port=port,
            client_completion_marker=client_marker,
            server_completion_marker=server_marker,
        )
        process = BASE.run_processes(
            server_argv=server_argv,
            client_argv=client_argv,
            working_dir=args.working_dir,
            run_root=run_root,
            client_marker=client_marker,
            server_marker=server_marker,
            timeout=min(args.timeout, TRIAL_TIMEOUT_SECONDS),
        )
        texts = process["texts"]
        paths = process["paths"]
        evidence = validate_runtime_text(
            str(texts["client_stdout"]),
            str(texts["server_stdout"]),
            client_marker=client_marker,
            server_marker=server_marker,
            port=port,
        )
        after_manifest = BASE.runtime_manifest(args.working_dir)
        if after_manifest != before_manifest:
            raise RuntimeError(
                "staged runtime components changed during repeated gate"
            )
        report: dict[str, object] = {
            "schema": SCHEMA,
            "passed": True,
            "run_id": run_id,
            "started_at_utc": started_at.isoformat(),
            "completed_at_utc": datetime.now(timezone.utc).isoformat(),
            "profile": build_profile(port),
            "command_count": evidence["command_count"],
            "statuses": {
                "client": evidence["client_status"],
                "server": evidence["server_status"],
            },
            "impairment": {
                "client": evidence["client_impairment"],
                "server": evidence["server_impairment"],
            },
            "commands": {
                "client_argc": len(client_argv),
                "client_argv_sha256": BASE.argv_sha256(client_argv),
                "server_argc": len(server_argv),
                "server_argv_sha256": BASE.argv_sha256(server_argv),
            },
            "process": {
                "client_terminated_by_harness": process[
                    "client_terminated_by_harness"
                ],
                "server_terminated_by_harness": process[
                    "server_terminated_by_harness"
                ],
                "elapsed_seconds": process["elapsed_seconds"],
            },
            "runtime_components": before_manifest,
            "logs": [
                BASE.make_file_record(role, paths[role])
                for role in LOG_ROLES
            ],
            "limitations": [
                "Legacy MOVE/BATCH_MOVE remains the sole simulation authority.",
                "The gate samples a repeated stop-and-wait subset, not every legacy command produced while one native command is retained.",
                "This directional pilot does not prove mixed DATA+ACK packets or server-originated native DATA.",
                "The deterministic profile adds latency but no loss, reordering, duplication, corruption, or bandwidth pressure.",
                "UDP localhost does not model WAN congestion, NAT traversal, multi-client fairness, or promotion to native authority.",
            ],
        }
        validate_report(report)
        BASE.write_json_atomic(run_root / "report.json", report)
        BASE.write_json_atomic(args.output, report)
    except Exception as error:
        artifacts = [
            BASE.make_file_record(path.stem.replace(".", "_"), path)
            for path in sorted(run_root.glob("*.log"))
            if path.is_file()
        ]
        failure = {
            "schema": FAILURE_SCHEMA,
            "passed": False,
            "run_id": run_id,
            "started_at_utc": started_at.isoformat(),
            "failed_at_utc": datetime.now(timezone.utc).isoformat(),
            "error_type": type(error).__name__,
            "error": str(error),
            "port": port,
            "commands": {
                "client_argv_sha256": (
                    BASE.argv_sha256(client_argv) if client_argv else None
                ),
                "server_argv_sha256": (
                    BASE.argv_sha256(server_argv) if server_argv else None
                ),
            },
            "artifacts": artifacts,
        }
        BASE.write_json_atomic(run_root / "failure.json", failure)
        BASE.write_json_atomic(failure_output, failure)
        print(f"native repeated shadow gate failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
