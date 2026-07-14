#!/usr/bin/env python3
"""Validate the live RmlUi admin menu and command-reference providers."""

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
ADMIN_PUBLISHER = Path("src/game/sgame/menu/menu_page_admin.cpp")
COMMAND_REFERENCE_PUBLISHER = Path(
    "src/game/sgame/menu/menu_page_admin_commands.cpp"
)
CLIENT_COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
ADMIN_COMMANDS = Path("src/game/sgame/commands/command_admin.cpp")
SESSION_THEME = Path("assets/ui/rml/common/theme/session.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
SESSION_ROUTES = Path("assets/ui/rml/session/routes.json")
MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

DOCUMENTS = {
    "admin_menu": Path("assets/ui/rml/session/admin_menu.rml"),
    "admin_commands": Path("assets/ui/rml/session/admin_commands.rml"),
}

ADMIN_COMMAND_NAMES = (
    "add_admin",
    "add_ban",
    "arena",
    "balance",
    "boot",
    "end_match",
    "force_vote",
    "gametype",
    "load_admins",
    "load_bans",
    "load_motd",
    "load_mappool",
    "load_mapcycle",
    "lock_team",
    "map_restart",
    "next_map",
    "ready_all",
    "remove_admin",
    "remove_ban",
    "reset_match",
    "replay",
    "ruleset",
    "set_map",
    "set_team",
    "shuffle",
    "start_match",
    "unlock_team",
    "unready_all",
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


def _validate_native_sources(repo_root: Path, errors: list[str]) -> None:
    runtime = _read(repo_root / RUNTIME, errors)
    _require(
        runtime,
        (
            "UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)",
            "UI_Rml_ConditionExpressionMatches",
            'element->GetAttribute<Rml::String>("data-route-target", "")',
            'element->GetAttribute<Rml::String>("data-command", "")',
        ),
        "native admin cvar/condition/route/command bridge",
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

    publisher = _read(repo_root / ADMIN_PUBLISHER, errors)
    _require(
        publisher,
        (
            'AppendCvar("ui_admin_show_replay"',
            "Tournament_IsActive()",
            'AppendCommand("pushmenu admin_menu")',
        ),
        "sgame admin menu publisher",
        errors,
    )
    reference_publisher = _read(repo_root / COMMAND_REFERENCE_PUBLISHER, errors)
    if 'AppendCommand("pushmenu admin_commands")' not in reference_publisher:
        errors.append("sgame admin command-reference publisher is missing its route")

    client_commands = _read(repo_root / CLIENT_COMMANDS, errors)
    for command in ("worr_admin_menu", "worr_admin_commands"):
        registration = re.search(
            rf'RegisterCommand\("{command}"[^\n]*AdminOnly', client_commands
        )
        if registration is None:
            errors.append(f"admin-only route command registration is missing: {command}")

    admin_commands = _read(repo_root / ADMIN_COMMANDS, errors)
    registered = set(
        re.findall(r'RegisterCommand\("([^"]+)"[^\n]*AdminOnly', admin_commands)
    )
    expected = set(ADMIN_COMMAND_NAMES)
    if registered != expected:
        missing = ", ".join(sorted(expected - registered)) or "-"
        extra = ", ".join(sorted(registered - expected)) or "-"
        errors.append(
            f"admin command registry drifted (missing: {missing}; extra: {extra})"
        )


def _validate_common(
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
    }
    for name, value in expected.items():
        if body.attrib.get(name) != value:
            errors.append(f"{route_id} must declare {name}={value}")
    back_controls = [
        element
        for element in root.iter("button")
        if element.attrib.get("data-command") == "ui.back"
    ]
    if len(back_controls) != 1:
        errors.append(f"{route_id} must expose exactly one standardized backplate")
    if any(
        element.attrib.get("data-command", "").startswith("popmenu")
        for element in root.iter("button")
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

    menu = _validate_common("admin_menu", roots["admin_menu"], errors)
    replay = menu.get("admin-menu-replay")
    if replay is None or replay.attrib.get("data-command") != (
        "worr_tourney_replay_menu"
    ):
        errors.append("admin Replay must dispatch the registered tournament action")
    elif replay.attrib.get("data-visible-if") != "ui_admin_show_replay!=0":
        errors.append("admin Replay must consume live tournament availability")
    commands_link = menu.get("admin-menu-commands")
    if commands_link is None or commands_link.attrib.get("data-route-target") != (
        "admin_commands"
    ):
        errors.append("admin command reference must use native route navigation")

    _validate_common("admin_commands", roots["admin_commands"], errors)
    rows = [
        element
        for element in roots["admin_commands"].iter("div")
        if "data-admin-command" in element.attrib
    ]
    authored = {row.attrib["data-admin-command"] for row in rows}
    expected = set(ADMIN_COMMAND_NAMES)
    if authored != expected or len(rows) != len(expected):
        missing = ", ".join(sorted(expected - authored)) or "-"
        extra = ", ".join(sorted(authored - expected)) or "-"
        errors.append(
            f"admin command reference drifted (missing: {missing}; extra: {extra})"
        )
    for row in rows:
        command = row.attrib["data-admin-command"]
        spans = list(row.findall("span"))
        if len(spans) != 3:
            errors.append(f"admin command row must have label, summary, and usage: {command}")
            continue
        label = "".join(spans[0].itertext()).strip()
        usage = "".join(spans[2].itertext()).strip()
        if label != command:
            errors.append(f"admin command label does not match registry: {command}")
        if not usage.startswith(f"usage: {command}"):
            errors.append(f"admin command usage does not match registry: {command}")


def _validate_style_capture_metadata(repo_root: Path, errors: list[str]) -> None:
    theme = _read(repo_root / SESSION_THEME, errors)
    _require(
        theme,
        (
            "#admin-menu-actions",
            "flex: 0 0 auto",
            "#admin-commands-list",
            "width: 632px",
            "#admin-commands-list .session-admin-row",
            "min-height: 44px",
            "font-family: \"WORR Mono\"",
            "overflow: auto",
            ".ui-high-visibility .session-admin-row",
        ),
        "admin layout",
        errors,
    )
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    _require(
        accessibility,
        (
            ".menu-list button:focus",
        ),
        "admin accessibility contract",
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
        errors.append(f"invalid admin provider metadata JSON: {exc}")
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
    _validate_native_sources(repo_root, errors)
    _validate_documents(repo_root, errors)
    _validate_style_capture_metadata(repo_root, errors)
    print("RmlUi live admin provider check")
    print(f"Routes checked: {len(DOCUMENTS)}")
    print(f"Admin commands checked: {len(ADMIN_COMMAND_NAMES)}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live admin provider check failed.")
        return 1
    print("Result: RmlUi live admin provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
