#!/usr/bin/env python3
"""Validate the live RmlUi MyMap main and flag providers."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
PUBLISHER = Path("src/game/sgame/menu/menu_page_mymap.cpp")
UI_LIST = Path("src/game/sgame/menu/menu_ui_list.cpp")
COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
SESSION_THEME = Path("assets/ui/rml/common/theme/session.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
SESSION_ROUTES = Path("assets/ui/rml/session/routes.json")
MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

DOCUMENTS = {
    "mymap_main": Path("assets/ui/rml/session/mymap_main.rml"),
    "mymap_flags": Path("assets/ui/rml/session/mymap_flags.rml"),
}

MAIN_CVARS = (
    "ui_mymap_status",
    "ui_mymap_flags_summary",
    "ui_mymap_can_select",
    "ui_mymap_can_flags",
    "ui_mymap_has_flags",
)
FLAG_CODES = ("pu", "pa", "ar", "am", "ht", "bfg", "pb", "fd", "sd", "ws")


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def _read(path: Path, errors: list[str]) -> str:
    if not path.is_file():
        errors.append(f"missing required file: {path.as_posix()}")
        return ""
    return path.read_text(encoding="utf-8")


def _parse(repo_root: Path, route_id: str, errors: list[str]) -> ElementTree.Element | None:
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


def _validate_sources(repo_root: Path, errors: list[str]) -> None:
    runtime = _read(repo_root / RUNTIME, errors)
    for token in (
        "UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)",
        "UI_Rml_ConditionExpressionMatches",
        'element->GetAttribute<Rml::String>("data-label-cvar", "")',
        'UI_Rml_ElementCondition(element, "data-enable-if", "data-enabled-if")',
    ):
        if token not in runtime:
            errors.append(f"native MyMap bridge is missing token: {token}")

    publisher = _read(repo_root / PUBLISHER, errors)
    for cvar_name in MAIN_CVARS:
        if f'AppendCvar("{cvar_name}"' not in publisher:
            errors.append(f"MyMap publisher is missing live cvar: {cvar_name}")
    for token in (
        'fmt::format("ui_mymap_flag_{}", flag.code)',
        'AppendCommand("pushmenu mymap_main")',
        'AppendCommand("pushmenu mymap_flags")',
        "UiList_Open(ent, UiListKind::MyMap)",
        "RefreshMyMapMenu(ent)",
    ):
        if token not in publisher:
            errors.append(f"MyMap publisher is missing token: {token}")

    ui_list = _read(repo_root / UI_LIST, errors)
    for token in (
        "case UiListKind::MyMap:",
        'extras[0].command = "worr_mymap_flags"',
        'extras[1].command = "worr_mymap_clear"',
        'fmt::format("worr_mymap_queue {}", entry.filename)',
    ):
        if token not in ui_list:
            errors.append(f"MyMap list provider is missing token: {token}")
    for code in FLAG_CODES:
        if f'"{code}"' not in ui_list:
            errors.append(f"MyMap flag source is missing code: {code}")

    commands = _read(repo_root / COMMANDS, errors)
    if "RefreshMyMapFlagsMenu(ent)" not in commands:
        errors.append("MyMap command flow must refresh live flag labels")
    for command in (
        "worr_mymap_menu",
        "worr_mymap_select",
        "worr_mymap_flags",
        "worr_mymap_flag",
        "worr_mymap_clear",
        "worr_mymap_queue",
    ):
        if f'RegisterCommand("{command}"' not in commands:
            errors.append(f"sgame command registration is missing: {command}")


def _validate_common(route_id: str, root: ElementTree.Element, errors: list[str]) -> None:
    body = next(root.iter("body"), None)
    if body is None:
        errors.append(f"{route_id} is missing its body")
        return
    expected = {
        "data-route-id": route_id,
        "data-route-version": "2",
        "data-document-status": "live-provider",
        "data-controller": "native-session-cvars",
    }
    for name, value in expected.items():
        if body.attrib.get(name) != value:
            errors.append(f"{route_id} must declare {name}={value}")
    back_controls = [
        element for element in root.iter() if element.attrib.get("data-command") == "ui.back"
    ]
    if len(back_controls) != 1:
        errors.append(f"{route_id} must expose exactly one standardized backplate")
    if any(
        element.attrib.get("data-command", "").startswith("popmenu")
        for element in root.iter()
    ):
        errors.append(f"{route_id} must not restore a duplicate footer Back action")


def _validate_documents(repo_root: Path, errors: list[str]) -> None:
    roots: dict[str, ElementTree.Element] = {}
    for route_id in DOCUMENTS:
        root = _parse(repo_root, route_id, errors)
        if root is None:
            continue
        roots[route_id] = root
        _validate_common(route_id, root, errors)
    if len(roots) != len(DOCUMENTS):
        return

    main = _elements(roots["mymap_main"])
    expected_bindings = {
        "mymap-status": "cvars.ui_mymap_status",
        "mymap-flags-summary": "cvars.ui_mymap_flags_summary",
    }
    for element_id, binding in expected_bindings.items():
        if main.get(element_id) is None or main[element_id].attrib.get("data-bind") != binding:
            errors.append(f"mymap_main is missing live binding: {binding}")
    expected_enable = {
        "mymap-select": "ui_mymap_can_select=1",
        "mymap-flags": "ui_mymap_can_flags=1",
        "mymap-clear": "ui_mymap_has_flags=1",
    }
    for element_id, condition in expected_enable.items():
        if main.get(element_id) is None or main[element_id].attrib.get("data-enable-if") != condition:
            errors.append(f"{element_id} must use live enabled state: {condition}")
    if "is-primary" not in main["mymap-select"].attrib.get("class", "").split():
        errors.append("Select Map must use the canonical primary treatment")

    flags_root = roots["mymap_flags"]
    nav = next(flags_root.iter("nav"), None)
    if nav is None or "session-vote-option-grid" not in nav.attrib.get("class", "").split():
        errors.append("mymap_flags must use the bounded two-column flag grid")
    labels = {
        element.attrib.get("data-label-cvar", "") for element in flags_root.iter("button")
    }
    commands = {
        element.attrib.get("data-command", "") for element in flags_root.iter("button")
    }
    for code in FLAG_CODES:
        if f"ui_mymap_flag_{code}" not in labels:
            errors.append(f"mymap_flags is missing live label for: {code}")
        if f"worr_mymap_flag {code}" not in commands:
            errors.append(f"mymap_flags is missing toggle command for: {code}")


def _validate_style_capture_metadata(repo_root: Path, errors: list[str]) -> None:
    theme = _read(repo_root / SESSION_THEME, errors)
    for token in (
        ".session-vote-option-grid",
        ".session-vote-option-grid button",
        "min-height: 36px",
    ):
        if token not in theme:
            errors.append(f"MyMap layout is missing token: {token}")
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    if ".ui-high-visibility .menu-list button" not in accessibility:
        errors.append("MyMap flag grid is missing high-visibility coverage")
    harness = _read(repo_root / CAPTURE_HARNESS, errors)
    for route_id, document in DOCUMENTS.items():
        relative = document.relative_to("assets/ui/rml")
        if f'"{route_id}": Path("{relative.as_posix()}")' not in harness:
            errors.append(f"capture harness is missing the {route_id} route")

    try:
        routes_data = json.loads(_read(repo_root / SESSION_ROUTES, errors))
        manifest_data = json.loads(_read(repo_root / MANIFEST, errors))
    except json.JSONDecodeError as exc:
        errors.append(f"invalid MyMap provider metadata JSON: {exc}")
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
    _validate_sources(repo_root, errors)
    _validate_documents(repo_root, errors)
    _validate_style_capture_metadata(repo_root, errors)
    print("RmlUi live MyMap provider check")
    print(f"Routes checked: {len(DOCUMENTS)}")
    print(f"Sgame-published cvars checked: {len(MAIN_CVARS) + len(FLAG_CODES)}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live MyMap provider check failed.")
        return 1
    print("Result: RmlUi live MyMap provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
