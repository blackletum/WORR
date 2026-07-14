#!/usr/bin/env python3
"""Validate the live RmlUi tournament menu family and sgame contract."""

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
PUBLISHER = Path("src/game/sgame/menu/menu_page_tournament.cpp")
UI_LIST_PROVIDER = Path("src/game/sgame/menu/menu_ui_list.cpp")
CLIENT_COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
REPLAY_STATE = Path("src/game/sgame/match/match_state.cpp")
SESSION_THEME = Path("assets/ui/rml/common/theme/session.rcss")
BASE_THEME = Path("assets/ui/rml/common/theme/base.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
SESSION_ROUTES = Path("assets/ui/rml/session/routes.json")
MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

DOCUMENTS = {
    "tourney_info": Path("assets/ui/rml/session/tourney_info.rml"),
    "tourney_mapchoices": Path("assets/ui/rml/session/tourney_mapchoices.rml"),
    "tourney_veto": Path("assets/ui/rml/session/tourney_veto.rml"),
    "tourney_replay_confirm": Path(
        "assets/ui/rml/session/tourney_replay_confirm.rml"
    ),
}

VETO_BINDINGS = (
    "ui_tourney_veto_inactive",
    "ui_tourney_veto_turn",
    "ui_tourney_veto_wait_0",
    "ui_tourney_veto_wait_1",
    "ui_tourney_veto_picks_needed",
    "ui_tourney_veto_maps_remaining",
)

VETO_STATE_CVARS = (
    "ui_tourney_veto_show_inactive",
    *VETO_BINDINGS,
    "ui_tourney_veto_show_wait",
    "ui_tourney_veto_can_pick",
    "ui_tourney_veto_can_ban",
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


def _parse_document(
    repo_root: Path, route_id: str, errors: list[str]
) -> ElementTree.Element | None:
    path = DOCUMENTS[route_id]
    source = _read(repo_root / path, errors)
    if not source:
        return None
    try:
        return ElementTree.fromstring(source)
    except ElementTree.ParseError as exc:
        errors.append(f"{path.as_posix()}: invalid XML: {exc}")
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
            'GetAttribute<Rml::String>("data-command-cvar", "")',
            'GetAttribute<Rml::String>("data-command", "")',
        ),
        "native tournament cvar/condition/command bridge",
        errors,
    )

    runtime_routes = _read(repo_root / RUNTIME_ROUTES, errors)
    cgame_ui = _read(repo_root / CGAME_UI, errors)
    for route_id, document in DOCUMENTS.items():
        relative = document.relative_to("assets/ui/rml")
        if f'{{ "{route_id}", "{relative.as_posix()}" }}' not in runtime_routes:
            errors.append(f"compiled route registry is missing {route_id}")
        if f'"{route_id}",' not in cgame_ui:
            errors.append(f"cgame RmlUi route registry is missing {route_id}")

    publisher = _read(repo_root / PUBLISHER, errors)
    _require(
        publisher,
        (
            'AppendCommand("pushmenu tourney_info")',
            "std::array<std::string, 10> lines{}",
            'fmt::format("ui_tourney_mapchoice_line_{}", i)',
            'AppendCommand("pushmenu tourney_mapchoices")',
            "TournamentActorTurn(ent)",
            "TournamentBansAllowed()",
            'AppendCommand("pushmenu tourney_veto")',
            'AppendCvar("ui_tourney_replay_prompt"',
            "Results from game {} onward will",
            'fmt::format("worr_tourney_replay {}", gameNumber)',
            'AppendCommand("pushmenu tourney_replay_confirm")',
        ),
        "sgame tournament publisher",
        errors,
    )
    for cvar in VETO_STATE_CVARS:
        if f'"{cvar}"' not in publisher:
            errors.append(f"sgame tournament publisher is missing {cvar}")

    commands = _read(repo_root / CLIENT_COMMANDS, errors)
    for command in (
        "worr_tourney_info",
        "worr_tourney_maps",
        "worr_tourney_veto",
        "worr_tourney_pick",
        "worr_tourney_ban",
    ):
        match = re.search(rf'RegisterCommand\("{command}"([^\n]*)', commands)
        if match is None:
            errors.append(f"registered tournament command is missing: {command}")
        elif "AdminOnly" in match.group(1):
            errors.append(f"participant tournament command became admin-only: {command}")
    for command in (
        "worr_tourney_replay_menu",
        "worr_tourney_replay_confirm",
        "worr_tourney_replay",
    ):
        match = re.search(rf'RegisterCommand\("{command}"([^\n]*)', commands)
        if match is None or "AdminOnly" not in match.group(1):
            errors.append(f"admin-only replay command registration is missing: {command}")
    for command in ("tourney_status", "tourney_pick", "tourney_ban"):
        if f'RegisterCommand("{command}"' not in commands:
            errors.append(f"legacy-compatible tournament command is missing: {command}")

    ui_list = _read(repo_root / UI_LIST_PROVIDER, errors)
    _require(
        ui_list,
        (
            "case UiListKind::TournamentPick:",
            "case UiListKind::TournamentBan:",
            "case UiListKind::TournamentReplay:",
            '"worr_tourney_pick"',
            '"worr_tourney_ban"',
            'fmt::format("worr_tourney_replay_confirm {}", gameNum)',
            "game.tournament.mapPicks",
            "game.tournament.mapBans",
        ),
        "shared tournament ui_list provider",
        errors,
    )

    replay = _read(repo_root / REPLAY_STATE, errors)
    _require(
        replay,
        (
            "bool Tournament_ReplayGame(int gameNumber, std::string &message)",
            "game.tournament.teamWins.fill(0)",
            "game.tournament.playerWins.fill(0)",
            "game.tournament.matchWinners.resize(targetIndex)",
            "game.tournament.matchIds.resize(targetIndex)",
            "game.tournament.matchMaps.resize(targetIndex)",
            'G_Fmt("gamemap {}\\n", mapName)',
        ),
        "tournament replay state reset",
        errors,
    )


