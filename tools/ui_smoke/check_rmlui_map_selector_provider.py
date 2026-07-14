#!/usr/bin/env python3
"""Validate the live RmlUi end-of-match map-selector provider."""

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
PUBLISHER = Path("src/game/sgame/menu/menu_page_mapselector.cpp")
CLIENT_COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
PLAYER_VIEW = Path("src/game/sgame/player/p_view.cpp")
MAP_MANAGER = Path("src/game/sgame/gameplay/g_map_manager.cpp")
G_LOCAL = Path("src/game/sgame/g_local.hpp")
DOCUMENT = Path("assets/ui/rml/session/map_selector.rml")
SESSION_THEME = Path("assets/ui/rml/common/theme/session.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
SESSION_ROUTES = Path("assets/ui/rml/session/routes.json")
MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

ROUTE_ID = "map_selector"
CANDIDATE_COUNT = 3


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
            "HandleBackKey",
            '!strcmp(command, "worr_mapselector_close")',
        ),
        "native map-selector cvar/condition/disconnected-close bridge",
        errors,
    )

    runtime_routes = _read(repo_root / RUNTIME_ROUTES, errors)
    if '{ "map_selector", "session/map_selector.rml" }' not in runtime_routes:
        errors.append("compiled route registry is missing map_selector")
    cgame_ui = _read(repo_root / CGAME_UI, errors)
    if '"map_selector",' not in cgame_ui:
        errors.append("cgame RmlUi route registry is missing map_selector")

    publisher = _read(repo_root / PUBLISHER, errors)
    _require(
        publisher,
        (
            "constexpr int kMapSelectorCandidates = 3",
            "constexpr int kMapSelectorBarSegments = 28",
            'AppendCvar("ui_mapselector_title"',
            'fmt::format("ui_mapselector_option_{}", i)',
            'fmt::format("ui_mapselector_option_show_{}", i)',
            'AppendCvar("ui_mapselector_ack_show"',
            'AppendCvar("ui_mapselector_ack_0"',
            'AppendCvar("ui_mapselector_ack_1"',
            "secondsRemaining",
            "std::ceil(secondsRemaining)",
            'AppendCvar("ui_mapselector_time_left"',
            'AppendCvar("ui_mapselector_bar"',
            'AppendCommand("pushmenu map_selector")',
            "mapSelectorDismissed = false",
        ),
        "sgame map-selector publisher",
        errors,
    )

    g_local = _read(repo_root / G_LOCAL, errors)
    if "bool mapSelectorDismissed = false;" not in g_local:
        errors.append("per-client map-selector dismissal state is missing")

    player_view = _read(repo_root / PLAYER_VIEW, errors)
    _require(
        player_view,
        (
            "const bool mapSelectorActive =",
            "cl->ui.mapSelectorDismissed = false;",
            "!cl->ui.mapSelectorActive && !cl->ui.mapSelectorDismissed",
            "cl->ui.mapSelectorActive &&",
            "RefreshMapSelectorMenu(ent)",
            "kMapSelectorUpdateInterval = 200_ms",
        ),
        "per-frame map-selector lifecycle",
        errors,
    )

    commands = _read(repo_root / CLIENT_COMMANDS, errors)
    _require(
        commands,
        (
            "MapSelector_CastVote(ent, *parsed)",
            "ent->client->ui.mapSelectorActive = false;",
            "ent->client->ui.mapSelectorDismissed = true;",
        ),
        "map-selector vote/close commands",
        errors,
    )
    for command in ("worr_mapselector_vote", "worr_mapselector_close"):
        match = re.search(rf'RegisterCommand\("{command}"([^\n]*)', commands)
        if match is None or "AllowIntermission" not in match.group(1):
            errors.append(f"intermission map-selector command is missing: {command}")

    manager = _read(repo_root / MAP_MANAGER, errors)
    _require(
        manager,
        (
            "void MapSelector_CastVote(gentity_t *ent, int voteIndex)",
            "voteIndex < 0 || voteIndex >= 3",
            "(ent->svFlags & SVF_BOT) || ent->client->sess.is_a_bot",
            "if (candidateId.empty())",
            "ms.votes[clientNum] = voteIndex",
            "MapSelector_SyncVotes(level)",
            "ms.voteCounts[i] > totalVoters / 2",
            "MapSelectorFinalize()",
            "for (auto ec : active_players())",
            "OpenMapSelectorMenu(ec)",
            "CloseActiveMenu(ec)",
            "ms.voteStartTime = 0_sec",
        ),
        "authoritative map-selector lifecycle",
        errors,
    )


