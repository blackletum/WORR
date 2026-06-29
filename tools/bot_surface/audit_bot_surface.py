#!/usr/bin/env python3
"""Audit WORR's public bot cvar and command surface."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass, field
from typing import Iterable


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

SOURCE_GLOBS = (
    "src/server/main.c",
    "src/server/commands.c",
    "src/game/sgame/bots/*.cpp",
    "src/game/sgame/gameplay/*.cpp",
    "src/game/sgame/commands/*.cpp",
    "src/game/sgame/g_local.hpp",
)

USER_DOC_GLOBS = (
    "docs-user/*.md",
)

REQUIRED_CVARS = {
    "bot_enable": "1",
    "bot_min_players": "0",
    "bot_profile": "",
    "bot_skill": "3",
    "bot_behavior_enable": "1",
    "bot_allow_item_timers": "1",
    "bot_item_timer_fuzz_ms": "0",
    "bot_allow_rocketjump": "0",
    "bot_allow_chat": "0",
}

PUBLIC_CVAR_DEFAULTS = {
    **REQUIRED_CVARS,
    "bot_chat_live_events": "0",
    "bot_chat_min_interval_ms": "0",
    "bot_chat_team_only": "0",
    "bot_name_prefix": "B|",
}

PUBLIC_CVARS = frozenset(
    {
        *PUBLIC_CVAR_DEFAULTS.keys(),
    }
)

REQUIRED_COMMANDS = frozenset(
    {
        "addbot",
        "removebot",
        "kickbots",
        "botlist",
        "bot_reload_profiles",
    }
)

FORBIDDEN_CVAR_PREFIXES = ("sv_bot_", "sg_bot_", "smoke_bot_", "smoke_")
FORBIDDEN_COMMAND_PREFIXES = ("sv_bot_", "sg_bot_", "smoke_bot_", "smoke_")
CVAR_GET_RE = re.compile(
    r"\b(?:Cvar_Get|gi\.cvar)\s*\(\s*\"(?P<name>[^\"]+)\"\s*,\s*\"(?P<default>[^\"]*)\"",
    re.MULTILINE,
)
CVAR_SET_RE = re.compile(r"\bCvar_Set\s*\(\s*\"(?P<name>[^\"]+)\"", re.MULTILINE)
EXTERN_CVAR_RE = re.compile(r"\bextern\s+cvar_t\s+\*(?P<name>[A-Za-z0-9_]+)\s*;")
COMMAND_REGISTER_RE = re.compile(r"\bCmd_AddCommand\s*\(\s*\"(?P<name>[^\"]+)\"")
COMMAND_TABLE_RE = re.compile(r"\{\s*\"(?P<name>[^\"]+)\"\s*,\s*SV_Bot[A-Za-z0-9_]*_f\b")
DOC_FORBIDDEN_RE = re.compile(
    r"\b(?:"
    r"sv_bot_[A-Za-z0-9_]*|"
    r"sg_bot_[A-Za-z0-9_]*|"
    r"smoke_bot_[A-Za-z0-9_]*|"
    r"smoke_[A-Za-z0-9_]*|"
    r"bot_[A-Za-z0-9_]*_smoke[A-Za-z0-9_]*"
    r")\b"
)


@dataclass
class SourceRef:
    path: pathlib.Path
    line: int

    def to_json(self, repo_root: pathlib.Path) -> dict[str, object]:
        return {
            "path": self.path.relative_to(repo_root).as_posix(),
            "line": self.line,
        }


@dataclass
class CvarRecord:
    name: str
    defaults: set[str] = field(default_factory=set)
    reads_or_registrations: list[SourceRef] = field(default_factory=list)
    sets: list[SourceRef] = field(default_factory=list)
    declarations: list[SourceRef] = field(default_factory=list)

    @property
    def classification(self) -> str:
        return classify_cvar(self.name)

    def to_json(self, repo_root: pathlib.Path) -> dict[str, object]:
        return {
            "name": self.name,
            "classification": self.classification,
            "defaults": sorted(self.defaults),
            "reads_or_registrations": [
                ref.to_json(repo_root) for ref in self.reads_or_registrations
            ],
            "sets": [ref.to_json(repo_root) for ref in self.sets],
            "declarations": [ref.to_json(repo_root) for ref in self.declarations],
        }


@dataclass
class CommandRecord:
    name: str
    registrations: list[SourceRef] = field(default_factory=list)

    def to_json(self, repo_root: pathlib.Path) -> dict[str, object]:
        return {
            "name": self.name,
            "registrations": [ref.to_json(repo_root) for ref in self.registrations],
        }


@dataclass
class AuditResult:
    repo_root: pathlib.Path
    cvars: dict[str, CvarRecord] = field(default_factory=dict)
    commands: dict[str, CommandRecord] = field(default_factory=dict)
    user_doc_mentions: dict[str, list[SourceRef]] = field(default_factory=dict)
    user_doc_public_cvar_mentions: dict[str, list[SourceRef]] = field(default_factory=dict)
    violations: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def to_json(self) -> dict[str, object]:
        classifications: dict[str, int] = {}
        for record in self.cvars.values():
            classifications[record.classification] = (
                classifications.get(record.classification, 0) + 1
            )

        return {
            "repo_root": str(self.repo_root),
            "summary": {
                "cvars": len(self.cvars),
                "commands": len(self.commands),
                "classifications": dict(sorted(classifications.items())),
                "violations": len(self.violations),
                "warnings": len(self.warnings),
            },
            "required_cvars": REQUIRED_CVARS,
            "public_cvar_defaults": PUBLIC_CVAR_DEFAULTS,
            "required_commands": sorted(REQUIRED_COMMANDS),
            "cvars": [
                record.to_json(self.repo_root)
                for record in sorted(self.cvars.values(), key=lambda item: item.name)
            ],
            "commands": [
                record.to_json(self.repo_root)
                for record in sorted(self.commands.values(), key=lambda item: item.name)
            ],
            "user_doc_mentions": {
                name: [ref.to_json(self.repo_root) for ref in refs]
                for name, refs in sorted(self.user_doc_mentions.items())
            },
            "user_doc_public_cvar_mentions": {
                name: [ref.to_json(self.repo_root) for ref in refs]
                for name, refs in sorted(self.user_doc_public_cvar_mentions.items())
            },
            "violations": self.violations,
            "warnings": self.warnings,
        }


def classify_cvar(name: str) -> str:
    if name.startswith(FORBIDDEN_CVAR_PREFIXES):
        return "forbidden-prefix"
    if "_smoke" in name or name.endswith("_smoke"):
        return "smoke-only"
    if name.startswith("bot_debug") or name.startswith("bot_nav_debug"):
        return "debug"
    if name in PUBLIC_CVARS:
        return "public"
    if name.startswith("bot_"):
        return "experimental"
    return "other"


def iter_files(repo_root: pathlib.Path, globs: Iterable[str]) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for pattern in globs:
        files.extend(path for path in repo_root.glob(pattern) if path.is_file())
    return sorted(set(files))


def line_for_offset(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def source_ref(path: pathlib.Path, text: str, offset: int) -> SourceRef:
    return SourceRef(path=path, line=line_for_offset(text, offset))


def ensure_cvar(result: AuditResult, name: str) -> CvarRecord:
    record = result.cvars.get(name)
    if record is None:
        record = CvarRecord(name=name)
        result.cvars[name] = record
    return record


def ensure_command(result: AuditResult, name: str) -> CommandRecord:
    record = result.commands.get(name)
    if record is None:
        record = CommandRecord(name=name)
        result.commands[name] = record
    return record


def scan_source_file(result: AuditResult, path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")

    for match in CVAR_GET_RE.finditer(text):
        name = match.group("name")
        if "bot" not in name:
            continue
        record = ensure_cvar(result, name)
        record.defaults.add(match.group("default"))
        record.reads_or_registrations.append(source_ref(path, text, match.start()))

    for match in CVAR_SET_RE.finditer(text):
        name = match.group("name")
        if "bot" not in name:
            continue
        record = ensure_cvar(result, name)
        record.sets.append(source_ref(path, text, match.start()))

    for match in EXTERN_CVAR_RE.finditer(text):
        name = match.group("name")
        if "bot" not in name:
            continue
        record = ensure_cvar(result, name)
        record.declarations.append(source_ref(path, text, match.start()))

    for regex in (COMMAND_REGISTER_RE, COMMAND_TABLE_RE):
        for match in regex.finditer(text):
            name = match.group("name")
            if "bot" not in name:
                continue
            ensure_command(result, name).registrations.append(
                source_ref(path, text, match.start())
            )


def scan_user_doc_file(result: AuditResult, path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    for match in DOC_FORBIDDEN_RE.finditer(text):
        name = match.group(0)
        result.user_doc_mentions.setdefault(name, []).append(
            source_ref(path, text, match.start())
        )
    for name in PUBLIC_CVARS:
        match = re.search(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])", text)
        if match:
            result.user_doc_public_cvar_mentions.setdefault(name, []).append(
                source_ref(path, text, match.start())
            )


def doc_default_value(value: str) -> str:
    if value == "":
        return '""'
    return value.replace("|", r"\|")


def doc_default_row_present(text: str, name: str, value: str) -> bool:
    expected = doc_default_value(value)
    pattern = (
        r"\|\s*`"
        + re.escape(name)
        + r"`\s*\|\s*`"
        + re.escape(expected)
        + r"`\s*\|"
    )
    return re.search(pattern, text) is not None


def validate_result(result: AuditResult) -> None:
    for name, expected_default in PUBLIC_CVAR_DEFAULTS.items():
        record = result.cvars.get(name)
        if record is None:
            result.violations.append(f"missing required bot cvar {name}")
            continue
        if expected_default not in record.defaults:
            defaults = ", ".join(sorted(record.defaults)) or "<none>"
            result.violations.append(
                f"{name} default mismatch: expected {expected_default!r}, found {defaults}"
            )

    for name in REQUIRED_COMMANDS:
        record = result.commands.get(name)
        if record is None or not record.registrations:
            result.violations.append(f"missing required bot command {name}")

    for record in sorted(result.cvars.values(), key=lambda item: item.name):
        if record.name.startswith(FORBIDDEN_CVAR_PREFIXES):
            refs = record.reads_or_registrations or record.sets or record.declarations
            where = refs[0].to_json(result.repo_root) if refs else {}
            result.violations.append(
                f"forbidden bot cvar prefix in active source: {record.name} at {where}"
            )

    for record in sorted(result.commands.values(), key=lambda item: item.name):
        if record.name.startswith(FORBIDDEN_COMMAND_PREFIXES):
            refs = record.registrations
            where = refs[0].to_json(result.repo_root) if refs else {}
            result.violations.append(
                f"forbidden bot command prefix in active source: {record.name} at {where}"
            )

    for name, refs in sorted(result.user_doc_mentions.items()):
        result.violations.append(
            "forbidden legacy/smoke-prefixed bot token in user docs: "
            f"{name} at {refs[0].to_json(result.repo_root)}"
        )

    for name in sorted(PUBLIC_CVARS):
        if name not in result.user_doc_public_cvar_mentions:
            result.violations.append(f"public bot cvar not documented in user docs: {name}")

    defaults_doc = result.repo_root / "docs-user" / "bot-cvars.md"
    if not defaults_doc.is_file():
        result.violations.append("missing public bot cvar defaults doc: docs-user/bot-cvars.md")
    else:
        text = defaults_doc.read_text(encoding="utf-8", errors="replace")
        for name, expected_default in sorted(PUBLIC_CVAR_DEFAULTS.items()):
            if not doc_default_row_present(text, name, expected_default):
                result.violations.append(
                    "docs-user/bot-cvars.md missing public default row: "
                    f"{name}={expected_default!r}"
                )


def audit_repo(repo_root: pathlib.Path = REPO_ROOT) -> AuditResult:
    repo_root = repo_root.resolve()
    result = AuditResult(repo_root=repo_root)

    for path in iter_files(repo_root, SOURCE_GLOBS):
        scan_source_file(result, path)

    for path in iter_files(repo_root, USER_DOC_GLOBS):
        scan_user_doc_file(result, path)

    validate_result(result)
    return result


def format_text(result: AuditResult) -> str:
    payload = result.to_json()
    summary = payload["summary"]
    classifications = summary["classifications"]
    lines = [
        "WORR bot surface audit",
        f"cvars: {summary['cvars']}",
        f"commands: {summary['commands']}",
        "classifications: "
        + ", ".join(f"{name}={count}" for name, count in classifications.items()),
        f"violations: {summary['violations']}",
        f"warnings: {summary['warnings']}",
    ]
    if result.violations:
        lines.append("")
        lines.append("Violations:")
        lines.extend(f"- {violation}" for violation in result.violations)
    if result.warnings:
        lines.append("")
        lines.append("Warnings:")
        lines.extend(f"- {warning}" for warning in result.warnings)
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=pathlib.Path,
        default=REPO_ROOT,
        help="Repository root to audit.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Report format.",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        help="Optional path to write the report.",
    )
    args = parser.parse_args(argv)

    result = audit_repo(args.repo_root)
    if args.format == "json":
        output = json.dumps(result.to_json(), indent=2, sort_keys=True) + "\n"
    else:
        output = format_text(result)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    else:
        print(output, end="")

    return 1 if result.violations else 0


if __name__ == "__main__":
    raise SystemExit(main())