def _validate_common_page(
    route_id: str, root: ElementTree.Element, errors: list[str]
) -> dict[str, ElementTree.Element]:
    body = next(root.iter("body"), None)
    if body is None:
        errors.append(f"{route_id} is missing its body")
        return _elements(root)
    expected = {
        "data-route-id": route_id,
        "data-route-version": "2",
        "data-document-status": "live-provider",
        "data-controller": "native-session-cvars",
        "data-session-owner": "sgame",
    }
    for name, value in expected.items():
        if body.attrib.get(name) != value:
            errors.append(f"{route_id} must declare {name}={value}")

    backplates = [
        button
        for button in root.iter("button")
        if button.attrib.get("data-command") == "ui.back"
    ]
    if len(backplates) != 1:
        errors.append(f"{route_id} must expose exactly one standardized backplate")
    if any(
        button.attrib.get("data-command", "").startswith("popmenu")
        for button in root.iter("button")
    ):
        errors.append(f"{route_id} must not restore a duplicate footer Back action")
    return _elements(root)


def _validate_documents(repo_root: Path, errors: list[str]) -> None:
    roots: dict[str, ElementTree.Element] = {}
    for route_id in DOCUMENTS:
        root = _parse_document(repo_root, route_id, errors)
        if root is not None:
            roots[route_id] = root
    if len(roots) != len(DOCUMENTS):
        return

    info = _validate_common_page("tourney_info", roots["tourney_info"], errors)
    status_ref = info.get("tourney-info-status-command")
    if status_ref is None or status_ref.attrib.get("data-command-ref") != "tourney_status":
        errors.append("tournament info must preserve the registered status command reference")
    elif "data-command" in status_ref.attrib:
        errors.append("tournament status guidance must not masquerade as a button")

    mapchoices = _validate_common_page(
        "tourney_mapchoices", roots["tourney_mapchoices"], errors
    )
    rows = [
        element
        for element in roots["tourney_mapchoices"].iter("p")
        if element.attrib.get("id", "").startswith("tourney-mapchoice-line-")
    ]
    if len(rows) != 10:
        errors.append("tournament map choices must expose exactly ten published rows")
    for index in range(10):
        cvar = f"ui_tourney_mapchoice_line_{index}"
        row = mapchoices.get(f"tourney-mapchoice-line-{index}")
        if row is None:
            errors.append(f"tournament map-choice row is missing: {index}")
            continue
        for attribute in ("data-bind-cvar", "data-visible-if"):
            if row.attrib.get(attribute) != cvar:
                errors.append(f"map-choice row {index} must declare {attribute}={cvar}")
    fallback = mapchoices.get("tourney-mapchoices-fallback")
    if fallback is None or fallback.attrib.get("data-show-if") != (
        "!ui_tourney_mapchoice_line_0"
    ):
        errors.append("tournament map choices must keep its direct-route empty state")

    veto = _validate_common_page("tourney_veto", roots["tourney_veto"], errors)
    veto_content = veto.get("tourney-veto-content")
    if veto_content is None:
        errors.append("tournament veto content is missing")
    elif "data-list-provider" in veto_content.attrib:
        errors.append("tournament veto must not claim an embedded candidate-list provider")
    for cvar in VETO_BINDINGS:
        matches = [
            element
            for element in roots["tourney_veto"].iter()
            if element.attrib.get("data-bind-cvar") == cvar
        ]
        if len(matches) != 1:
            errors.append(f"tournament veto must bind {cvar} exactly once")
    expected_conditions = {
        "tourney-veto-inactive-panel": "ui_tourney_veto_show_inactive!=0",
        "tourney-veto-turn-panel": "ui_tourney_veto_turn",
        "tourney-veto-wait-panel": "ui_tourney_veto_show_wait=1",
        "tourney-veto-action-panel": "ui_tourney_veto_can_pick=1",
        "tourney-veto-pick": "ui_tourney_veto_can_pick=1",
        "tourney-veto-ban": "ui_tourney_veto_can_ban=1",
        "tourney-veto-ban-locked": (
            "ui_tourney_veto_can_pick=1;ui_tourney_veto_can_ban=0"
        ),
    }
    for element_id, condition in expected_conditions.items():
        element = veto.get(element_id)
        if element is None or element.attrib.get("data-visible-if") != condition:
            errors.append(f"{element_id} must consume live condition {condition}")
    for element_id, command in (
        ("tourney-veto-pick", "worr_tourney_pick"),
        ("tourney-veto-ban", "worr_tourney_ban"),
    ):
        element = veto.get(element_id)
        if element is None or element.attrib.get("data-command") != command:
            errors.append(f"{element_id} must dispatch {command}")
    ban_locked = veto.get("tourney-veto-ban-locked")
    if ban_locked is None or ban_locked.tag != "button" or (
        ban_locked.attrib.get("disabled") != "disabled"
    ):
        errors.append("the Ban-locked state must be a disabled semantic control")
    elif "data-command" in ban_locked.attrib:
        errors.append("the Ban-locked control must not dispatch a veto command")

    replay_root = roots["tourney_replay_confirm"]
    replay_body = next(replay_root.iter("body"), None)
    if replay_body is None:
        errors.append("tournament replay confirmation is missing its body")
        return
    replay_expected = {
        "data-route-id": "tourney_replay_confirm",
        "data-route-version": "2",
        "data-document-status": "live-provider",
        "data-controller": "native-session-cvars",
        "data-menu-presentation": "popup",
    }
    for name, value in replay_expected.items():
        if replay_body.attrib.get(name) != value:
            errors.append(f"tourney_replay_confirm must declare {name}={value}")
    replay = _elements(replay_root)
    dialog = replay.get("tourney-replay-confirm-dialog")
    if dialog is None or dialog.attrib.get("data-confirm-kind") != "destructive":
        errors.append("tournament replay must declare destructive confirmation intent")
    prompt = replay.get("tourney-replay-prompt")
    if prompt is None or prompt.attrib.get("data-bind-cvar") != "ui_tourney_replay_prompt":
        errors.append("tournament replay prompt must consume the sgame cvar")
    elif "discarded" not in "".join(prompt.itertext()).lower():
        errors.append("tournament replay fallback must explain result truncation")
    actions = replay.get("tourney-replay-confirm-actions")
    action_buttons = list(actions.findall("button")) if actions is not None else []
    if [button.attrib.get("id") for button in action_buttons] != [
        "tourney-replay-no",
        "tourney-replay-yes",
    ]:
        errors.append("tournament replay must put safe No before destructive Yes")
    else:
        if action_buttons[0].attrib.get("data-command") != "popmenu":
            errors.append("tournament replay No must close without changing state")
        if action_buttons[1].attrib.get("data-command-cvar") != (
            "ui_tourney_replay_yes_cmd"
        ):
            errors.append("tournament replay Yes must dispatch the sgame command cvar")
        if "popup-danger" not in action_buttons[1].attrib.get("class", "").split():
            errors.append("tournament replay Yes must use destructive action styling")


