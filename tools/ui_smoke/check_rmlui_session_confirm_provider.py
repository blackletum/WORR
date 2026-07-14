#!/usr/bin/env python3
"""Validate the live RmlUi forfeit and leave-match confirmation providers."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
RUNTIME_ROUTES = Path("src/client/ui_rml/ui_rml.cpp")
CGAME_UI = Path("src/game/cgame/ui/ui_core.cpp")
FORFEIT_PUBLISHER = Path("src/game/sgame/menu/menu_page_forfeit.cpp")
COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
BASE_THEME = Path("assets/ui/rml/common/theme/base.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
SESSION_ROUTES = Path("assets/ui/rml/session/routes.json")
MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

DOCUMENTS = {
    "forfeit_confirm": Path("assets/ui/rml/session/forfeit_confirm.rml"),
    "leave_match_confirm": Path("assets/ui/rml/session/leave_match_confirm.rml"),
}


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


def _validate_native_paths(repo_root: Path, errors: list[str]) -> None:
    runtime = _read(repo_root / RUNTIME, errors)
    _require(
        runtime,
        (
            'element->GetAttribute<Rml::String>("data-command", "")',
            'UI_Rml_CommandStartsWithToken(command, "popmenu")',
            'UI_Rml_CommandStartsWithToken(command, "forcemenuoff")',
            'UI_Rml_CommandTailAfterToken(command, "forcemenuoff")',
            'UI_Rml_InsertCommandSequence(\n                "ui_rml_runtime_close",',
            "UI_Rml_RemoteSessionCommandWhenConnected(tail)",
            "cls.state <= ca_active",
        ),
        "native confirmation command bridge",
        errors,
    )

    runtime_routes = _read(repo_root / RUNTIME_ROUTES, errors)
    cgame_ui = _read(repo_root / CGAME_UI, errors)
    for route_id in DOCUMENTS:
        document = DOCUMENTS[route_id].relative_to("assets/ui/rml")
        if f'{{ "{route_id}", "{document.as_posix()}" }}' not in runtime_routes:
            errors.append(f"compiled route registry is missing {route_id}")
        if f'!strcmp(menu_name, "{route_id}")' not in cgame_ui:
            errors.append(f"cgame popup route registry is missing {route_id}")

    publisher = _read(repo_root / FORFEIT_PUBLISHER, errors)
    if 'AppendCommand("pushmenu forfeit_confirm")' not in publisher:
        errors.append("sgame forfeit publisher must open the confirmation route")

    commands = _read(repo_root / COMMANDS, errors)
    _require(
        commands,
        (
            "void WorrForfeitYes",
            'TryLaunchVoteWithFeedback(ent, "forfeit", "")',
            "CloseActiveMenu(ent)",
            'RegisterCommand("worr_forfeit_menu"',
            'RegisterCommand("worr_forfeit_yes"',
        ),
        "sgame forfeit command flow",
        errors,
    )


def _validate_common(
    route_id: str, root: ElementTree.Element, errors: list[str]
) -> dict[str, ElementTree.Element]:
    elements = _elements(root)
    body = next(root.iter("body"), None)
    if body is None:
        errors.append(f"{route_id} is missing its body")
        return elements
    expected = {
        "data-route-id": route_id,
        "data-route-version": "2",
        "data-document-status": "live-provider",
        "data-controller": "native-session-cvars",
        "data-menu-presentation": "popup",
    }
    for name, value in expected.items():
        if body.attrib.get(name) != value:
            errors.append(f"{route_id} must declare {name}={value}")

    screen = elements.get(f"{route_id.replace('_', '-')}-screen")
    if screen is None or screen.attrib.get("data-close-command") != "popmenu":
        errors.append(f"{route_id} must expose safe popup cancel/back behavior")

    dialog = next(
        (
            element
            for element in root.iter("main")
            if "popup-dialog" in element.attrib.get("class", "").split()
        ),
        None,
    )
    if dialog is None or dialog.attrib.get("data-confirm-kind") != "destructive":
        errors.append(f"{route_id} must use the destructive confirmation dialog")
    elif dialog.attrib.get("data-controller") != "native-session-cvars":
        errors.append(f"{route_id} dialog must declare native controller ownership")

    buttons = list(root.iter("button"))
    if len(buttons) != 2:
        errors.append(f"{route_id} must expose exactly two confirmation actions")
        return elements
    if buttons[0].attrib.get("data-command") != "popmenu":
        errors.append(f"{route_id} must focus the safe No action first")
    if "popup-secondary" not in buttons[0].attrib.get("class", "").split():
        errors.append(f"{route_id} No action must use the secondary treatment")
    if buttons[0].attrib.get("data-loc-key") != "m_no":
        errors.append(f"{route_id} No action must retain localization")
    if "popup-danger" not in buttons[1].attrib.get("class", "").split():
        errors.append(f"{route_id} Yes action must use the destructive treatment")
    if buttons[1].attrib.get("data-loc-key") != "m_yes":
        errors.append(f"{route_id} Yes action must retain localization")
    return elements


def _validate_documents(repo_root: Path, errors: list[str]) -> None:
    roots: dict[str, ElementTree.Element] = {}
    for route_id in DOCUMENTS:
        root = _parse_document(repo_root, route_id, errors)
        if root is not None:
            roots[route_id] = root
    if len(roots) != len(DOCUMENTS):
        return

    forfeit = _validate_common("forfeit_confirm", roots["forfeit_confirm"], errors)
    forfeit_yes = forfeit.get("forfeit-confirm-yes")
    if forfeit_yes is None or forfeit_yes.attrib.get("data-command") != "worr_forfeit_yes":
        errors.append("forfeit Yes must dispatch the registered sgame action")
    prompt = forfeit.get("forfeit-confirm-prompt")
    if prompt is None or "count as a loss" not in "".join(prompt.itertext()).lower():
        errors.append("forfeit prompt must explain the destructive match outcome")

    leave = _validate_common("leave_match_confirm", roots["leave_match_confirm"], errors)
    leave_yes = leave.get("leave-match-yes")
    if leave_yes is None or leave_yes.attrib.get("data-command") != (
        "forcemenuoff; disconnect"
    ):
        errors.append("leave Yes must close the menu before disconnecting")
    leave_prompt = leave.get("leave-match-prompt")
    if leave_prompt is None or leave_prompt.attrib.get("data-loc-key") != (
        "m_confirm_leave_match_prompt"
    ):
        errors.append("leave prompt must retain its localization hook")
    elif "return to the lobby" in "".join(leave_prompt.itertext()).lower():
        errors.append("leave prompt must not claim that disconnect returns to a lobby")


def _validate_style_capture_metadata(repo_root: Path, errors: list[str]) -> None:
    base_theme = _read(repo_root / BASE_THEME, errors)
    _require(
        base_theme,
        (
            ".popup-screen",
            ".popup-dialog",
            ".confirmation-dialog",
            ".popup-actions",
            ".popup-danger",
            ".popup-danger:focus",
            ".popup-secondary",
            ".popup-secondary:focus",
            '.popup-dialog[data-confirm-kind="destructive"]',
        ),
        "confirmation popup layout",
        errors,
    )
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    _require(
        accessibility,
        (
            ".ui-high-visibility .popup-dialog",
            ".ui-high-visibility button:focus",
        ),
        "confirmation popup accessibility",
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
        errors.append(f"invalid confirmation provider metadata JSON: {exc}")
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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []
    _validate_native_paths(repo_root, errors)
    _validate_documents(repo_root, errors)
    _validate_style_capture_metadata(repo_root, errors)
    print("RmlUi live session confirmation provider check")
    print(f"Routes checked: {len(DOCUMENTS)}")
    print("Destructive actions checked: 2")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live session confirmation provider check failed.")
        return 1
    print("Result: RmlUi live session confirmation provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
