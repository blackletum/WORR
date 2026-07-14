#!/usr/bin/env python3
"""Regression tests for the two-process native-shadow acceptance parser."""

from __future__ import annotations

import copy
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("run_native_shadow_runtime_smoke.py")
SPEC = importlib.util.spec_from_file_location("native_shadow_smoke", MODULE_PATH)
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
        "protocol": SMOKE.PROTOCOL,
        "public_mask": SMOKE.PUBLIC_MASK,
        "private_mask": SMOKE.PRIVATE_MASK,
        "probe_hold": 0,
        "challenges": 1,
        "client_ready_queued": 1,
        "server_active": 1,
        "proof_enqueued": 1,
        "retained": 0,
        "retained_highwater": 1,
        "retained_releases": 1,
        "tx_first_sends": 1,
        "tx_retries": 2,
        "tx_handoffs": 3,
        "ack_carriers": 3,
        "acknowledged_reliable": 1,
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
        "protocol": SMOKE.PROTOCOL,
        "enabled": 1,
        "lifecycle": 2,
        "hooks": 1,
        "readiness_phase": 4,
        "official_epoch": 41,
        "transport_epoch": 73,
        "public_mask": SMOKE.PUBLIC_MASK,
        "private_mask": SMOKE.PRIVATE_MASK,
        "wire_committed": 1,
        "challenges_queued": 1,
        "client_ready": 1,
        "server_active": 1,
        "rx_carriers": 3,
        "rx_commits": 1,
        "rx_repeat_refreshes": 2,
        "legacy_joins": 1,
        "command_matches": 1,
        "command_mismatches": 0,
        "sample_mismatches": 0,
        "ack_eligible": 0,
        "ack_prepares": 3,
        "ack_handoffs": 3,
        "async_rate_deferrals": 4,
        "async_fragment_deferrals": 5,
        "async_wake_attempts": 3,
        "async_ack_handoffs": 3,
        "async_wake_no_handoff": 0,
        "rx_rejections": 0,
        "tx_ack_rejections": 0,
        "rx_drained": 0,
        "drains": 0,
        "failures": 0,
        "last_failure": 0,
    }
    values.update(overrides)
    return values


def status_row(prefix: str, values: dict[str, int]) -> str:
    tokens: list[str] = []
    for name, value in values.items():
        if name in ("public_mask", "private_mask"):
            tokens.append(f"{name}=0x{value:02x}")
        else:
            tokens.append(f"{name}={value}")
    return prefix + " " + " ".join(tokens)


def impairment_rows(seed: int, **counter_overrides: int) -> str:
    counters = {
        "seen": 200,
        "dropped": 0,
        "burst_dropped": 0,
        "reordered": 0,
        "duplicated": 0,
        "corrupted": 0,
        "upstream_stalled": 0,
        "throttled": 0,
        "overflow": 0,
        "resets": 0,
    }
    counters.update(counter_overrides)
    return (
        f"net_impair: enabled=1 seed={seed} latency=25 jitter=0 "
        "loss=0.00 burst=0.00/3 reorder=0.00 duplicate=0.00 "
        "corrupt=0.00 upstream_stall=0 rate_kbps=0 "
        "queue=1/1024 high_water=9\n"
        "net_impair counters: "
        + " ".join(f"{name}={value}" for name, value in counters.items())
    )


def runtime_texts(
    *,
    port: int = 28017,
    client_marker: str = "client_complete",
    server_marker: str = "server_complete",
    reliable_record_count: int = SMOKE.FRAGMENT_RELIABLE_RECORD_COUNT,
) -> tuple[str, str]:
    client = (
        f"Connected to {SMOKE.ADDRESS}:{port} (protocol {SMOKE.PROTOCOL}).\n"
        f"Serverdata packet received (protocol={SMOKE.PROTOCOL})\n"
        + "\n".join(SMOKE.reliable_payloads(reliable_record_count))
        + "\n"
        + status_row(SMOKE.CLIENT_STATUS_PREFIX, client_status())
        + "\n"
        + impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        + "\n"
        + client_marker
        + "\n"
    )
    server = (
        f"SpawnServer: {SMOKE.MAP_NAME}\n"
        "Going from cs_primed to cs_spawned for native_shadow_probe\n"
        + status_row(SMOKE.SERVER_STATUS_PREFIX, server_status())
        + "\n"
        + impairment_rows(SMOKE.SERVER_IMPAIR_SEED)
        + "\n"
        + server_marker
        + "\n"
    )
    return client, server


