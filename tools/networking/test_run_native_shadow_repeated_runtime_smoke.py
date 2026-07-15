#!/usr/bin/env python3
"""Regression tests for the repeated native-shadow runtime gate."""

from __future__ import annotations

import copy
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name(
    "run_native_shadow_repeated_runtime_smoke.py"
)
SPEC = importlib.util.spec_from_file_location(
    "native_shadow_repeated_smoke", MODULE_PATH
)
assert SPEC is not None and SPEC.loader is not None
SMOKE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SMOKE
SPEC.loader.exec_module(SMOKE)


def client_status(**overrides: int) -> dict[str, int]:
    values = {
        "schema": 1,
        "enabled": 1,
        "mode": 2,
        "hooks": 1,
        "capability_confirmed": 1,
        "readiness_phase": 5,
        "official_epoch": 41,
        "transport_epoch": 73,
        "protocol": SMOKE.BASE.PROTOCOL,
        "public_mask": SMOKE.BASE.PUBLIC_MASK,
        "private_mask": SMOKE.BASE.PRIVATE_MASK,
        "probe_hold": 1,
        "cancelled_through_epoch": 0,
        "cancellation_barriers": 1,
        "cancelled_transports": 0,
        "cancelled_command_tx": 0,
        "cancelled_event_rx": 0,
        "cancelled_event_receipts": 0,
        "stale_cancelled_carriers": 0,
        "stale_cancelled_readiness_records": 0,
        "challenges": 1,
        "client_ready_queued": 1,
        "server_active": 1,
        "proof_enqueued": 40,
        "retained": 0,
        "retained_highwater": 1,
        "retained_releases": 40,
        "tx_first_sends": 40,
        "tx_retries": 2,
        "tx_handoffs": 42,
        "ack_carriers": 45,
        "acknowledged_reliable": 40,
        "drains": 0,
        "failures": 0,
        "last_failure": 0,
    }
    values.update(overrides)
    return values


def server_status(**overrides: int) -> dict[str, int]:
    values = {
        "schema": 1,
        "slot": 0,
        "protocol": SMOKE.BASE.PROTOCOL,
        "enabled": 1,
        "lifecycle": 2,
        "hooks": 1,
        "readiness_phase": 4,
        "official_epoch": 41,
        "transport_epoch": 73,
        "cancelled_through_epoch": 0,
        "public_mask": SMOKE.BASE.PUBLIC_MASK,
        "private_mask": SMOKE.BASE.PRIVATE_MASK,
        "wire_committed": 1,
        "wire_committed_transport_epoch": 73,
        "challenges_queued": 1,
        "client_ready": 1,
        "server_active": 1,
        "rx_carriers": 45,
        "rx_commits": 40,
        "rx_repeat_refreshes": 5,
        "legacy_joins": 40,
        "command_matches": 40,
        "command_mismatches": 0,
        "sample_mismatches": 0,
        "ack_eligible": 0,
        "ack_prepares": 48,
        "ack_handoffs": 45,
        "async_rate_deferrals": 0,
        "async_fragment_deferrals": 0,
        "async_wake_attempts": 3,
        "async_ack_handoffs": 3,
        "async_wake_no_handoff": 0,
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
    }
    values.update(overrides)
    return values


def status_line(
    prefix: str, fields: tuple[str, ...], values: dict[str, int]
) -> str:
    return prefix + " " + " ".join(
        f"{name}={values[name]}" for name in fields
    )


def impairment_text(seed: int) -> str:
    return "\n".join(
        (
            "net_impair: enabled=1 "
            f"seed={seed} latency=25 jitter=0 loss=0.0 "
            "burst=0.0/3 reorder=0.0 duplicate=0.0 corrupt=0.0 "
            "upstream_stall=0 rate_kbps=0 queue=0/1024 high_water=3",
            "net_impair counters: seen=100 dropped=0 burst_dropped=0 "
            "reordered=0 duplicated=0 corrupted=0 upstream_stalled=0 "
            "throttled=0 overflow=0 resets=0",
        )
    )


def impairment_evidence(seed: int) -> dict[str, object]:
    config, counters = SMOKE.BASE.parse_impairment_status(
        impairment_text(seed)
    )
    return {"config": config, "counters": counters}


