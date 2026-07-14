#!/usr/bin/env python3
"""Validate the live RmlUi current-match statistics provider."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
RUNTIME_ROUTES = Path("src/client/ui_rml/ui_rml.cpp")
CGAME_UI = Path("src/game/cgame/ui/ui_core.cpp")
PUBLISHER = Path("src/game/sgame/menu/menu_page_matchstats.cpp")
CLIENT_COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
PLAYER_VIEW = Path("src/game/sgame/player/p_view.cpp")
G_LOCAL = Path("src/game/sgame/g_local.hpp")
DOCUMENT = Path("assets/ui/rml/session/match_stats.rml")
SESSION_THEME = Path("assets/ui/rml/common/theme/session.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
SESSION_ROUTES = Path("assets/ui/rml/session/routes.json")
MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

ROUTE_ID = "match_stats"
STAT_BINDINGS = (
    "player",
    "kills",
    "deaths",
    "kd",
    "damage_dealt",
    "damage_received",
    "damage_ratio",
    "shots",
    "hits",
    "accuracy",
)


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def _read(path: Path, errors: list[str]) -> str:
    if not path.is_file():
        errors.append(f"missing required file: {path.as_posix()}")
        return ""
    return path.read_text(encoding="utf-8")


def _require(source: str, tokens: tuple[str, ...], label: str, errors: list[str]) -> None:
    for token in tokens:
        if token not in source:
            errors.append(f"{label} is missing token: {token}")


def _parse_document(repo_root: Path, errors: list[str]) -> ElementTree.Element | None:
    source = _read(repo_root / DOCUMENT, errors)
    if not source:
        return None
    try:
        return ElementTree.fromstring(source)
    except ElementTree.ParseError as exc:
        errors.append(f"{DOCUMENT.as_posix()}: invalid XML: {exc}")
        return None


def _elements(root: ElementTree.Element) -> dict[str, ElementTree.Element]:
    return {
        element.attrib["id"]: element
        for element in root.iter()
        if "id" in element.attrib
    }


def _validate_native_sources(repo_root: Path, errors: list[str]) -> None:
    runtime = _read(repo_root / RUNTIME, errors)
    _require(
        runtime,
        (
            "UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)",
            "UI_Rml_ConditionExpressionMatches",
            'GetAttribute<Rml::String>("data-close-command", "")',
            '!strcmp(command, "worr_matchstats_close")',
        ),
        "native match-stats cvar/condition/disconnected-close bridge",
        errors,
    )

    runtime_routes = _read(repo_root / RUNTIME_ROUTES, errors)
    if '{ "match_stats", "session/match_stats.rml" }' not in runtime_routes:
        errors.append("compiled route registry is missing match_stats")
    cgame_ui = _read(repo_root / CGAME_UI, errors)
    if '"match_stats",' not in cgame_ui:
        errors.append("cgame RmlUi route registry is missing match_stats")

    publisher = _read(repo_root / PUBLISHER, errors)
    _require(
        publisher,
        (
            "std::array<std::string, 16> lines{}",
            'fmt::format("ui_matchstats_line_{}", lineIndex)',
            'AppendCvar("ui_matchstats_player"',
            'AppendCvar("ui_matchstats_kills"',
            'AppendCvar("ui_matchstats_deaths"',
            'AppendCvar("ui_matchstats_kd"',
            'AppendCvar("ui_matchstats_damage_dealt"',
            'AppendCvar("ui_matchstats_damage_received"',
            '"ui_matchstats_damage_ratio"',
            'AppendCvar("ui_matchstats_shots"',
            'AppendCvar("ui_matchstats_hits"',
            '"ui_matchstats_accuracy"',
            '"N/A"',
            'AppendCommand("pushmenu match_stats")',
            "matchStatsActive = true",
            "matchStatsNextUpdate = level.time",
        ),
        "sgame semantic match-stats publisher",
        errors,
    )

    player_view = _read(repo_root / PLAYER_VIEW, errors)
    _require(
        player_view,
        (
            "constexpr GameTime kMatchStatsUpdateInterval = 1_sec",
            "!g_matchstats || !g_matchstats->integer",
            "cl->ui.matchStatsActive",
            "RefreshMatchStatsMenu(ent)",
            "cl->ui.matchStatsNextUpdate = level.time + kMatchStatsUpdateInterval",
        ),
        "per-frame match-stats lifecycle",
        errors,
    )

    commands = _read(repo_root / CLIENT_COMMANDS, errors)
    _require(
        commands,
        (
            "OpenPlayerMatchStatsMenu(ent)",
            "ent->client->ui.matchStatsActive = false;",
        ),
        "match-stats open/close commands",
        errors,
    )
    for command in ("worr_matchstats_menu", "worr_matchstats_close"):
        match = re.search(rf'RegisterCommand\("{command}"([^\n]*)', commands)
        if match is None or "AllowIntermission" not in match.group(1):
            errors.append(f"intermission match-stats command is missing: {command}")

    g_local = _read(repo_root / G_LOCAL, errors)
    _require(
        g_local,
        ("bool matchStatsActive = false;", "GameTime matchStatsNextUpdate = 0_ms;"),
        "per-client match-stats state",
        errors,
    )


def _validate_document(repo_root: Path, errors: list[str]) -> None:
    root = _parse_document(repo_root, errors)
    if root is None:
        return
    body = next(root.iter("body"), None)
    if body is None:
        errors.append("match_stats is missing its body")
        return
    expected = {
        "data-route-id": ROUTE_ID,
        "data-route-version": "2",
        "data-document-status": "live-provider",
        "data-controller": "native-session-cvars",
        "data-session-owner": "sgame",
    }
    for name, value in expected.items():
        if body.attrib.get(name) != value:
            errors.append(f"match_stats must declare {name}={value}")

    elements = _elements(root)
    screen = elements.get("match-stats-screen")
    expected_close = "popmenu; worr_matchstats_close"
    if screen is None or screen.attrib.get("data-close-command") != expected_close:
        errors.append("match_stats must preserve connected cleanup for all back paths")
    backplates = [
        button
        for button in root.iter("button")
        if button.attrib.get("data-command") == "ui.back"
    ]
    if len(backplates) != 1:
        errors.append("match_stats must expose exactly one standardized backplate")
    if any(
        button.attrib.get("data-command", "").startswith("popmenu")
        for button in root.iter("button")
    ):
        errors.append("match_stats must not restore a duplicate Back action")

    content = elements.get("match-stats-content")
    expected_content = {
        "data-model": "session.match_stats.snapshot",
        "data-controller": "native-session-cvars",
        "data-list-provider": "sgame-fixed-stats-snapshot",
        "data-owner-boundary": "sgame-published-cvars",
        "data-refresh-interval-ms": "1000",
    }
    if content is None:
        errors.append("match_stats is missing its live snapshot content")
    else:
        for name, value in expected_content.items():
            if content.attrib.get(name) != value:
                errors.append(f"match_stats content must declare {name}={value}")

    empty = elements.get("match-stats-empty")
    if empty is None or empty.attrib.get("data-show-if") != "!ui_matchstats_player":
        errors.append("match_stats must retain a truthful direct-route empty state")
    grid = elements.get("match-stats-grid")
    if grid is None or grid.attrib.get("data-visible-if") != "ui_matchstats_player":
        errors.append("match_stats live cards must hide without a player snapshot")

    for binding in STAT_BINDINGS:
        element_id = f"match-stats-{binding.replace('_', '-')}"
        element = elements.get(element_id)
        cvar = f"ui_matchstats_{binding}"
        if element is None or element.attrib.get("data-bind-cvar") != cvar:
            errors.append(f"match_stats binding is missing or incorrect: {cvar}")

    card_headings = {
        "match-stats-combat": "Combat",
        "match-stats-damage": "Damage",
        "match-stats-accuracy-card": "Accuracy",
    }
    for element_id, heading in card_headings.items():
        card = elements.get(element_id)
        card_heading = next(card.iter("h2"), None) if card is not None else None
        if card_heading is None or "".join(card_heading.itertext()).strip() != heading:
            errors.append(f"match_stats semantic card is missing: {heading}")

    document_source = _read(repo_root / DOCUMENT, errors)
    if "ui_matchstats_line_" in document_source:
        errors.append("match_stats must not regress to the raw compatibility-line report")

    theme = _read(repo_root / SESSION_THEME, errors)
    _require(
        theme,
        (
            ".session-match-stats-content",
            ".match-stats-player",
            ".match-stats-grid",
            ".match-stats-card",
            ".match-stats-row",
            "flex-wrap: wrap",
        ),
        "match-stats responsive card styles",
        errors,
    )
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    _require(
        accessibility,
        (".ui-a11y-large-text", ".ui-reduced-motion", ".ui-safe-label"),
        "shared accessibility theme",
        errors,
    )


def _validate_metadata(repo_root: Path, errors: list[str]) -> None:
    try:
        routes = json.loads(_read(repo_root / SESSION_ROUTES, errors))
        route = next(item for item in routes["routes"] if item["id"] == ROUTE_ID)
    except (json.JSONDecodeError, KeyError, StopIteration) as exc:
        errors.append(f"invalid match_stats session metadata: {exc}")
        return
    if route.get("status") != "live_provider":
        errors.append("session metadata must promote match_stats to live_provider")
    if route.get("migration_phase") != "parity_ready":
        errors.append("match_stats migration phase must remain parity_ready")
    if "session.match_stats.snapshot" not in route.get("data_models", []):
        errors.append("match_stats metadata is missing the semantic snapshot model")
    if "DV-07-T04" not in route.get("task_ids", []):
        errors.append("match_stats metadata is missing DV-07-T04")
    contracts = route.get("controller_contracts", [])
    if len(contracts) != 4 or any(
        contract.get("status") != "live_provider" for contract in contracts
    ):
        errors.append("all four match_stats controller contracts must be live_provider")

    try:
        manifest = json.loads(_read(repo_root / MANIFEST, errors))
        entry = next(item for item in manifest["routes"] if item["id"] == ROUTE_ID)
    except (json.JSONDecodeError, KeyError, StopIteration) as exc:
        errors.append(f"invalid match_stats central manifest entry: {exc}")
        return
    if entry.get("status") != "live_provider":
        errors.append("central manifest must promote match_stats to live_provider")
    if entry.get("migration_phase") != "parity_ready":
        errors.append("central match_stats phase must remain parity_ready")

    capture = _read(repo_root / CAPTURE_HARNESS, errors)
    if '"match_stats": Path("session/match_stats.rml")' not in capture:
        errors.append("capture harness is missing match_stats route coverage")


def collect_errors(repo_root: Path) -> list[str]:
    errors: list[str] = []
    _validate_native_sources(repo_root, errors)
    _validate_document(repo_root, errors)
    _validate_metadata(repo_root, errors)
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    errors = collect_errors(args.repo_root.resolve())
    if errors:
        print("RmlUi live match-stats provider check failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("RmlUi live match-stats provider check")
    print("Routes checked: 1")
    print("Semantic stat bindings checked: 10")
    print("Result: RmlUi live match-stats provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
