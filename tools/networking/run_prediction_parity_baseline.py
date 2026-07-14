#!/usr/bin/env python3
"""Verify deterministic production prediction parity evidence."""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import subprocess
from pathlib import Path


REPORT_SCHEMA = "worr.networking.prediction-parity.v1"
WIRE_REPORT_SCHEMA = "worr.networking.usercmd-live-wire-parity.v1"
GOLDEN_SCHEMA = "worr.networking.prediction-parity-golden.v1"
DEFAULT_GOLDEN = (
    Path(__file__).resolve().parent
    / "baselines"
    / "prediction-parity-golden-v1.json"
)
MATRIX_SCHEMA = "worr.networking.prediction-replay-matrix.v1"
DEFAULT_MATRIX = (
    Path(__file__).resolve().parent
    / "scenarios"
    / "prediction_replay_matrix.json"
)
FNV1A_64_OFFSET = 14695981039346656037
FNV1A_64_PRIME = 1099511628211
U64_MASK = (1 << 64) - 1


def append_u64(hash_value: int, value: int) -> int:
    for shift in range(0, 64, 8):
        hash_value ^= (value >> shift) & 0xFF
        hash_value = (hash_value * FNV1A_64_PRIME) & U64_MASK
    return hash_value


def fail_closed_name_transcript(case_names: list[str]) -> str:
    hash_value = FNV1A_64_OFFSET
    for name in case_names:
        encoded = name.encode("utf-8")
        hash_value = append_u64(hash_value, len(encoded))
        for value in encoded:
            hash_value ^= value
            hash_value = (hash_value * FNV1A_64_PRIME) & U64_MASK
    return f"{hash_value:016x}"