class RepeatedStatusTests(unittest.TestCase):
    def test_valid_repeated_pair(self) -> None:
        self.assertEqual(
            SMOKE.validate_status_pair(client_status(), server_status()), 40
        )

    def test_floor_and_exact_join_accounting_are_enforced(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "floor"):
            SMOKE.validate_status_pair(
                client_status(
                    proof_enqueued=31,
                    retained_releases=31,
                    tx_first_sends=31,
                    acknowledged_reliable=31,
                ),
                server_status(
                    rx_commits=31,
                    legacy_joins=31,
                    command_matches=31,
                ),
            )
        with self.assertRaisesRegex(RuntimeError, "legacy joins"):
            SMOKE.validate_status_pair(
                client_status(), server_status(legacy_joins=39)
            )

    def test_terminal_drain_and_retention_are_rejected(self) -> None:
        for changed, pattern in (
            (client_status(retained=1), "retained"),
            (client_status(probe_hold=0), "probe_hold"),
            (client_status(cancellation_barriers=0), "cancellation_barriers"),
            (client_status(cancelled_command_tx=1), "cancelled_command_tx"),
            (server_status(drains=1), "drains"),
            (server_status(command_mismatches=1), "command_mismatches"),
            (server_status(cancellation_barriers=0), "cancellation_barriers"),
            (server_status(cancelled_rx_messages=1), "cancelled_rx_messages"),
        ):
            with self.subTest(pattern=pattern):
                if changed.get("lifecycle") is not None:
                    client, server = client_status(), changed
                else:
                    client, server = changed, server_status()
                with self.assertRaisesRegex(RuntimeError, pattern):
                    SMOKE.validate_status_pair(client, server)

    def test_wire_committed_epoch_must_be_active(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "wire-committed epoch"):
            SMOKE.validate_status_pair(
                client_status(),
                server_status(wire_committed_transport_epoch=72),
            )

    def test_runtime_text_requires_udp_status_and_latency_contract(self) -> None:
        client_marker = "client_done"
        server_marker = "server_done"
        port = 28017
        client_text = "\n".join(
            (
                f"Connected to {SMOKE.BASE.ADDRESS}:{port}",
                status_line(
                    SMOKE.BASE.CLIENT_STATUS_PREFIX,
                    SMOKE.BASE.CLIENT_STATUS_FIELDS,
                    client_status(),
                ),
                impairment_text(SMOKE.BASE.CLIENT_IMPAIR_SEED),
                client_marker,
            )
        )
        server_text = "\n".join(
            (
                status_line(
                    SMOKE.BASE.SERVER_STATUS_PREFIX,
                    SMOKE.BASE.SERVER_STATUS_FIELDS,
                    server_status(),
                ),
                impairment_text(SMOKE.BASE.SERVER_IMPAIR_SEED),
                server_marker,
            )
        )
        evidence = SMOKE.validate_runtime_text(
            client_text,
            server_text,
            client_marker=client_marker,
            server_marker=server_marker,
            port=port,
        )
        self.assertEqual(evidence["command_count"], 40)


class RepeatedCommandTests(unittest.TestCase):
    def test_relative_control_opens_then_closes_sampling(self) -> None:
        control = SMOKE.client_control_payload("client_done")
        release = control.index("cl_worr_native_shadow_probe_hold 0")
        rehold = control.index("cl_worr_native_shadow_probe_hold 1")
        status = control.index("cl_worr_native_shadow_status")
        self.assertLess(release, rehold)
        self.assertLess(rehold, status)
        self.assertIn(
            f"wait {SMOKE.CLIENT_SAMPLE_WAIT_FRAMES}", control
        )

    def test_commands_pin_udp_rate_cap_and_default_hold(self) -> None:
        client = SMOKE.client_command(Path("client.exe"), port=28017)
        server = SMOKE.server_command(
            Path("server.exe"),
            port=28017,
            client_completion_marker="client_done",
            server_completion_marker="server_done",
        )
        for command in (client, server):
            settings = {
                command[index + 1]: command[index + 2]
                for index, value in enumerate(command[:-2])
                if value == "+set"
            }
            self.assertEqual(
                settings["net_maxmsglen"], str(SMOKE.BASE.NET_MAXMSGLEN)
            )
        client_settings = {
            client[index + 1]: client[index + 2]
            for index, value in enumerate(client[:-2])
            if value == "+set"
        }
        self.assertEqual(client_settings["cl_worr_native_shadow"], "1")
        self.assertEqual(client_settings["win_headless"], "1")
        self.assertEqual(client_settings["in_enable"], "0")
        self.assertEqual(client_settings["in_grab"], "0")
        self.assertEqual(
            client_settings["cl_worr_native_shadow_probe_hold"], "1"
        )


class RepeatedReportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        run_id = "unit-report"
        port = 28017
        runtime = []
        runtime_paths = {}
        for role in SMOKE.BASE.RUNTIME_ROLES:
            path = root / f"{role}.bin"
            path.write_bytes(role.encode("ascii"))
            runtime.append(SMOKE.BASE.make_file_record(role, path))
            runtime_paths[role] = path.resolve()
        client_marker = f"worr_native_repeated_client_complete_{run_id}"
        server_marker = f"worr_native_repeated_server_complete_{run_id}"
        log_text = {
            "client_stdout": "\n".join(
                (
                    f"Connected to {SMOKE.BASE.ADDRESS}:{port}",
                    status_line(
                        SMOKE.BASE.CLIENT_STATUS_PREFIX,
                        SMOKE.BASE.CLIENT_STATUS_FIELDS,
                        client_status(),
                    ),
                    impairment_text(SMOKE.BASE.CLIENT_IMPAIR_SEED),
                    client_marker,
                    "",
                )
            ),
            "client_stderr": "",
            "server_stdout": "\n".join(
                (
                    status_line(
                        SMOKE.BASE.SERVER_STATUS_PREFIX,
                        SMOKE.BASE.SERVER_STATUS_FIELDS,
                        server_status(),
                    ),
                    impairment_text(SMOKE.BASE.SERVER_IMPAIR_SEED),
                    server_marker,
                    "",
                )
            ),
            "server_stderr": "",
        }
        logs = []
        for role in SMOKE.LOG_ROLES:
            path = root / f"{role}.log"
            path.write_text(log_text[role], encoding="utf-8")
            logs.append(SMOKE.BASE.make_file_record(role, path))
        client_argv = SMOKE.client_command(
            runtime_paths["client_executable"], port=port
        )
        server_argv = SMOKE.server_command(
            runtime_paths["dedicated_executable"],
            port=port,
            client_completion_marker=client_marker,
            server_completion_marker=server_marker,
        )
        self.report = {
            "schema": SMOKE.SCHEMA,
            "passed": True,
            "run_id": run_id,
            "profile": SMOKE.build_profile(port),
            "command_count": 40,
            "statuses": {
                "client": client_status(),
                "server": server_status(),
            },
            "impairment": {
                "client": impairment_evidence(
                    SMOKE.BASE.CLIENT_IMPAIR_SEED
                ),
                "server": impairment_evidence(
                    SMOKE.BASE.SERVER_IMPAIR_SEED
                ),
            },
            "commands": {
                "client_argc": len(client_argv),
                "client_argv_sha256": SMOKE.BASE.argv_sha256(client_argv),
                "server_argc": len(server_argv),
                "server_argv_sha256": SMOKE.BASE.argv_sha256(server_argv),
            },
            "process": {
                "client_terminated_by_harness": True,
                "server_terminated_by_harness": True,
                "elapsed_seconds": 30.0,
            },
            "runtime_components": runtime,
            "logs": logs,
            "limitations": ["one", "two", "three", "four"],
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_complete_report_passes(self) -> None:
        SMOKE.validate_report(self.report)

    def test_report_rejects_counter_and_hash_drift(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["command_count"] = 39
        with self.assertRaisesRegex(RuntimeError, "command count"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        changed["runtime_components"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "SHA-256"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        changed["commands"]["client_argv_sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "argv binding"):
            SMOKE.validate_report(changed)


if __name__ == "__main__":
    unittest.main()