def _validate_style_capture_metadata(repo_root: Path, errors: list[str]) -> None:
    session_theme = _read(repo_root / SESSION_THEME, errors)
    _require(
        session_theme,
        (
            "#tourney-veto-content",
            "width: 620px",
            "#tourney-mapchoices-list",
            "overflow: auto",
            ".session-flow-step.is-danger",
            ".ui-high-visibility .session-panel",
        ),
        "tournament layout and visibility styles",
        errors,
    )
    base_theme = _read(repo_root / BASE_THEME, errors)
    _require(
        base_theme,
        (".popup-danger", ".popup-danger:focus", "button.worr-backplate:focus"),
        "tournament destructive/focus styles",
        errors,
    )
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    _require(
        accessibility,
        (".ui-reduced-motion button:focus", ".ui-high-visibility button:focus"),
        "tournament accessibility styles",
        errors,
    )

    harness = _read(repo_root / CAPTURE_HARNESS, errors)
    for route_id, document in DOCUMENTS.items():
        relative = document.relative_to("assets/ui/rml")
        if f'"{route_id}": Path("{relative.as_posix()}")' not in harness:
            errors.append(f"capture harness is missing the {route_id} route")

    try:
        routes_data = json.loads(_read(repo_root / SESSION_ROUTES, errors))
        manifest_data = json.loads(_read(repo_root / MANIFEST, errors))
    except json.JSONDecodeError as exc:
        errors.append(f"invalid tournament provider metadata JSON: {exc}")
        return
    routes = {route["id"]: route for route in routes_data.get("routes", [])}
    manifest = {route["id"]: route for route in manifest_data.get("routes", [])}
    for route_id in DOCUMENTS:
        route = routes.get(route_id)
        if route is None or route.get("status") != "live_provider":
            errors.append(f"session metadata must promote {route_id} to live_provider")
        elif any(
            contract.get("status") != "live_provider"
            for contract in route.get("controller_contracts", [])
        ):
            errors.append(f"all {route_id} controller contracts must be live_provider")
        if manifest.get(route_id, {}).get("status") != "live_provider":
            errors.append(f"central manifest must promote {route_id} to live_provider")
    veto = routes.get("tourney_veto", {})
    if any(
        contract.get("category") == "list_provider"
        for contract in veto.get("controller_contracts", [])
    ):
        errors.append("tournament veto metadata must use the shared ui_list boundary")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []
    _validate_native_sources(repo_root, errors)
    _validate_documents(repo_root, errors)
    _validate_style_capture_metadata(repo_root, errors)
    print("RmlUi live tournament provider check")
    print(f"Routes checked: {len(DOCUMENTS)}")
    print("Map-choice rows checked: 10")
    print(f"Veto bindings checked: {len(VETO_BINDINGS)}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live tournament provider check failed.")
        return 1
    print("Result: RmlUi live tournament provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