def run_harness(executable: Path) -> dict[str, object]:
    completed = subprocess.run(
        (str(executable), "--json"),
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    report = json.loads(completed.stdout)
    if report.get("schema") != REPORT_SCHEMA:
        raise RuntimeError("unexpected prediction parity report schema")
    return report


def run_wire_harness(executable: Path) -> dict[str, object]:
    completed = subprocess.run(
        (str(executable), "--json"),
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    report = json.loads(completed.stdout)
    if report.get("schema") != WIRE_REPORT_SCHEMA:
        raise RuntimeError("unexpected usercmd live-wire report schema")
    return report


def require_hash(value: object, label: str) -> None:
    if (
        not isinstance(value, str)
        or len(value) != 16
        or value == "0000000000000000"
    ):
        raise RuntimeError(f"{label} is not a non-zero 64-bit hash")
    try:
        int(value, 16)
    except ValueError as error:
        raise RuntimeError(f"{label} is not hexadecimal") from error


def validate_float_bits(value: object, label: str) -> int:
    count = 0
    if isinstance(value, dict):
        if "value" in value:
            bits = value.get("bits")
            if not isinstance(bits, str) or len(bits) != 8:
                raise RuntimeError(f"{label} float value has invalid bit field")
            try:
                int(bits, 16)
            except ValueError as error:
                raise RuntimeError(
                    f"{label} float bit field is not hexadecimal"
                ) from error
            count += 1
        for key, child in value.items():
            count += validate_float_bits(child, f"{label}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            count += validate_float_bits(child, f"{label}[{index}]")
    return count


def validate_focused_output(value: object, label: str) -> None:
    if not isinstance(value, dict):
        raise RuntimeError(f"{label} is not an object")
    output = value.get("output")
    native_output = value.get("native_output")
    if not isinstance(output, dict) or not isinstance(native_output, dict):
        raise RuntimeError(f"{label} output pair is missing")
    require_hash(output.get("state_hash"), f"{label}.state_hash")
    require_hash(output.get("collision_hash"), f"{label}.collision_hash")
    require_hash(native_output.get("state_hash"),
                 f"{label}.native_state_hash")
    if output["state_hash"] != native_output["state_hash"]:
        raise RuntimeError(f"{label} native state hash diverged")
    if value.get("full_output_equal") is not True:
        raise RuntimeError(f"{label} full output parity is false")
    if validate_float_bits(value, label) <= 0:
        raise RuntimeError(f"{label} contains no exact float-bit evidence")


def validate_report(
    report: dict[str, object], matrix: dict[str, object]
) -> None:
    if report.get("abi_version") != 1:
        raise RuntimeError("unexpected prediction ABI version")
    if report.get("movement_model_revision") != 1:
        raise RuntimeError("unexpected prediction movement model revision")
    if report.get("parity_scope") != "abi-client-vs-native-server-core":
        raise RuntimeError("prediction evidence overstates or changes parity scope")
    if report.get("full_output_scope") != (
        "all-pmove-value-outputs-excluding-native-collision-hash"
    ):
        raise RuntimeError("prediction full-output scope changed")
    if report.get("wire_parity_scope") != "not-covered-by-this-harness":
        raise RuntimeError("prediction harness wire scope changed")
    if report.get("bridge_repeatability") is not True:
        raise RuntimeError("independent ABI bridge repeatability is missing")

    nested = report.get("nested_step")
    if not isinstance(nested, dict):
        raise RuntimeError("nested prediction evidence is missing")
    for key in (
        "outer_state",
        "outer_collision",
        "inner_state",
        "inner_collision",
    ):
        require_hash(nested.get(key), f"nested_step.{key}")

    hashes = report.get("hash_contract")
    if not isinstance(hashes, dict):
        raise RuntimeError("prediction hash contract is missing")
    for key in ("state", "command", "config"):
        require_hash(hashes.get(key), f"hash_contract.{key}")

    matrix_scenarios = matrix.get("scenarios")
    if not isinstance(matrix_scenarios, list) or not matrix_scenarios:
        raise RuntimeError("prediction scenario matrix is empty")
    expected_scenarios = {
        str(scenario["name"]): int(scenario["commands"])
        for scenario in matrix_scenarios
        if isinstance(scenario, dict)
        and "name" in scenario
        and "commands" in scenario
    }
    if len(expected_scenarios) != len(matrix_scenarios):
        raise RuntimeError("prediction scenario matrix has invalid entries")

    scenarios = report.get("scenarios")
    if not isinstance(scenarios, list):
        raise RuntimeError("prediction scenarios are missing")
    names = tuple(
        scenario.get("name") if isinstance(scenario, dict) else None
        for scenario in scenarios
    )
    if names != tuple(expected_scenarios):
        raise RuntimeError(f"unexpected prediction scenario order: {names}")
    for scenario in scenarios:
        if not isinstance(scenario, dict):
            raise RuntimeError("prediction scenario entry is not an object")
        name = str(scenario["name"])
        if scenario.get("command_count") != expected_scenarios[name]:
            raise RuntimeError(f"{name} command count does not match matrix")
        for key in (
            "command_transcript",
            "final_state",
            "native_server_state",
            "state_transcript",
            "collision_transcript",
            "replay_chain",
        ):
            require_hash(scenario.get(key), f"{name}.{key}")
        if scenario["final_state"] != scenario["native_server_state"]:
            raise RuntimeError(f"{name} native server state is not in parity")
        if scenario.get("full_output_parity") is not True:
            raise RuntimeError(f"{name} full output is not in parity")
        if int(scenario.get("collision_queries", 0)) <= 0:
            raise RuntimeError(f"{name} issued no collision queries")

    matrix_coverage = matrix.get("coverage_cases")
    if not isinstance(matrix_coverage, list) or not matrix_coverage:
        raise RuntimeError("prediction surface coverage matrix is empty")
    expected_coverage = tuple(
        (item.get("name"), item.get("category"))
        if isinstance(item, dict)
        else (None, None)
        for item in matrix_coverage
    )
    if any(not name or not category for name, category in expected_coverage):
        raise RuntimeError("prediction surface coverage matrix has invalid entries")
    coverage = report.get("coverage_cases")
    if not isinstance(coverage, list):
        raise RuntimeError("prediction surface coverage evidence is missing")
    actual_coverage = tuple(
        (item.get("name"), item.get("category"))
        if isinstance(item, dict)
        else (None, None)
        for item in coverage
    )
    if actual_coverage != expected_coverage:
        raise RuntimeError("prediction surface coverage order drifted from matrix")
    for item in coverage:
        if not isinstance(item, dict):
            raise RuntimeError("prediction coverage entry is not an object")
        name = str(item["name"])
        for key in (
            "command",
            "config",
            "final_state",
            "native_server_state",
            "collision",
        ):
            require_hash(item.get(key), f"coverage.{name}.{key}")
        if item["final_state"] != item["native_server_state"]:
            raise RuntimeError(f"coverage {name} is not in native parity")
        if item.get("full_output_parity") is not True:
            raise RuntimeError(f"coverage {name} full output is not in parity")
        if int(item.get("collision_queries", -1)) < 0:
            raise RuntimeError(f"coverage {name} has invalid query accounting")

    coverage_by_name = {str(item["name"]): item for item in coverage}
    q3_control = coverage_by_name.get("config-q3-control-default")
    q3_enabled = coverage_by_name.get("config-q3-overbounce")
    if not isinstance(q3_control, dict) or not isinstance(q3_enabled, dict):
        raise RuntimeError("Q3 overbounce differential controls are missing")
    if q3_control["command"] != q3_enabled["command"]:
        raise RuntimeError("Q3 overbounce differential commands do not match")
    if q3_control["config"] == q3_enabled["config"]:
        raise RuntimeError("Q3 overbounce differential configs are identical")
    if q3_control["final_state"] == q3_enabled["final_state"]:
        raise RuntimeError("Q3 overbounce did not change wall-collision physics")
    if min(
        int(q3_control["collision_queries"]),
        int(q3_enabled["collision_queries"]),
    ) <= 0:
        raise RuntimeError("Q3 overbounce differential issued no collision query")

    matrix_focused = matrix.get("focused_fixtures")
    if not isinstance(matrix_focused, list) or not matrix_focused:
        raise RuntimeError("focused prediction fixture matrix is missing")
    focused = report.get("focused_fixtures")
    if not isinstance(focused, list):
        raise RuntimeError("focused prediction evidence is missing")
    expected_focused_order: list[tuple[str, str]] = []
    focused_requirements: dict[str, list[str]] = {}
    for specification in matrix_focused:
        if not isinstance(specification, dict):
            raise RuntimeError("focused fixture specification is not an object")
        name = specification.get("name")
        category = specification.get("category")
        requires = specification.get("requires")
        if (
            not isinstance(name, str)
            or not name
            or not isinstance(category, str)
            or not category
            or not isinstance(requires, list)
            or not requires
            or not all(isinstance(item, str) and item for item in requires)
            or len(set(requires)) != len(requires)
        ):
            raise RuntimeError("focused fixture specification is invalid")
        expected_focused_order.append((name, category))
        focused_requirements[name] = requires
    if len(focused_requirements) != len(matrix_focused):
        raise RuntimeError("focused fixture names are not unique")
    actual_focused_order = [
        (item.get("name"), item.get("category"))
        if isinstance(item, dict)
        else (None, None)
        for item in focused
    ]
    if actual_focused_order != expected_focused_order:
        raise RuntimeError("focused fixture name/category order drifted")

    for item in focused:
        if not isinstance(item, dict):
            raise RuntimeError("focused fixture evidence is not an object")
        name = str(item["name"])
        validate_focused_output(item, f"focused.{name}")
        assertions = item.get("behaviour_assertions")
        if not isinstance(assertions, dict) or not assertions:
            raise RuntimeError(f"focused {name} has no behaviour assertions")
        if any(value is not True for value in assertions.values()):
            raise RuntimeError(f"focused {name} has a false behaviour assertion")
        for requirement in focused_requirements[name]:
            assertion_name = requirement.replace("-", "_")
            if assertions.get(assertion_name) is not True:
                raise RuntimeError(
                    f"focused {name} does not prove requirement {requirement}"
                )
        control = item.get("control")
        if control is not None:
            validate_focused_output(control, f"focused.{name}.control")

    fail_closed = report.get("fail_closed")
    if not isinstance(fail_closed, dict):
        raise RuntimeError("prediction fail-closed evidence is missing")
    matrix_fail_names = matrix.get("fail_closed_cases")
    if (
        not isinstance(matrix_fail_names, list)
        or not matrix_fail_names
        or not all(isinstance(name, str) and name for name in matrix_fail_names)
        or len(set(matrix_fail_names)) != len(matrix_fail_names)
    ):
        raise RuntimeError("prediction fail-closed matrix names are invalid")
    report_fail_names = fail_closed.get("case_names")
    if report_fail_names != matrix_fail_names:
        raise RuntimeError("prediction fail-closed ordered case names drifted")
    if fail_closed.get("passed_cases") != len(matrix_fail_names):
        raise RuntimeError("prediction fail-closed case count drifted")
    require_hash(fail_closed.get("transcript"), "fail_closed.transcript")
    if fail_closed["transcript"] != fail_closed_name_transcript(
        matrix_fail_names
    ):
        raise RuntimeError("prediction fail-closed transcript is not name-bound")

    correction = report.get("correction_replay")
    if not isinstance(correction, dict):
        raise RuntimeError("correction replay evidence is missing")
    matrix_correction = matrix.get("correction_replay")
    if not isinstance(matrix_correction, dict):
        raise RuntimeError("correction replay matrix is missing")
    if correction.get("acknowledged_sequence") != matrix_correction.get(
        "acknowledged_sequence"
    ):
        raise RuntimeError("correction ACK no longer precedes uint32 wrap")
    if correction.get("first_sequence") != matrix_correction.get(
        "first_sequence"
    ):
        raise RuntimeError("unexpected correction first command sequence")
    if correction.get("current_sequence") != matrix_correction.get(
        "current_sequence"
    ):
        raise RuntimeError("correction current sequence no longer follows wrap")
    if correction.get("replayed_commands") != matrix_correction.get(
        "replayed_commands"
    ):
        raise RuntimeError("unexpected correction replay command count")
    if correction.get("commands") != matrix_correction.get("commands"):
        raise RuntimeError("unexpected correction total command count")
    for key in (
        "pre_correction_state",
        "authoritative_state",
        "final_state",
        "sequence_transcript",
        "collision_transcript",
        "replay_chain",
    ):
        require_hash(correction.get(key), f"correction_replay.{key}")
    if correction["pre_correction_state"] == correction["final_state"]:
        raise RuntimeError("correction fixture did not begin divergent")


def validate_wire_report(report: dict[str, object]) -> None:
    for field in (
        "serverdata_serialized_and_parsed",
        "signed_short_angle_exhaustive_identity",
        "multi_turn_angle_stable",
        "atomic_rejection",
    ):
        if report.get(field) is not True:
            raise RuntimeError(f"live-wire evidence field {field} is not true")

    expected_rows = (
        ("vanilla", "move", True, 3),
        ("q2pro", "move", True, 3),
        ("q2pro", "batch_move", True, 8),
        ("q2repro", "move", False, 3),
        ("q2repro", "batch_move", False, 8),
    )
    rows = report.get("rows")
    if not isinstance(rows, list) or len(rows) != len(expected_rows):
        raise RuntimeError("live-wire evidence does not contain five rows")

    observed_durations: set[int] = set()
    for row, expected in zip(rows, expected_rows, strict=True):
        if not isinstance(row, dict):
            raise RuntimeError("live-wire row is not an object")
        protocol, message, has_upmove, command_count = expected
        if (
            row.get("protocol") != protocol
            or row.get("message") != message
            or row.get("has_upmove") is not has_upmove
        ):
            raise RuntimeError(
                "unexpected live-wire protocol/message/upmove row: "
                f"{row.get('protocol')}/{row.get('message')}/"
                f"{row.get('has_upmove')}"
            )
        if row.get("canonical_idempotent") is not True:
            raise RuntimeError(f"{protocol}/{message} is not idempotent")
        if row.get("decoded_equal") is not True:
            raise RuntimeError(f"{protocol}/{message} did not decode equally")
        if not isinstance(row.get("wire_bytes"), int) or row["wire_bytes"] <= 0:
            raise RuntimeError(f"{protocol}/{message} has no wire bytes")
        wire_sha256 = row.get("wire_sha256")
        if not isinstance(wire_sha256, str) or len(wire_sha256) != 64:
            raise RuntimeError(f"{protocol}/{message} has invalid wire SHA-256")
        try:
            int(wire_sha256, 16)
        except ValueError as error:
            raise RuntimeError(
                f"{protocol}/{message} wire SHA-256 is not hexadecimal"
            ) from error

        canonical = row.get("canonical_commands")
        decoded = row.get("decoded_commands")
        if (
            not isinstance(canonical, list)
            or not isinstance(decoded, list)
            or len(canonical) != command_count
            or decoded != canonical
        ):
            raise RuntimeError(
                f"{protocol}/{message} canonical/decoded command rows drifted"
            )
        for command in canonical:
            if not isinstance(command, dict):
                raise RuntimeError("live-wire command is not an object")
            duration = command.get("duration_ms")
            if not isinstance(duration, int) or not 0 <= duration <= 255:
                raise RuntimeError("live-wire duration is outside uint8 range")
            observed_durations.add(duration)
            angles = command.get("angle_float_bits")
            bit_fields = (
                list(angles) if isinstance(angles, list) else []
            ) + [
                command.get("forward_move_float_bits"),
                command.get("side_move_float_bits"),
            ]
            if len(bit_fields) != 5:
                raise RuntimeError("live-wire command angle bit shape drifted")
            for bits in bit_fields:
                if not isinstance(bits, str) or len(bits) != 8:
                    raise RuntimeError("live-wire command float bits are invalid")
                try:
                    int(bits, 16)
                except ValueError as error:
                    raise RuntimeError(
                        "live-wire command float bits are not hexadecimal"
                    ) from error
            require_hash(command.get("prediction_hash"),
                         "live-wire prediction_hash")

    required_durations = {0, 1, 7, 8, 9, 66, 250, 255}
    if not required_durations.issubset(observed_durations):
        raise RuntimeError(
            "live-wire duration boundary coverage drifted: "
            f"{sorted(observed_durations)}"
        )
    if report.get("move_batch_equivalence") != {
        "q2pro": True,
        "q2repro": True,
    }:
        raise RuntimeError("MOVE/BATCH_MOVE equivalence evidence drifted")


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_golden(path: Path, current: dict[str, object]) -> None:
    if not path.is_file():
        raise RuntimeError(
            f"golden prediction baseline is missing: {path}\n"
            "Create it only after reviewing deterministic output with --rebaseline."
        )
    expected = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(expected, dict) or expected.get("schema") != GOLDEN_SCHEMA:
        raise RuntimeError(f"unexpected prediction golden schema in {path}")
    if expected == current:
        return

    expected_lines = json.dumps(expected, indent=2, sort_keys=True).splitlines()
    current_lines = json.dumps(current, indent=2, sort_keys=True).splitlines()
    difference = "\n".join(
        difflib.unified_diff(
            expected_lines,
            current_lines,
            fromfile="checked-in-golden",
            tofile="current-prediction-output",
            lineterm="",
        )
    )
    raise RuntimeError(
        "deterministic prediction output drifted from the checked-in golden "
        f"baseline:\n{difference}\n"
        "Review the movement/ABI change, then use --rebaseline only when the "
        "new behavior is intentional."
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness-exe", required=True, type=Path)
    parser.add_argument("--wire-harness-exe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scenarios", type=Path, default=DEFAULT_MATRIX)
    parser.add_argument("--golden", type=Path, default=DEFAULT_GOLDEN)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument(
        "--rebaseline",
        action="store_true",
        help="replace --golden after reviewing intentional prediction drift",
    )
    args = parser.parse_args()
    if args.repeat < 2:
        parser.error("--repeat must be at least 2")

    matrix = json.loads(args.scenarios.read_text(encoding="utf-8"))
    if not isinstance(matrix, dict) or matrix.get("schema") != MATRIX_SCHEMA:
        raise RuntimeError("unexpected prediction replay matrix schema")
    if matrix.get("prediction_abi_version") != 1:
        raise RuntimeError("matrix targets an unexpected prediction ABI")
    if matrix.get("movement_model_revision") != 1:
        raise RuntimeError("matrix targets an unexpected movement model")

    reports = [run_harness(args.harness_exe) for _ in range(args.repeat)]
    canonical = reports[0]
    validate_report(canonical, matrix)
    if any(report != canonical for report in reports[1:]):
        raise RuntimeError("prediction parity output was not repeatable")

    wire_reports = [
        run_wire_harness(args.wire_harness_exe) for _ in range(args.repeat)
    ]
    canonical_wire = wire_reports[0]
    validate_wire_report(canonical_wire)
    if any(report != canonical_wire for report in wire_reports[1:]):
        raise RuntimeError("usercmd live-wire output was not repeatable")

    golden = {
        "schema": GOLDEN_SCHEMA,
        "matrix": matrix,
        "report": canonical,
        "wire_report": canonical_wire,
    }
    if args.rebaseline:
        write_json(args.golden, golden)
        print(f"rebaselined {args.golden}")
    else:
        verify_golden(args.golden, golden)

    evidence = {
        "schema": "worr.networking.prediction-parity-evidence.v1",
        "repeat_runs": args.repeat,
        "deterministic": True,
        "golden_schema": GOLDEN_SCHEMA,
        # Rebaseline creates the candidate that a later, independent run must
        # verify.  Do not let the creation run attest to its own output.
        "golden_verified": not args.rebaseline,
        "rebaselined": args.rebaseline,
        "golden_path": str(args.golden.resolve()),
        "golden_sha256": file_sha256(args.golden),
        "matrix_schema": MATRIX_SCHEMA,
        "matrix_path": str(args.scenarios.resolve()),
        "matrix_sha256": file_sha256(args.scenarios),
        "report": canonical,
        "wire_harness_path": str(args.wire_harness_exe.resolve()),
        "wire_repeat_runs": args.repeat,
        "wire_report": canonical_wire,
    }
    write_json(args.output, evidence)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