def replace_status(text: str, prefix: str, values: dict[str, int]) -> str:
    lines = text.splitlines()
    replacement = status_row(prefix, values)
    return "\n".join(replacement if prefix in line else line for line in lines) + "\n"


def valid_profile() -> dict[str, object]:
    return SMOKE.build_profile({
        SMOKE.FRAGMENT_TRIAL: 28017,
        SMOKE.ASYNC_TRIAL: 28018,
    })


class NativeShadowStatusParserTests(unittest.TestCase):
    def test_complete_runtime_evidence_passes(self) -> None:
        client, server = runtime_texts()
        evidence = SMOKE.validate_runtime_text(
            client,
            server,
            client_marker="client_complete",
            server_marker="server_complete",
            port=28017,
        )
        self.assertEqual(evidence["client_status"]["private_mask"], 0x13)
        self.assertEqual(
            evidence["server_status"]["async_ack_handoffs"], 3
        )
        self.assertTrue(
            evidence["reliable_delivery"]["complete_exact_once"]
        )

    def test_status_parser_rejects_missing_and_duplicate_rows(self) -> None:
        client, _ = runtime_texts()
        with self.assertRaisesRegex(RuntimeError, "missing WORR_NATIVE_SERVER"):
            SMOKE.parse_server_status(client)
        duplicate = client + status_row(
            SMOKE.CLIENT_STATUS_PREFIX, client_status()
        )
        with self.assertRaisesRegex(RuntimeError, "duplicate WORR_NATIVE_CLIENT"):
            SMOKE.parse_client_status(duplicate)

    def test_status_parser_rejects_missing_duplicate_and_malformed_fields(self) -> None:
        values = client_status()
        del values["acknowledged_reliable"]
        with self.assertRaisesRegex(RuntimeError, "missing fields"):
            SMOKE.parse_client_status(
                status_row(SMOKE.CLIENT_STATUS_PREFIX, values)
            )
        row = status_row(SMOKE.CLIENT_STATUS_PREFIX, client_status())
        with self.assertRaisesRegex(RuntimeError, "duplicate or empty"):
            SMOKE.parse_client_status(row + " mode=2")
        with self.assertRaisesRegex(RuntimeError, "non-numeric"):
            SMOKE.parse_client_status(row.replace("mode=2", "mode=active"))

    def test_wrong_protocol_or_masks_are_rejected_on_both_endpoints(self) -> None:
        for endpoint, validator, base in (
            ("client", SMOKE.validate_client_status, client_status),
            ("server", SMOKE.validate_server_status, server_status),
        ):
            for field, value in (
                ("protocol", 36),
                ("public_mask", 0x13),
                ("private_mask", 0x03),
            ):
                with self.subTest(endpoint=endpoint, field=field):
                    with self.assertRaisesRegex(RuntimeError, field):
                        validator(base(**{field: value}))

    def test_readiness_chain_and_exact_command_counts_are_required(self) -> None:
        client_zero = (
            "challenges",
            "client_ready_queued",
            "server_active",
            "proof_enqueued",
            "tx_first_sends",
            "tx_handoffs",
            "ack_carriers",
            "acknowledged_reliable",
        )
        for field in client_zero:
            with self.subTest(endpoint="client", field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_client_status(client_status(**{field: 0}))
        server_zero = (
            "challenges_queued",
            "client_ready",
            "server_active",
            "rx_carriers",
            "rx_commits",
            "legacy_joins",
            "command_matches",
        )
        for field in server_zero:
            with self.subTest(endpoint="server", field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_server_status(server_status(**{field: 0}))
        with self.assertRaisesRegex(RuntimeError, "rx_commits"):
            SMOKE.validate_server_status(server_status(rx_commits=2))
        with self.assertRaisesRegex(RuntimeError, "command_matches"):
            SMOKE.validate_server_status(server_status(command_matches=2))

    def test_each_trial_requires_only_its_scheduler_evidence(self) -> None:
        for spec in SMOKE.TRIAL_SPECS:
            for field in spec.required_server_evidence:
                with self.subTest(trial=spec.name, field=field):
                    with self.assertRaisesRegex(RuntimeError, field):
                        SMOKE.validate_server_status(
                            server_status(**{field: 0}),
                            required_evidence=spec.required_server_evidence,
                        )
        SMOKE.validate_server_status(
            server_status(
                async_wake_attempts=0,
                async_ack_handoffs=0,
            ),
            required_evidence=SMOKE.TRIAL_SPECS[0].required_server_evidence,
        )
        SMOKE.validate_server_status(
            server_status(async_fragment_deferrals=0),
            required_evidence=SMOKE.TRIAL_SPECS[1].required_server_evidence,
        )

    def test_ack_handoff_must_be_classified_as_asynchronous(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "async_ack_handoffs"):
            SMOKE.validate_server_status(
                server_status(async_ack_handoffs=0, ack_handoffs=3),
                required_evidence=("async_ack_handoffs",),
            )
        with self.assertRaisesRegex(RuntimeError, "exceed all handoffs"):
            SMOKE.validate_server_status(
                server_status(async_ack_handoffs=4, ack_handoffs=3,
                              async_wake_attempts=4, ack_prepares=4)
            )

    def test_client_must_prove_retained_transition_and_ack_release(self) -> None:
        for field, value in (
            ("retained", 1),
            ("retained_highwater", 0),
            ("retained_releases", 0),
            ("acknowledged_reliable", 0),
        ):
            with self.subTest(field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_client_status(client_status(**{field: value}))

    def test_nonzero_mismatch_rejection_drain_and_failure_are_rejected(self) -> None:
        for field in (
            "command_mismatches",
            "sample_mismatches",
            "rx_rejections",
            "tx_ack_rejections",
            "rx_drained",
            "drains",
            "failures",
            "last_failure",
        ):
            with self.subTest(endpoint="server", field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_server_status(server_status(**{field: 1}))
        for field in ("drains", "failures", "last_failure"):
            with self.subTest(endpoint="client", field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_client_status(client_status(**{field: 1}))

    def test_status_pair_rejects_epoch_disagreement(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "transport_epoch"):
            SMOKE.validate_status_pair(
                client_status(), server_status(transport_epoch=74)
            )

    def test_terminal_order_rejects_missing_duplicate_and_late_status(self) -> None:
        status = status_row(SMOKE.CLIENT_STATUS_PREFIX, client_status())
        with self.assertRaisesRegex(RuntimeError, "count is 0"):
            SMOKE.validate_terminal_order(
                status, status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                completion_marker="done"
            )
        with self.assertRaisesRegex(RuntimeError, "count is 2"):
            SMOKE.validate_terminal_order(
                status + "\ndone\ndone\n",
                status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                completion_marker="done",
            )
        with self.assertRaisesRegex(RuntimeError, "did not precede"):
            SMOKE.validate_terminal_order(
                "done\n" + status,
                status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                completion_marker="done",
            )

    def test_terminal_order_rejects_failure_before_or_after_completion(self) -> None:
        status = status_row(SMOKE.CLIENT_STATUS_PREFIX, client_status())
        for text, relation in (
            ("was dropped:\n" + status + "\ndone\n", "before"),
            (status + "\ndone\nServer disconnected\n", "after"),
        ):
            with self.subTest(relation=relation):
                with self.assertRaisesRegex(RuntimeError, relation):
                    SMOKE.validate_terminal_order(
                        text,
                        status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                        completion_marker="done",
                    )
        with self.assertRaisesRegex(RuntimeError, "Cbuf_AddText"):
            SMOKE.validate_terminal_order(
                status + "\nCbuf_AddText: overflow\ndone\n",
                status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                completion_marker="done",
            )

    def test_reliable_delivery_rejects_missing_and_duplicate_records(self) -> None:
        payloads = SMOKE.reliable_payloads()
        valid = "\n".join(payloads) + "\n"
        evidence = SMOKE.validate_reliable_delivery(valid)
        self.assertEqual(evidence["record_count"], len(payloads))
        with self.assertRaisesRegex(RuntimeError, "PRINT_07"):
            SMOKE.validate_reliable_delivery(
                "\n".join(payloads[:7] + payloads[8:]) + "\n"
            )
        with self.assertRaisesRegex(RuntimeError, "prefix_count=2"):
            SMOKE.validate_reliable_delivery(valid + payloads[3] + "\n")


class NativeShadowImpairmentParserTests(unittest.TestCase):
    def test_latency_only_profile_and_counters_pass(self) -> None:
        config, counters = SMOKE.parse_impairment_status(
            impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        )
        SMOKE.validate_impairment_status(
            config, counters, expected_seed=SMOKE.CLIENT_IMPAIR_SEED
        )

    def test_impairment_parser_rejects_missing_and_duplicate_rows(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "missing net_impair profile"):
            SMOKE.parse_impairment_status("unrelated")
        rows = impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        with self.assertRaisesRegex(RuntimeError, "duplicate net_impair profile"):
            SMOKE.parse_impairment_status(rows + "\n" + rows)

    def test_wrong_impairment_profile_fields_are_rejected(self) -> None:
        config, counters = SMOKE.parse_impairment_status(
            impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        )
        for field, value in (
            ("enabled", 0),
            ("seed", 1),
            ("latency", 0),
            ("loss", 1.0),
            ("rate_kbps", 1),
            ("queue_limit", 7),
            ("high_water", 0),
        ):
            with self.subTest(field=field):
                changed = dict(config)
                changed[field] = value
                with self.assertRaisesRegex(RuntimeError, field.replace("high_water", "latency queue")):
                    SMOKE.validate_impairment_status(
                        changed, counters,
                        expected_seed=SMOKE.CLIENT_IMPAIR_SEED,
                    )

    def test_nonzero_impairment_failures_are_rejected(self) -> None:
        config, counters = SMOKE.parse_impairment_status(
            impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        )
        for field in (
            "dropped",
            "burst_dropped",
            "reordered",
            "duplicated",
            "corrupted",
            "upstream_stalled",
            "throttled",
            "overflow",
            "resets",
        ):
            with self.subTest(field=field):
                changed = dict(counters)
                changed[field] = 1
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_impairment_status(
                        config, changed,
                        expected_seed=SMOKE.CLIENT_IMPAIR_SEED,
                    )
        changed = dict(counters)
        changed["seen"] = 0
        with self.assertRaisesRegex(RuntimeError, "no packets"):
            SMOKE.validate_impairment_status(
                config, changed, expected_seed=SMOKE.CLIENT_IMPAIR_SEED
            )


class NativeShadowCommandTests(unittest.TestCase):
    def test_waits_are_chunked_at_the_engine_limit(self) -> None:
        self.assertEqual(
            SMOKE.wait_commands(2501),
            ["+wait", "1000", "+wait", "1000", "+wait", "501"],
        )

    def test_reliable_burst_is_valid_bounded_print_traffic(self) -> None:
        payloads = SMOKE.reliable_payloads()
        self.assertEqual(len(payloads), 16)
        self.assertEqual(sum(map(len, payloads)), 12800)
        self.assertEqual(len(set(payloads)), len(payloads))
        for payload in payloads:
            self.assertEqual(len(payload.encode("ascii")), 800)
            self.assertTrue(
                payload.startswith("WORR_NATIVE_RELIABLE_PRINT_")
            )
            self.assertNotIn('"', payload)

    def test_server_command_pins_real_rate_and_fragment_profile(self) -> None:
        command = SMOKE.server_command(
            Path("server.exe"),
            port=28017,
            client_completion_marker="client_done",
            server_completion_marker="server_done",
        )
        settings = {
            command[index + 1]: command[index + 2]
            for index, value in enumerate(command[:-2])
            if value == "+set"
        }
        self.assertEqual(settings["net_port"], "28017")
        self.assertEqual(settings["net_maxmsglen"], "512")
        self.assertEqual(settings["sv_min_rate"], "1500")
        self.assertEqual(settings["sv_max_rate"], "1500")
        self.assertEqual(settings["sv_lan_force_rate"], "0")
        self.assertEqual(settings["sv_fps"], str(SMOKE.SERVER_FPS))
        self.assertEqual(settings["sv_worr_native_shadow"], "1")
        printed = [
            command[index + 1]
            for index, value in enumerate(command[:-1])
            if value == "+printall"
        ]
        self.assertEqual(
            len(printed), SMOKE.FRAGMENT_RELIABLE_RECORD_COUNT
        )
        self.assertTrue(
            all(value.startswith('"WORR_NATIVE_RELIABLE_PRINT_')
                for value in printed)
        )
        control = command[command.index("+stuffall") + 1]
        self.assertNotIn("worr_dm_initial_spectate", control)
        self.assertIn("cl_worr_native_shadow_probe_hold 0", control)
        self.assertIn("echo client_done", control)
        self.assertLess(
            len(subprocess.list2cmdline(command)),
            SMOKE.WINDOWS_COMMAND_LINE_LIMIT,
        )

    def test_client_stays_held_until_stuffed_relative_control(self) -> None:
        command = SMOKE.client_command(Path("client.exe"), port=28017)
        early_hold = [
            index for index, value in enumerate(command[:-2])
            if value == "+set"
            and command[index + 1] == "cl_worr_native_shadow_probe_hold"
        ]
        self.assertEqual(len(early_hold), 1)
        self.assertEqual(command[early_hold[0] + 2], "1")
        self.assertNotIn("+cl_worr_native_shadow_probe_hold", command)
        control = SMOKE.client_control_payload("client_done")
        release = control.index("cl_worr_native_shadow_probe_hold 0")
        first_wait = control.index(
            f"wait {SMOKE.FRAGMENT_CLIENT_RELEASE_WAIT_FRAMES}"
        )
        status = control.index("cl_worr_native_shadow_status")
        self.assertLess(first_wait, release)
        self.assertLess(release, status)

    def test_async_trial_uses_proven_post_burst_profile(self) -> None:
        command = SMOKE.server_command(
            Path("server.exe"),
            port=28018,
            client_completion_marker="client_done",
            server_completion_marker="server_done",
            reliable_record_count=SMOKE.ASYNC_RELIABLE_RECORD_COUNT,
            client_release_wait_frames=(
                SMOKE.ASYNC_CLIENT_RELEASE_WAIT_FRAMES
            ),
        )
        printed = [
            value for index, value in enumerate(command)
            if index and command[index - 1] == "+printall"
        ]
        self.assertEqual(len(printed), 8)
        control = command[command.index("+stuffall") + 1]
        self.assertIn("wait 186", control)


class NativeShadowReportHashTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.runtime_records = []
        for role in SMOKE.RUNTIME_ROLES:
            path = self.root / f"runtime-{role}.bin"
            path.write_bytes(role.encode("ascii"))
            self.runtime_records.append(SMOKE.make_file_record(role, path))
        self.log_records = []
        for role in SMOKE.LOG_ROLES:
            path = self.root / f"{role}.log"
            path.write_text(role + "\n", encoding="utf-8")
            self.log_records.append(SMOKE.make_file_record(role, path))
        client_impair, client_counters = SMOKE.parse_impairment_status(
            impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        )
        server_impair, server_counters = SMOKE.parse_impairment_status(
            impairment_rows(SMOKE.SERVER_IMPAIR_SEED)
        )
        trials = {}
        for spec in SMOKE.TRIAL_SPECS:
            delivery_text = "\n".join(
                SMOKE.reliable_payloads(spec.reliable_record_count)
            ) + "\n"
            trials[spec.name] = {
                "statuses": {
                    "client": client_status(),
                    "server": server_status(),
                },
                "reliable_delivery": SMOKE.validate_reliable_delivery(
                    delivery_text,
                    reliable_record_count=spec.reliable_record_count,
                ),
                "impairment": {
                    "client": {
                        "config": client_impair,
                        "counters": client_counters,
                    },
                    "server": {
                        "config": server_impair,
                        "counters": server_counters,
                    },
                },
                "commands": {
                    "client_argc": 10,
                    "client_argv_sha256": "1" * 64,
                    "server_argc": 20,
                    "server_argv_sha256": "2" * 64,
                },
                "processes": {
                    "client_terminated_by_harness": True,
                    "server_terminated_by_harness": True,
                    "elapsed_seconds": 32.0,
                },
            }
        self.report = {
            "schema": SMOKE.SCHEMA,
            "passed": True,
            "profile": valid_profile(),
            "status_contract": {
                "client_prefix": SMOKE.CLIENT_STATUS_PREFIX,
                "client_required_fields": list(SMOKE.CLIENT_STATUS_FIELDS),
                "server_prefix": SMOKE.SERVER_STATUS_PREFIX,
                "server_required_fields": list(SMOKE.SERVER_STATUS_FIELDS),
            },
            "trials": trials,
            "overall_elapsed_seconds": 64.0,
            "runtime_components": self.runtime_records,
            "logs": self.log_records,
            "limitations": ["one", "two", "three", "four"],
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_complete_report_and_hashes_pass(self) -> None:
        SMOKE.validate_report(self.report)

    def test_report_enforces_each_trial_scheduler_contract(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["trials"][SMOKE.FRAGMENT_TRIAL]["statuses"]["server"][
            "async_fragment_deferrals"
        ] = 0
        with self.assertRaisesRegex(RuntimeError, "async_fragment_deferrals"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        async_server = changed["trials"][SMOKE.ASYNC_TRIAL]["statuses"][
            "server"
        ]
        async_server["async_wake_attempts"] = 0
        async_server["async_ack_handoffs"] = 0
        with self.assertRaisesRegex(RuntimeError, "async_wake_attempts"):
            SMOKE.validate_report(changed)

    def test_profile_requires_distinct_trial_ports(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["profile"]["trials"][SMOKE.ASYNC_TRIAL]["port"] = 28017
        with self.assertRaisesRegex(RuntimeError, "distinct ports"):
            SMOKE.validate_report(changed)

    def test_wrong_schema_port_and_profile_are_rejected(self) -> None:
        for path, value, pattern in (
            (("schema",), "v0", "schema"),
            (("profile", "trials", SMOKE.FRAGMENT_TRIAL, "port"), 0, "port"),
            (("profile", "protocol"), 36, "protocol"),
            (("profile", "rate_bytes_per_second"), 15000, "rate"),
            (("profile", "net_maxmsglen"), 1390, "net_maxmsglen"),
        ):
            with self.subTest(path=path):
                changed = copy.deepcopy(self.report)
                target = changed
                for name in path[:-1]:
                    target = target[name]
                target[path[-1]] = value
                with self.assertRaisesRegex(RuntimeError, pattern):
                    SMOKE.validate_report(changed)

    def test_missing_and_duplicate_artifact_roles_are_rejected(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["logs"] = changed["logs"][:-1]
        with self.assertRaisesRegex(RuntimeError, "missing roles"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        changed["runtime_components"].append(
            copy.deepcopy(changed["runtime_components"][0])
        )
        with self.assertRaisesRegex(RuntimeError, "duplicates role"):
            SMOKE.validate_report(changed)

    def test_wrong_executable_and_log_hashes_are_rejected(self) -> None:
        for manifest_name in ("runtime_components", "logs"):
            with self.subTest(manifest=manifest_name):
                changed = copy.deepcopy(self.report)
                changed[manifest_name][0]["sha256"] = "0" * 64
                with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                    SMOKE.validate_report(changed)
                changed = copy.deepcopy(self.report)
                changed[manifest_name][0]["sha256"] = "not-a-hash"
                with self.assertRaisesRegex(RuntimeError, "invalid SHA-256"):
                    SMOKE.validate_report(changed)

    def test_changed_hashed_file_is_rejected(self) -> None:
        path = Path(self.report["logs"][0]["path"])
        path.write_text("changed\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "(byte count|mtime|SHA-256)"):
            SMOKE.validate_report(self.report)

    def test_command_hash_and_harness_lifetime_are_required(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["trials"][SMOKE.FRAGMENT_TRIAL]["commands"][
            "client_argv_sha256"
        ] = "bad"
        with self.assertRaisesRegex(RuntimeError, "command SHA-256"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        changed["trials"][SMOKE.ASYNC_TRIAL]["processes"][
            "server_terminated_by_harness"
        ] = False
        with self.assertRaisesRegex(RuntimeError, "harness-terminated"):
            SMOKE.validate_report(changed)

    def test_previous_pass_and_failure_are_invalidated(self) -> None:
        output = self.root / "native-shadow-runtime.json"
        failure = output.with_suffix(".failure.json")
        output.write_text("stale pass", encoding="utf-8")
        failure.write_text("stale failure", encoding="utf-8")
        self.assertEqual(SMOKE.invalidate_previous_outputs(output), failure)
        self.assertFalse(output.exists())
        self.assertFalse(failure.exists())


if __name__ == "__main__":
    unittest.main()
