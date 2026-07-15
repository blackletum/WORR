#!/usr/bin/env python3
"""Run the headless dedicated-server plus two-client weapon-damage acceptance gate.

The dedicated server only arms a fixture. A hidden, input-disabled shooter
receives the ordinary console ``+attack`` action after connection while a
second hidden, input-disabled client provides independent target history. The
shooter command is still built, sideband-identified, decoded, and consumed by
production code.
The fixture never constructs command authority or calls a weapon function.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


SCHEMA = "worr.networking.canonical-weapon-damage-runtime.v7"
MAP_NAME = "worr_fr10_rewind_mover"
GATE_MODES = {
    "railgun": {
        "arm_command": "worr_rewind_canonical_rail_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_rail_damage_status",
        "weapon_policy": 5,
        "expected_damage": 80,
    },
    "machinegun": {
        "arm_command": "worr_rewind_canonical_machinegun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_machinegun_damage_status",
        "weapon_policy": 1,
        "expected_damage": 8,
    },
    "chaingun": {
        "arm_command": "worr_rewind_canonical_chaingun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_chaingun_damage_status",
        "weapon_policy": 2,
        "expected_damage": 18,
    },
    "super-shotgun": {
        "arm_command": "worr_rewind_canonical_super_shotgun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_super_shotgun_damage_status",
        "weapon_policy": 4,
        "expected_damage": 120,
    },
    "disruptor": {
        "arm_command": "worr_rewind_canonical_disruptor_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_disruptor_damage_status",
        "weapon_policy": 6,
        "expected_damage": 45,
    },
    "plasma-beam": {
        "arm_command": "worr_rewind_canonical_plasma_beam_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_damage_status",
        "weapon_policy": 7,
        "expected_damage": 8,
    },
    "plasma-beam-held": {
        "arm_command": "worr_rewind_canonical_plasma_beam_held_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_held_damage_status",
        "weapon_policy": 7,
        "expected_damage": 24,
        "refresh_held_attack": True,
    },
    "plasma-beam-sustained": {
        "arm_command": "worr_rewind_canonical_plasma_beam_sustained_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_sustained_damage_status",
        "weapon_policy": 7,
        "expected_damage": 256,
        "require_sustained_hold": True,
    },
    "plasma-beam-release": {
        "arm_command": "worr_rewind_canonical_plasma_beam_release_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_release_damage_status",
        "weapon_policy": 7,
        "expected_damage": 24,
        "refresh_held_attack": True,
        "release_after_expected_damage": True,
    },
    "plasma-beam-water-retrace": {
        "arm_command": "worr_rewind_canonical_plasma_beam_water_retrace_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status",
        "weapon_policy": 7,
        "expected_damage": 4,
        "require_water_retrace": True,
    },
    "thunderbolt": {
        "arm_command": "worr_rewind_canonical_thunderbolt_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_damage_status",
        "weapon_policy": 8,
        "expected_damage": 8,
    },
    "thunderbolt-held": {
        "arm_command": "worr_rewind_canonical_thunderbolt_held_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_held_damage_status",
        "weapon_policy": 8,
        "expected_damage": 24,
        "refresh_held_attack": True,
    },
    "thunderbolt-sustained": {
        "arm_command": "worr_rewind_canonical_thunderbolt_sustained_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_sustained_damage_status",
        "weapon_policy": 8,
        "expected_damage": 256,
        "require_sustained_hold": True,
    },
    "thunderbolt-release": {
        "arm_command": "worr_rewind_canonical_thunderbolt_release_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_release_damage_status",
        "weapon_policy": 8,
        "expected_damage": 24,
        "refresh_held_attack": True,
        "release_after_expected_damage": True,
    },
    "thunderbolt-water-retrace": {
        "arm_command": "worr_rewind_canonical_thunderbolt_water_retrace_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status",
        "weapon_policy": 8,
        "expected_damage": 4,
        "require_water_retrace": True,
    },
    "thunderbolt-discharge": {
        "arm_command": "worr_rewind_canonical_thunderbolt_discharge_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_discharge_damage_status",
        "weapon_policy": 8,
        "expected_damage": 70,
        "require_thunderbolt_discharge": True,
        "current_authority_discharge": True,
    },
    "shotgun": {
        "arm_command": "worr_rewind_canonical_shotgun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_shotgun_damage_status",
        "weapon_policy": 3,
        "expected_damage": 48,
    },
}
STATUS_CVAR = GATE_MODES["railgun"]["status_cvar"]
STATUS_FIELDS = (
    "status",
    "armed",
    "players_ready",
    "history_ready",
    "canonical_scope",
    "attack_received",
    "weapon_callback",
    "canonical_historical_hit",
    "damage_applied",
    "current_geometry_unchanged",
    "target_history_captures",
    "applied_age_us",
    "failure_code",
    "eligible_candidates",
    "playing_candidates",
    "observation_path",
    "observation_outcome",
    "observation_fallback",
    "observation_flags",
    "observation_query",
    "observation_snapshot_epoch",
    "history_epoch",
    "target_history_count",
    "observation_applied_time_us",
    "latest_capture_time_us",
    "trace_current_time_us",
    "context_snapshot_time_us",
    "context_mapped_time_us",
    "target_capture_prepares",
    "capture_append_rejections",
    "target_capture_callbacks",
    "observation_weapon_policy",
    "expected_damage",
    "observed_damage",
    "water_retrace_required",
    "water_retrace_observed",
    "thunderbolt_discharge_required",
    "thunderbolt_discharge_ammo_drained",
    "thunderbolt_discharge_observed",
    "sustained_hold_required",
    "sustained_hold_interrupted",
)
STATUS_RE = re.compile(
    rf'{re.escape(STATUS_CVAR)}\s+"(?P<value>(?:pending|pass|fail):[0-9:]+)"'
)
CLIENT_STATUS_RE = re.compile(r"^\s*(?P<user_id>\d+)\s+[-\d]+\s+[-\d]+\s+", re.MULTILINE)
RCON_PASSWORD = "canonical_rail_runtime"
SHOOTER_NAME = "rail_shooter"
TARGET_NAME = "rail_target"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json_atomic(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def creation_flags() -> int:
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def build_server_command(
    dedicated_exe: Path, port: int, runtime_home: Path | None = None, lag_debug: int = 2,
) -> list[str]:
    """Build a dedicated-only fixture host; it never launches a renderer."""
    command = [
        str(dedicated_exe),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+set", "net_enable_ipv6", "0",
        "+set", "net_ip", "127.0.0.1",
        "+set", "net_port", str(port),
        # The harness issues commands over a temporary UDP rcon socket only
        # after the client is admitted. This keeps both launched processes
        # stdin-free and avoids frame-count races during asset negotiation.
        "+set", "rcon_password", RCON_PASSWORD,
        "+set", "deathmatch", "1",
        "+set", "maxclients", "2",
        "+set", "g_owner_auto_join", "1",
        "+set", "match_auto_join", "1",
        "+set", "match_force_join", "1",
        # The weapon callback must run in an active FFA match.  This changes
        # only the test server's match lifecycle; it does not bypass combat
        # policy or damage checks inside the fixture.
        "+set", "warmup_enabled", "0",
        "+set", "g_warmup_countdown", "0",
        "+set", "match_start_no_humans", "1",
        "+set", "g_lag_compensation", "1",
        # Match the deterministic upstream impairment below. This is a real
        # server policy cvar, so the canonical command resolves against a
        # retained earlier authoritative target pose rather than "now".
        "+set", "sg_lag_compensation_interp_ms", "50",
        "+set", "sg_lag_compensation_debug", str(lag_debug),
        "+set", "sv_fps", "62",
        "+map", MAP_NAME,
    ]
    if runtime_home is not None:
        command[1:1] = ["+set", "fs_homepath", str(runtime_home)]
    return command


def build_client_command(
    client_exe: Path, port: int, name: str, runtime_home: Path | None = None,
) -> list[str]:
    """Build the hidden client. Device input is disabled before initialization."""
    command = [
        str(client_exe),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+set", "win_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "s_enable", "0",
        "+set", "r_renderer", "opengl",
        "+set", "r_fullscreen", "0",
        "+set", "r_geometry", "640x480+0+0",
        # Keep user-command production on the independent physics cadence.
        # Headless automation has no visible render loop to act as an input
        # clock, while ordinary held keyboard state remains device-independent.
        "+set", "cl_async", "1",
        "+set", "cl_maxfps", "62",
        "+set", "r_maxfps", "62",
        "+set", "net_enable_ipv6", "0",
        "+set", "net_clientport", "-1",
        "+set", "cl_protocol", "1038",
        "+set", "name", name,
    ]
    if runtime_home is not None:
        command[1:1] = ["+set", "fs_homepath", str(runtime_home)]
    if name == SHOOTER_NAME:
        # Produce a real, deterministic earlier server selection without
        # loss, reordering, or synthetic command timestamps. Only the
        # shooter's upstream packets are delayed; the target remains an
        # independent ordinary client.
        command.extend([
            "+set", "net_impair_enable", "1",
            "+set", "net_impair_seed", "481516",
            "+set", "net_impair_latency_ms", "50",
            "+set", "net_impair_jitter_ms", "0",
            "+set", "net_impair_loss_pct", "0",
            "+set", "net_impair_burst_loss_pct", "0",
            "+set", "net_impair_reorder_pct", "0",
            "+set", "net_impair_duplicate_pct", "0",
            "+set", "net_impair_upstream_stall_ms", "0",
        ])
    # Config files can legally replace a userinfo value during startup. Repeat
    # the test-only identity after queuing the connection so the live UDP
    # session reports the named, independently selected fixture clients.
    command.extend((
        "+connect", f"127.0.0.1:{port}",
        "+set", "name", name,
    ))
    return command


def parse_status(text: str, status_cvar: str = STATUS_CVAR) -> dict[str, int | str]:
    status_re = re.compile(
        rf'{re.escape(status_cvar)}\s+"(?P<value>(?:pending|pass|fail):[0-9:]+)"'
    )
    matches = list(status_re.finditer(text))
    if len(matches) != 1:
        raise RuntimeError(f"expected one canonical rail status row; observed={len(matches)}")
    values = matches[0].group("value").split(":")
    if len(values) != len(STATUS_FIELDS):
        raise RuntimeError("canonical rail status field count changed")
    status: dict[str, int | str] = {"status": values[0]}
    for name, value in zip(STATUS_FIELDS[1:], values[1:], strict=True):
        if not value.isdecimal():
            raise RuntimeError(f"canonical rail status {name} is not decimal")
        status[name] = int(value)
    return status


def validate_status(
    status: dict[str, int | str], mode: dict[str, int | str] = GATE_MODES["railgun"],
) -> dict[str, int | str]:
    if status["status"] != "pass":
        raise RuntimeError(f"canonical rail probe reported {status['status']!r}")
    required = (
        "armed",
        "players_ready",
        "history_ready",
        "canonical_scope",
        "attack_received",
        "damage_applied",
    )
    if not mode.get("current_authority_discharge", False):
        required += (
            "weapon_callback",
            "canonical_historical_hit",
            "current_geometry_unchanged",
        )
    for name in required:
        if status[name] != 1:
            raise RuntimeError(f"canonical rail probe did not prove {name}")
    if not isinstance(status["target_history_captures"], int) or status["target_history_captures"] < 6:
        raise RuntimeError("canonical rail probe did not retain the pre-fire target history")
    if (not mode.get("current_authority_discharge", False) and
            (not isinstance(status["applied_age_us"], int) or
             status["applied_age_us"] <= 0)):
        raise RuntimeError("canonical rail probe did not select an earlier authoritative instant")
    if status["failure_code"] != 0:
        raise RuntimeError("passing canonical rail probe retained a failure code")
    if status["eligible_candidates"] < 1 or status["playing_candidates"] != 2:
        raise RuntimeError("canonical rail probe did not retain both real gameplay clients")
    if (not mode.get("current_authority_discharge", False) and
            status["observation_weapon_policy"] != mode["weapon_policy"]):
        raise RuntimeError("canonical hitscan probe observed the wrong weapon policy")
    if (status["expected_damage"] != mode["expected_damage"] or
            status["observed_damage"] != mode["expected_damage"]):
        raise RuntimeError("canonical hitscan probe did not apply exact expected damage")
    if mode.get("require_water_retrace", False):
        if status["water_retrace_required"] != 1 or status["water_retrace_observed"] != 1:
            raise RuntimeError("canonical hitscan probe did not prove water retrace")
    if mode.get("require_thunderbolt_discharge", False):
        if (status["thunderbolt_discharge_required"] != 1 or
                status["thunderbolt_discharge_ammo_drained"] != 1 or
                status["thunderbolt_discharge_observed"] != 1):
            raise RuntimeError("canonical hitscan probe did not prove Thunderbolt discharge")
    if mode.get("require_sustained_hold", False):
        if (status["sustained_hold_required"] != 1 or
                status["sustained_hold_interrupted"] != 0):
            raise RuntimeError("canonical hitscan probe did not retain the sustained held attack")
    return status


def determinism_signature(status: dict[str, int | str]) -> tuple[int | str, ...]:
    """Compare stable proof semantics, not unavoidable wall-clock samples."""
    return tuple(status[name] for name in (
        "status",
        "armed",
        "players_ready",
        "history_ready",
        "canonical_scope",
        "attack_received",
        "weapon_callback",
        "canonical_historical_hit",
        "damage_applied",
        "current_geometry_unchanged",
        "target_history_captures",
        "failure_code",
        "eligible_candidates",
        "playing_candidates",
        "observation_path",
        "observation_outcome",
        "observation_fallback",
        "observation_flags",
        "observation_query",
        "observation_snapshot_epoch",
        "history_epoch",
        "capture_append_rejections",
        "observation_weapon_policy",
        "expected_damage",
        "observed_damage",
        "water_retrace_required",
        "water_retrace_observed",
        "thunderbolt_discharge_required",
        "thunderbolt_discharge_ammo_drained",
        "thunderbolt_discharge_observed",
        "sustained_hold_required",
        "sustained_hold_interrupted",
    ))


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


def wait_for_marker(process: subprocess.Popen[str], path: Path, marker: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if marker in read_text(path):
            return
        if process.poll() is not None:
            raise RuntimeError(f"process exited before marker {marker!r}: {process.returncode}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for marker {marker!r}")


def wait_for_marker_count(
    process: subprocess.Popen[str], path: Path, marker: str, count: int, timeout: float,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if read_text(path).count(marker) >= count:
            return
        if process.poll() is not None:
            raise RuntimeError(f"process exited before {count} markers {marker!r}: {process.returncode}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {count} markers {marker!r}")


def rcon_command(port: int, command: str, timeout: float) -> str:
    """Execute one localhost-only rcon command and return its redirected output."""
    packet = b"\xff\xff\xff\xffrcon " + RCON_PASSWORD.encode("ascii")
    packet += b" " + command.encode("ascii") + b"\n"
    responses: list[bytes] = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.settimeout(min(timeout, 0.25))
        connection.sendto(packet, ("127.0.0.1", port))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                response, _ = connection.recvfrom(65535)
            except socket.timeout:
                break
            responses.append(response)
    if not responses:
        raise RuntimeError(f"localhost rcon command did not reply: {command!r}")
    return b"".join(responses).decode("utf-8", errors="replace")


def wait_for_status(
    port: int, timeout: float, mode: dict[str, int | str | bool],
    shooter_user_id: int | None = None,
) -> tuple[dict[str, int | str], list[str]]:
    """Poll the fixture cvar until it completes or reports failure."""
    deadline = time.monotonic() + timeout
    responses: list[str] = []
    release_sent = False
    while time.monotonic() < deadline:
        # A repeated console +attack is a duplicate key-down and need not emit
        # a fresh client command. Cadence modes therefore submit a client-side
        # release/press edge while pending, so every refresh forces another
        # ordinary BUTTON_ATTACK user command. This never calls a server weapon
        # path or constructs server-side input.
        if mode.get("refresh_held_attack", False) and not release_sent:
            if shooter_user_id is None:
                raise RuntimeError("held canonical weapon mode requires a shooter user id")
            responses.append(rcon_command(
                port, f'stuff {shooter_user_id} "-attack; +attack"', min(1.0, timeout),
            ))
        response = rcon_command(port, f"cvarlist {mode['status_cvar']}", min(1.0, timeout))
        responses.append(response)
        try:
            status = parse_status(response, str(mode["status_cvar"]))
        except RuntimeError:
            time.sleep(0.10)
            continue
        if status["status"] != "pending":
            return status, responses
        if (mode.get("release_after_expected_damage", False) and
                not release_sent and
                status["observed_damage"] == mode["expected_damage"]):
            if shooter_user_id is None:
                raise RuntimeError("release canonical weapon mode requires a shooter user id")
            # A normal client-side key-up becomes the no-attack user command
            # that the production repeating weapon sees. No server input or
            # weapon callback is constructed here.
            responses.append(rcon_command(
                # +moveup requests immediate packet delivery; its matching
                # release leaves this client command with zero movement.
                port, f'stuff {shooter_user_id} "-attack; +moveup; -moveup"',
                min(1.0, timeout),
            ))
            release_sent = True
        time.sleep(0.10)
    last_response = responses[-1] if responses else "<no response>"
    raise RuntimeError(
        "timed out waiting for canonical rail fixture completion; "
        f"last rcon response={last_response!r}"
    )


def admitted_fixture_user_ids(status_response: str) -> tuple[int, int]:
    """Return the two sequential slots assigned to the fresh real clients."""
    user_ids = sorted({
        int(match.group("user_id"))
        for match in CLIENT_STATUS_RE.finditer(status_response)
    })
    if len(user_ids) != 2:
        raise RuntimeError(
            f"expected exactly two admitted canonical-rail clients; observed={user_ids}"
        )
    return user_ids[0], user_ids[1]


def wait_for_client_user_ids(port: int, timeout: float) -> tuple[int, int, list[str]]:
    deadline = time.monotonic() + timeout
    responses: list[str] = []
    while time.monotonic() < deadline:
        response = rcon_command(port, "status", min(1.0, timeout))
        responses.append(response)
        try:
            shooter_user_id, target_user_id = admitted_fixture_user_ids(response)
            return shooter_user_id, target_user_id, responses
        except RuntimeError:
            time.sleep(0.10)
    raise RuntimeError("timed out waiting for both admitted real canonical-rail clients")


def terminate(process: subprocess.Popen[str] | None) -> bool:
    if process is None or process.poll() is not None:
        return False
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
    return True


def run_once(
    *, server_command: list[str], shooter_command: list[str], target_command: list[str], working_dir: Path,
    run_root: Path, timeout: float, mode: dict[str, int | str | bool],
) -> dict[str, object]:
    paths = {role: run_root / f"{role}.log" for role in (
        "server.stdout", "server.stderr", "shooter.stdout", "shooter.stderr",
        "target.stdout", "target.stderr", "server.rcon"
    )}
    server: subprocess.Popen[str] | None = None
    shooter: subprocess.Popen[str] | None = None
    target: subprocess.Popen[str] | None = None
    server_terminated = shooter_terminated = target_terminated = False
    rcon_log: list[str] = []
    try:
        with contextlib.ExitStack() as files:
            server_out = files.enter_context(paths["server.stdout"].open("w", encoding="utf-8"))
            server_err = files.enter_context(paths["server.stderr"].open("w", encoding="utf-8"))
            shooter_out = files.enter_context(paths["shooter.stdout"].open("w", encoding="utf-8"))
            shooter_err = files.enter_context(paths["shooter.stderr"].open("w", encoding="utf-8"))
            target_out = files.enter_context(paths["target.stdout"].open("w", encoding="utf-8"))
            target_err = files.enter_context(paths["target.stderr"].open("w", encoding="utf-8"))
            server = subprocess.Popen(
                server_command, cwd=working_dir, stdin=subprocess.DEVNULL,
                stdout=server_out, stderr=server_err, text=True,
                creationflags=creation_flags(),
            )
            wait_for_marker(server, paths["server.stdout"], f"SpawnServer: {MAP_NAME}", timeout)
            shooter = subprocess.Popen(
                shooter_command, cwd=working_dir, stdin=subprocess.DEVNULL,
                stdout=shooter_out, stderr=shooter_err, text=True,
                creationflags=creation_flags(),
            )
            wait_for_marker(server, paths["server.stdout"], "Going from cs_primed to cs_spawned", timeout)
            target = subprocess.Popen(
                target_command, cwd=working_dir, stdin=subprocess.DEVNULL,
                stdout=target_out, stderr=target_err, text=True,
                creationflags=creation_flags(),
            )
            wait_for_marker_count(
                server, paths["server.stdout"], "Going from cs_primed to cs_spawned", 2, timeout
            )
            port = int(server_command[server_command.index("net_port") + 1])
            # Resolve the ordinary welcome-menu choice before arming. This is
            # a real client string command; it only joins the FFA match and
            # cannot create user-command authority or weapon state.
            rcon_log.append(rcon_command(port, 'stuffall "cmd team free"', timeout))
            shooter_user_id, target_user_id, client_statuses = wait_for_client_user_ids(port, timeout)
            rcon_log.extend(client_statuses)
            # This arms only the fixture. It never manufactures a command
            # context or invokes the selected hitscan weapon callback.
            rcon_log.append(rcon_command(port, f"sv {mode['arm_command']}", timeout))
            # Retain normal end-frame target poses before the real cgame
            # render-watermark can refer to them. This must precede the
            # settling delay: a command may legitimately refer to an earlier
            # received snapshot, which cannot be reconstructed after arming.
            time.sleep(1.2)
            # The action goes only to the admitted shooter. Its client turns
            # the payload into a normal BUTTON_ATTACK user command.
            rcon_log.append(rcon_command(port, f'stuff {shooter_user_id} "+attack"', timeout))
            status, status_responses = wait_for_status(
                port, timeout, mode, shooter_user_id,
            )
            rcon_log.extend(status_responses)
        server_text = read_text(paths["server.stdout"])
        shooter_text = read_text(paths["shooter.stdout"])
        target_text = read_text(paths["target.stdout"])
        if server_text.count("Going from cs_primed to cs_spawned") < 2:
            raise RuntimeError("server did not admit both headless clients")
        if "Serverdata packet received" not in shooter_text or "Serverdata packet received" not in target_text:
            raise RuntimeError("a headless canonical-rail client did not accept serverdata")
        status = validate_status(status, mode)
        if any(read_text(paths[name]).strip() for name in (
            "server.stderr", "shooter.stderr", "target.stderr"
        )):
            raise RuntimeError("canonical rail runtime gate wrote stderr")
        shooter_terminated = terminate(shooter)
        target_terminated = terminate(target)
        server_terminated = terminate(server)
        return {
            "status": status,
            "server_stdout_sha256": file_sha256(paths["server.stdout"]),
            "shooter_stdout_sha256": file_sha256(paths["shooter.stdout"]),
            "target_stdout_sha256": file_sha256(paths["target.stdout"]),
            "shooter_terminated_by_gate": shooter_terminated,
            "target_terminated_by_gate": target_terminated,
            "server_terminated_by_gate": server_terminated,
            "logs": {name: str(path) for name, path in paths.items()},
        }
    finally:
        if rcon_log:
            paths["server.rcon"].write_text(
                "\n--- rcon response ---\n".join(rcon_log), encoding="utf-8"
            )
        terminate(shooter)
        terminate(target)
        terminate(server)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client-exe", required=True, type=Path)
    parser.add_argument("--dedicated-exe", required=True, type=Path)
    parser.add_argument("--working-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--port", type=int, default=27960)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("--weapon", choices=tuple(GATE_MODES), default="railgun")
    parser.add_argument("--lag-debug", type=int, default=2)
    args = parser.parse_args()
    if (not 1 <= args.port <= 65535 or args.repeat < 1 or args.timeout <= 0 or
            not 0 <= args.lag_debug <= 3):
        parser.error("port, repeat, timeout, or lag debug is invalid")
    client_exe, dedicated_exe, working_dir, output = (
        args.client_exe.resolve(), args.dedicated_exe.resolve(),
        args.working_dir.resolve(), args.output.resolve(),
    )
    if not client_exe.is_file() or not dedicated_exe.is_file() or not working_dir.is_dir():
        parser.error("client, dedicated executable, or working directory is missing")
    if not (working_dir / "basew" / "maps" / f"{MAP_NAME}.bsp").is_file():
        parser.error("staged canonical hitscan fixture map is missing")
    mode = GATE_MODES[args.weapon]
    started = datetime.now(timezone.utc)
    run_id = started.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    run_root = output.parent / f"{output.stem}.runs" / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    runtime_root = run_root / "runtime"
    server_home = runtime_root / "server"
    shooter_home = runtime_root / "shooter"
    target_home = runtime_root / "target"
    for runtime_home in (server_home, shooter_home, target_home):
        runtime_home.mkdir(parents=True, exist_ok=False)
    server_command = build_server_command(
        dedicated_exe, args.port, server_home, args.lag_debug,
    )
    shooter_command = build_client_command(
        client_exe, args.port, SHOOTER_NAME, shooter_home,
    )
    target_command = build_client_command(
        client_exe, args.port, TARGET_NAME, target_home,
    )
    try:
        runs: list[dict[str, object]] = []
        for index in range(args.repeat):
            repeat_root = run_root / f"repeat-{index + 1:02d}"
            repeat_root.mkdir()
            runs.append(run_once(
                server_command=server_command, shooter_command=shooter_command,
                target_command=target_command,
                working_dir=working_dir, run_root=repeat_root,
                timeout=args.timeout, mode=mode,
            ))
        statuses = [run["status"] for run in runs]
        signatures = [determinism_signature(validate_status(status, mode)) for status in statuses]
        if any(signature != signatures[0] for signature in signatures[1:]):
            raise RuntimeError("canonical rail runtime evidence was not deterministic")
        report: dict[str, object] = {
            "schema": SCHEMA,
            "run_id": run_id,
            "started_at_utc": started.isoformat(),
            "completed_at_utc": datetime.now(timezone.utc).isoformat(),
            "shooter_command": shooter_command,
            "target_command": target_command,
            "dedicated_command": server_command,
            "repeat": args.repeat,
            "weapon": args.weapon,
            "weapon_policy": mode["weapon_policy"],
            "expected_damage": mode["expected_damage"],
            "status": statuses[0],
            "runs": runs,
        }
        write_json_atomic(run_root / "report.json", report)
        write_json_atomic(output, report)
    except Exception as error:
        failure = {
            "schema": SCHEMA + ".failure", "run_id": run_id,
            "error_type": type(error).__name__, "error": str(error),
            "weapon": args.weapon,
            "shooter_command": shooter_command, "target_command": target_command,
            "dedicated_command": server_command,
        }
        write_json_atomic(run_root / "failure.json", failure)
        write_json_atomic(output.with_suffix(".failure.json"), failure)
        print(f"canonical rail runtime gate failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
