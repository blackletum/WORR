#!/usr/bin/env python3
"""Validate the live RmlUi vote and callvote provider family."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
VOTE_PUBLISHER = Path("src/game/sgame/menu/menu_page_voting.cpp")
CALLVOTE_PUBLISHER = Path("src/game/sgame/menu/menu_page_callvote.cpp")
UI_LIST = Path("src/game/sgame/menu/menu_ui_list.cpp")
COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
SESSION_THEME = Path("assets/ui/rml/common/theme/session.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
SESSION_ROUTES = Path("assets/ui/rml/session/routes.json")
MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

DOCUMENTS = {
    "vote_menu": Path("assets/ui/rml/session/vote_menu.rml"),
    "callvote_main": Path("assets/ui/rml/session/callvote_main.rml"),
    "callvote_ruleset": Path("assets/ui/rml/session/callvote_ruleset.rml"),
    "callvote_timelimit": Path("assets/ui/rml/session/callvote_timelimit.rml"),
    "callvote_scorelimit": Path("assets/ui/rml/session/callvote_scorelimit.rml"),
    "callvote_unlagged": Path("assets/ui/rml/session/callvote_unlagged.rml"),
    "callvote_random": Path("assets/ui/rml/session/callvote_random.rml"),
    "callvote_map_flags": Path("assets/ui/rml/session/callvote_map_flags.rml"),
}

VOTE_CVARS = (
    "ui_vote_line_0",
    "ui_vote_line_1",
    "ui_vote_line_2",
    "ui_vote_can_vote",
    "ui_vote_ready_label",
    "ui_vote_ready_countdown",
    "ui_vote_time_left",
)

CALLVOTE_SHOW_CVARS = (
    "ui_callvote_show_map",
    "ui_callvote_show_nextmap",
    "ui_callvote_show_restart",
    "ui_callvote_show_gametype",
    "ui_callvote_show_ruleset",
    "ui_callvote_show_timelimit",
    "ui_callvote_show_scorelimit",
    "ui_callvote_show_shuffle",
    "ui_callvote_show_balance",
    "ui_callvote_show_unlagged",
    "ui_callvote_show_cointoss",
    "ui_callvote_show_random",
    "ui_callvote_show_arena",
)

SCORE_LABEL_CVARS = tuple(
    f"ui_callvote_scorelimit_set_{value}"
    for value in (5, 10, 15, 20, 25, 30, 50, 100)
)

FLAG_CODES = ("pu", "pa", "ar", "am", "ht", "bfg", "pb", "fd", "sd", "ws")

PUBLISHED_CVAR_COUNT = (
    len(VOTE_CVARS)
    + len(CALLVOTE_SHOW_CVARS)
    + 1
    + 1
    + len(SCORE_LABEL_CVARS)
    + 1
    + len(FLAG_CODES)
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
    text = _read(repo_root / path, errors)
    if not text:
        return None
    try:
        return ElementTree.fromstring(text)
    except ElementTree.ParseError as exc:
        errors.append(f"{path.as_posix()}: invalid XML: {exc}")
        return None


def _elements(root: ElementTree.Element) -> dict[str, ElementTree.Element]:
    return {
        element.attrib["id"]: element
        for element in root.iter()
        if "id" in element.attrib
    }


def _validate_runtime(repo_root: Path, errors: list[str]) -> None:
    runtime = _read(repo_root / RUNTIME, errors)
    _require(
        runtime,
        (
            "static void UI_Rml_ApplyDocumentCvarBindings",
            "UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)",
            "UI_Rml_ConditionExpressionMatches",
            "UI_Rml_ApplyElementConditions",
            'element->GetAttribute<Rml::String>("data-label-cvar", "")',
            "UI_Rml_ActiveDocumentCloseCommand",
            "UI_Rml_CompiledRuntimeHandleBackKey",
            "UI_Rml_RemoteSessionCommandWhenConnected",
            "cls.state <= ca_active",
            '!strcmp(command, "worr_vote_close")',
        ),
        "native vote cvar/condition/command bridge",
        errors,
    )
    hydrate = runtime.find("UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)")
    show = runtime.find("ui_rml_document->Show()", hydrate)
    if hydrate < 0 or show < hydrate:
        errors.append("vote and callvote cvar state must hydrate before show")
    if runtime.count('"ui_rml_runtime_back",\n') < 2 or runtime.count(
        "UI_Rml_RemoteSessionCommandWhenConnected(tail)"
    ) < 4:
        errors.append("click and key-driven popmenu tails must use the connection guard")


def _validate_publishers(repo_root: Path, errors: list[str]) -> None:
    vote_source = _read(repo_root / VOTE_PUBLISHER, errors)
    for cvar_name in VOTE_CVARS:
        if f'AppendCvar("{cvar_name}"' not in vote_source:
            errors.append(f"vote publisher is missing live cvar: {cvar_name}")
    _require(
        vote_source,
        ('AppendCommand("pushmenu vote_menu")', "Vote_Menu_Active(ent)"),
        "vote publisher lifecycle",
        errors,
    )

    callvote_source = _read(repo_root / CALLVOTE_PUBLISHER, errors)
    for cvar_name in CALLVOTE_SHOW_CVARS:
        if f'AppendCvar("{cvar_name}"' not in callvote_source:
            errors.append(f"callvote publisher is missing live cvar: {cvar_name}")
    for cvar_name in (
        "ui_callvote_timelimit_current",
        "ui_callvote_scorelimit_current",
        *SCORE_LABEL_CVARS,
        "ui_callvote_unlagged_current",
    ):
        if f'AppendCvar("{cvar_name}"' not in callvote_source:
            errors.append(f"callvote publisher is missing live cvar: {cvar_name}")
    _require(
        callvote_source,
        (
            'fmt::format("ui_callvote_flag_{}", flag.code)',
            'AppendCommand("pushmenu callvote_main")',
            'AppendCommand("pushmenu callvote_ruleset")',
            'AppendCommand("pushmenu callvote_timelimit")',
            'AppendCommand("pushmenu callvote_scorelimit")',
            'AppendCommand("pushmenu callvote_unlagged")',
            'AppendCommand("pushmenu callvote_random")',
            'AppendCommand("pushmenu callvote_map_flags")',
        ),
        "callvote route publisher",
        errors,
    )

    ui_list = _read(repo_root / UI_LIST, errors)
    for code in FLAG_CODES:
        if f'"{code}"' not in ui_list:
            errors.append(f"map-flag provider is missing tri-state code: {code}")

    commands = _read(repo_root / COMMANDS, errors)
    command_names = (
        "worr_vote_yes",
        "worr_vote_no",
        "worr_vote_close",
        "worr_callvote_menu",
        "worr_callvote_map",
        "worr_callvote_map_flags",
        "worr_callvote_map_flag",
        "worr_callvote_map_clear",
        "worr_callvote_gametype",
        "worr_callvote_ruleset",
        "worr_callvote_timelimit",
        "worr_callvote_scorelimit",
        "worr_callvote_unlagged",
        "worr_callvote_random",
        "worr_callvote_arena",
        "worr_callvote_nextmap",
        "worr_callvote_restart",
        "worr_callvote_shuffle",
        "worr_callvote_balance",
        "worr_callvote_cointoss",
    )
    for command in command_names:
        if f'RegisterCommand("{command}"' not in commands:
            errors.append(f"sgame command registration is missing: {command}")


def _validate_common_document(
    route_id: str, root: ElementTree.Element, errors: list[str]
) -> None:
    body = next(root.iter("body"), None)
    if body is None:
        errors.append(f"{route_id} is missing its body")
        return
    if body.attrib.get("data-route-id") != route_id:
        errors.append(f"{route_id} has the wrong route identity")
    if body.attrib.get("data-route-version") != "2":
        errors.append(f"{route_id} has the wrong live route version")
    if body.attrib.get("data-document-status") != "live-provider":
        errors.append(f"{route_id} must declare data-document-status=live-provider")
    if body.attrib.get("data-controller") != "native-session-cvars":
        errors.append(f"{route_id} must declare native-session-cvars ownership")

    back_controls = [
        element
        for element in root.iter()
        if element.attrib.get("data-command") == "ui.back"
    ]
    if len(back_controls) != 1:
        errors.append(f"{route_id} must expose exactly one standardized backplate")
    if any(
        element.attrib.get("data-command", "").startswith("popmenu")
        for element in root.iter()
    ):
        errors.append(f"{route_id} must not restore a duplicate footer Back/Close action")


def _validate_vote(root: ElementTree.Element, errors: list[str]) -> None:
    elements = _elements(root)
    screen = elements.get("vote-menu-screen")
    if screen is None or screen.attrib.get("data-close-command") != (
        "popmenu; worr_vote_close"
    ):
        errors.append("vote_menu must preserve authoritative close cleanup")
    for element_id in ("vote-menu-ready-label", "vote-menu-ready-countdown"):
        element = elements.get(element_id)
        if element is None or element.attrib.get("data-visible-if") != (
            "ui_vote_can_vote=0;ui_vote_line_0"
        ):
            errors.append(f"{element_id} must be hidden when no vote is active")
    for element_id in ("vote-menu-yes", "vote-menu-no"):
        element = elements.get(element_id)
        if element is None or element.attrib.get("data-visible-if") != (
            "ui_vote_can_vote!=0;ui_vote_line_0"
        ):
            errors.append(f"{element_id} must require an active, open vote")
    yes = elements.get("vote-menu-yes")
    if yes is None or "is-primary" not in yes.attrib.get("class", "").split():
        errors.append("vote Yes must use the canonical primary treatment")
    if "vote-menu-close" in elements:
        errors.append("vote_menu must use its backplate as the single close affordance")
    time_value = elements.get("vote-menu-time-left-value")
    if time_value is None or time_value.attrib.get("data-bind") != (
        "cvars.ui_vote_time_left"
    ):
        errors.append("vote_menu must bind the live time-left value")


def _validate_callvote_documents(
    roots: dict[str, ElementTree.Element], errors: list[str]
) -> None:
    for route_id, root in roots.items():
        if route_id == "vote_menu":
            continue
        nav = next(root.iter("nav"), None)
        if nav is None or "session-vote-option-grid" not in nav.attrib.get(
            "class", ""
        ).split():
            errors.append(f"{route_id} must use the bounded two-column vote grid")
        if nav is not None and nav.attrib.get("data-controller") != (
            "native-session-cvars"
        ):
            errors.append(f"{route_id} nav must declare live controller ownership")

    main = _elements(roots["callvote_main"])
    empty = main.get("callvote-main-empty")
    expected_empty = ";".join(f"!{cvar_name}" for cvar_name in CALLVOTE_SHOW_CVARS)
    if empty is None or empty.attrib.get("data-show-if") != expected_empty:
        errors.append("callvote_main empty state must require every option to be absent")
    for cvar_name in CALLVOTE_SHOW_CVARS:
        if not any(
            element.attrib.get("data-visible-if") == f"{cvar_name}!=0"
            for element in roots["callvote_main"].iter("button")
        ):
            errors.append(f"callvote_main is missing live option gate: {cvar_name}")

    timelimit = _elements(roots["callvote_timelimit"])
    if timelimit.get("callvote-timelimit-current") is None or timelimit[
        "callvote-timelimit-current"
    ].attrib.get("data-bind") != "cvars.ui_callvote_timelimit_current":
        errors.append("callvote_timelimit must bind the current live limit")

    scorelimit = roots["callvote_scorelimit"]
    label_cvars = {
        element.attrib.get("data-label-cvar", "")
        for element in scorelimit.iter("button")
    }
    for cvar_name in SCORE_LABEL_CVARS:
        if cvar_name not in label_cvars:
            errors.append(f"callvote_scorelimit is missing live option label: {cvar_name}")

    unlagged = _elements(roots["callvote_unlagged"])
    if unlagged.get("callvote-unlagged-current") is None or unlagged[
        "callvote-unlagged-current"
    ].attrib.get("data-bind") != "cvars.ui_callvote_unlagged_current":
        errors.append("callvote_unlagged must bind the current live state")

    flags = roots["callvote_map_flags"]
    label_cvars = {
        element.attrib.get("data-label-cvar", "") for element in flags.iter("button")
    }
    commands = {
        element.attrib.get("data-command", "") for element in flags.iter("button")
    }
    for code in FLAG_CODES:
        if f"ui_callvote_flag_{code}" not in label_cvars:
            errors.append(f"callvote_map_flags is missing live label for: {code}")
        if f"worr_callvote_map_flag {code}" not in commands:
            errors.append(f"callvote_map_flags is missing toggle command for: {code}")


def _validate_documents(repo_root: Path, errors: list[str]) -> None:
    roots: dict[str, ElementTree.Element] = {}
    for route_id in DOCUMENTS:
        root = _parse_document(repo_root, route_id, errors)
        if root is None:
            continue
        roots[route_id] = root
        _validate_common_document(route_id, root, errors)
    if "vote_menu" in roots:
        _validate_vote(roots["vote_menu"], errors)
    if all(route_id in roots for route_id in DOCUMENTS):
        _validate_callvote_documents(roots, errors)


def _validate_styles_and_capture(repo_root: Path, errors: list[str]) -> None:
    theme = _read(repo_root / SESSION_THEME, errors)
    _require(
        theme,
        (
            ".session-vote-option-grid",
            ".session-vote-option-grid button",
            "flex-direction: row",
            "flex-wrap: wrap",
            "width: 632px",
            "width: 296px",
            "min-height: 36px",
            "overflow: auto",
        ),
        "bounded vote/callvote layout",
        errors,
    )
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    _require(
        accessibility,
        (
            ".ui-high-visibility .menu-list button",
            ".ui-high-visibility .menu-list button:hover",
            ".ui-high-visibility .menu-list button:active",
            ".menu-list button:focus",
        ),
        "vote/callvote accessibility contract",
        errors,
    )
    harness = _read(repo_root / CAPTURE_HARNESS, errors)
    for route_id, document in DOCUMENTS.items():
        relative = document.relative_to("assets/ui/rml")
        token = f'"{route_id}": Path("{relative.as_posix()}")'
        if token not in harness:
            errors.append(f"capture harness is missing the {route_id} route")


def _validate_metadata(repo_root: Path, errors: list[str]) -> None:
    try:
        routes_data = json.loads(_read(repo_root / SESSION_ROUTES, errors))
        manifest_data = json.loads(_read(repo_root / MANIFEST, errors))
    except json.JSONDecodeError as exc:
        errors.append(f"invalid vote provider metadata JSON: {exc}")
        return
    routes = {route["id"]: route for route in routes_data.get("routes", [])}
    manifest = {route["id"]: route for route in manifest_data.get("routes", [])}
    for route_id in DOCUMENTS:
        route = routes.get(route_id)
        if route is None:
            errors.append(f"session metadata is missing {route_id}")
        else:
            if route.get("status") != "live_provider":
                errors.append(f"session metadata must promote {route_id} to live_provider")
            contracts = route.get("controller_contracts", [])
            if not contracts or any(
                contract.get("status") != "live_provider" for contract in contracts
            ):
                errors.append(f"all {route_id} controller contracts must be live_provider")
        if manifest.get(route_id, {}).get("status") != "live_provider":
            errors.append(f"central manifest must promote {route_id} to live_provider")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []

    _validate_runtime(repo_root, errors)
    _validate_publishers(repo_root, errors)
    _validate_documents(repo_root, errors)
    _validate_styles_and_capture(repo_root, errors)
    _validate_metadata(repo_root, errors)

    print("RmlUi live vote/callvote provider check")
    print(f"Routes checked: {len(DOCUMENTS)}")
    print(f"Sgame-published cvars checked: {PUBLISHED_CVAR_COUNT}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live vote/callvote provider check failed.")
        return 1
    print("Result: RmlUi live vote/callvote provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