def _validate_document(repo_root: Path, errors: list[str]) -> None:
    root = _parse_document(repo_root, errors)
    if root is None:
        return
    body = next(root.iter("body"), None)
    if body is None:
        errors.append("map_selector is missing its body")
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
            errors.append(f"map_selector must declare {name}={value}")

    elements = _elements(root)
    screen = elements.get("map-selector-screen")
    expected_close = "popmenu; worr_mapselector_close"
    if screen is None or screen.attrib.get("data-close-command") != expected_close:
        errors.append("map_selector must preserve close cleanup for all back paths")
    backplates = [
        button
        for button in root.iter("button")
        if button.attrib.get("data-command") == "ui.back"
    ]
    if len(backplates) != 1:
        errors.append("map_selector must expose exactly one standardized backplate")
    if any(
        button.attrib.get("data-command", "").startswith("popmenu")
        for button in root.iter("button")
    ):
        errors.append("map_selector must not restore a duplicate Close action")

    header = elements.get("map-selector-header")
    heading = next(header.iter("h1"), None) if header is not None else None
    if heading is None or "".join(heading.itertext()).strip() != "Next Map Vote":
        errors.append("map_selector must keep a stable Next Map Vote heading")
    elif any(name in heading.attrib for name in ("data-bind", "data-bind-cvar")):
        errors.append("map_selector heading must not be blanked by post-vote state")

    prompt = elements.get("map-selector-summary")
    if prompt is None or prompt.attrib.get("data-bind-cvar") != "ui_mapselector_title":
        errors.append("map_selector must consume the live pre-vote prompt")
    elif prompt.attrib.get("data-visible-if") != "ui_mapselector_ack_show=0":
        errors.append("map_selector pre-vote prompt must hide after acknowledgement")
    ack_summary = elements.get("map-selector-summary-ack")
    if ack_summary is None or ack_summary.attrib.get("data-visible-if") != (
        "ui_mapselector_ack_show=1"
    ):
        errors.append("map_selector must show truthful post-vote summary copy")

    options = elements.get("map-selector-options")
    if options is None or options.attrib.get("data-controller") != (
        "native-session-cvars"
    ):
        errors.append("map_selector options must use the native session provider")
    elif options.attrib.get("data-list-provider") != "sgame-fixed-cvar-options":
        errors.append("map_selector options must declare the fixed sgame provider")

    option_buttons = [
        element
        for element in root.iter("button")
        if element.attrib.get("id", "").startswith("map-selector-option-")
    ]
    if len(option_buttons) != CANDIDATE_COUNT:
        errors.append("map_selector must expose exactly three candidate controls")
    for index in range(CANDIDATE_COUNT):
        option = elements.get(f"map-selector-option-{index}")
        label_cvar = f"ui_mapselector_option_{index}"
        show_cvar = f"ui_mapselector_option_show_{index}!=0"
        command = f"worr_mapselector_vote {index}"
        if option is None:
            errors.append(f"map-selector candidate control is missing: {index}")
            continue
        expected_attributes = {
            "data-command": command,
            "data-label-cvar": label_cvar,
            "data-visible-if": show_cvar,
        }
        for name, value in expected_attributes.items():
            if option.attrib.get(name) != value:
                errors.append(f"map-selector option {index} must declare {name}={value}")

    empty = elements.get("map-selector-empty")
    if empty is None or empty.attrib.get("data-show-if") != (
        "!ui_mapselector_option_show_0;ui_mapselector_ack_show=0"
    ):
        errors.append("map-selector empty state must not reappear after voting")

    ack = elements.get("map-selector-ack")
    if ack is None or ack.attrib.get("data-visible-if") != "ui_mapselector_ack_show=1":
        errors.append("map-selector acknowledgement must consume live vote state")
    elif options is not None and ack not in list(options):
        errors.append("map-selector acknowledgement must replace candidates in main content")
    for index in range(2):
        element = elements.get(f"map-selector-ack-{index}")
        cvar = f"ui_mapselector_ack_{index}"
        if element is None or element.attrib.get("data-bind-cvar") != cvar:
            errors.append(f"map-selector acknowledgement must bind {cvar}")

    countdown = elements.get("map-selector-countdown")
    if countdown is None or countdown.attrib.get("data-visible-if") != (
        "ui_mapselector_time_left"
    ):
        errors.append("map-selector countdown must use numeric time availability")
    time_left = elements.get("map-selector-time-left")
    time_binding = next(time_left.iter("span"), None) if time_left is not None else None
    if time_binding is None or time_binding.attrib.get("data-bind-cvar") != (
        "ui_mapselector_time_left"
    ):
        errors.append("map-selector must expose an explicit seconds-remaining readout")
    bar = elements.get("map-selector-bar")
    if bar is None or bar.attrib.get("data-bind-cvar") != "ui_mapselector_bar":
        errors.append("map-selector must preserve the live countdown bar")


def _validate_style_capture_metadata(repo_root: Path, errors: list[str]) -> None:
    theme = _read(repo_root / SESSION_THEME, errors)
    _require(
        theme,
        (
            "#map-selector-options",
            "overflow: auto",
            "#map-selector-options button",
            "min-height: 54px",
            "#map-selector-status .status-panel",
            "#map-selector-bar",
        ),
        "map-selector bounded layout",
        errors,
    )
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    _require(
        accessibility,
        (
            ".ui-high-visibility #map-selector-options button",
            "#map-selector-options button:focus",
        ),
        "map-selector accessibility contract",
        errors,
    )

    harness = _read(repo_root / CAPTURE_HARNESS, errors)
    if '"map_selector": Path("session/map_selector.rml")' not in harness:
        errors.append("capture harness is missing the map_selector route")

    try:
        routes_data = json.loads(_read(repo_root / SESSION_ROUTES, errors))
        manifest_data = json.loads(_read(repo_root / MANIFEST, errors))
    except json.JSONDecodeError as exc:
        errors.append(f"invalid map-selector provider metadata JSON: {exc}")
        return
    routes = {route["id"]: route for route in routes_data.get("routes", [])}
    manifest = {route["id"]: route for route in manifest_data.get("routes", [])}
    route = routes.get(ROUTE_ID)
    if route is None or route.get("status") != "live_provider":
        errors.append("session metadata must promote map_selector to live_provider")
    elif any(
        contract.get("status") != "live_provider"
        for contract in route.get("controller_contracts", [])
    ):
        errors.append("all map_selector controller contracts must be live_provider")
    if manifest.get(ROUTE_ID, {}).get("status") != "live_provider":
        errors.append("central manifest must promote map_selector to live_provider")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []
    _validate_native_sources(repo_root, errors)
    _validate_document(repo_root, errors)
    _validate_style_capture_metadata(repo_root, errors)
    print("RmlUi live map-selector provider check")
    print("Routes checked: 1")
    print(f"Candidate controls checked: {CANDIDATE_COUNT}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live map-selector provider check failed.")
        return 1
    print("Result: RmlUi live map-selector provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
