#!/usr/bin/env python3
"""Validate the live RmlUi session welcome, match-hub, and info providers."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
CLIENT_RML = Path("src/client/ui_rml/ui_rml.cpp")
SESSION_PUBLISHER = Path("src/game/sgame/menu/menu_page_welcome.cpp")
SESSION_COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
SESSION_THEME = Path("assets/ui/rml/common/theme/session.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
SESSION_ROUTES = Path("assets/ui/rml/session/routes.json")
MANIFEST = Path("tools/ui_smoke/rmlui_manifest.json")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")

DOCUMENTS = {
    "dm_welcome": Path("assets/ui/rml/session/dm_welcome.rml"),
    "dm_join": Path("assets/ui/rml/session/dm_join.rml"),
    "join": Path("assets/ui/rml/session/join.rml"),
    "dm_hostinfo": Path("assets/ui/rml/session/dm_hostinfo.rml"),
    "dm_matchinfo": Path("assets/ui/rml/session/dm_matchinfo.rml"),
}

EXPECTED_VERSIONS = {
    "dm_welcome": "2",
    "dm_join": "3",
    "join": "3",
    "dm_hostinfo": "2",
    "dm_matchinfo": "2",
}

PUBLISHED_CVARS = (
    "ui_dm_can_join",
    "ui_dm_context",
    "ui_dm_current_status",
    "ui_dm_gametype",
    "ui_dm_initial",
    "ui_dm_join_auto",
    "ui_dm_join_blue",
    "ui_dm_join_free",
    "ui_dm_join_notice",
    "ui_dm_join_red",
    "ui_dm_map",
    "ui_dm_mapname",
    "ui_dm_match_state",
    "ui_dm_menu_active",
    "ui_dm_motd",
    "ui_dm_player_count",
    "ui_dm_ready_command",
    "ui_dm_ready_label",
    "ui_dm_ruleset",
    "ui_dm_score_limit",
    "ui_dm_show_admin",
    "ui_dm_show_callvote",
    "ui_dm_show_forfeit",
    "ui_dm_show_join",
    "ui_dm_show_leave",
    "ui_dm_show_matchstats",
    "ui_dm_show_mymap",
    "ui_dm_show_ready",
    "ui_dm_show_resume",
    "ui_dm_show_spectate",
    "ui_dm_show_tourney_info",
    "ui_dm_show_tourney_maps",
    "ui_dm_spectate_command",
    "ui_dm_spectator_count",
    "ui_dm_subtitle",
    "ui_dm_teamplay",
    "ui_dm_time_limit",
    "ui_dm_title",
    "ui_hostinfo_host",
    "ui_hostinfo_motd",
    "ui_hostinfo_server",
    "ui_matchinfo_author1",
    "ui_matchinfo_author2",
    "ui_matchinfo_gametype",
    "ui_matchinfo_map",
    "ui_matchinfo_mapname",
    "ui_matchinfo_ruleset",
    "ui_matchinfo_scorelimit",
    "ui_matchinfo_timelimit",
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


def _elements(root: ElementTree.Element) -> dict[str, ElementTree.Element]:
    return {
        element.attrib["id"]: element
        for element in root.iter()
        if "id" in element.attrib
    }


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


def _validate_runtime(repo_root: Path, errors: list[str]) -> None:
    runtime = _read(repo_root / RUNTIME, errors)
    _require(
        runtime,
        (
            "static void UI_Rml_ApplyDocumentCvarBindings",
            "UI_Rml_RefreshCvarBindings(document, nullptr, true)",
            "UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)",
            "ui_rml_document->Show()",
            'element->GetAttribute<Rml::String>("data-label-cvar", "")',
            'element->GetAttribute<Rml::String>("data-command-cvar", "")',
            "UI_Rml_ConditionExpressionMatches",
            "UI_Rml_ApplyElementConditions",
            "UI_Rml_ActiveDocumentCloseCommand",
            "UI_Rml_CompiledRuntimeHandleBackKey",
            "UI_Rml_MatchHubCanCloseLocally",
            'command == "worr_dm_join_close"',
            '!Cvar_VariableInteger("ui_dm_initial")',
            'Cvar_VariableInteger("ui_dm_show_resume")',
            'UI_Rml_InsertCommandSequence("ui_rml_runtime_close",',
            "UI_Rml_RemoteSessionCommandWhenConnected",
            "cls.state >= ca_connected",
            '!strcmp(command, "worr_dm_join_close")',
            '!strcmp(command, "worr_welcome_continue")',
        ),
        "native cvar/condition/command session bridge",
        errors,
    )
    hydrate = runtime.find("UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)")
    show = runtime.find("ui_rml_document->Show()", hydrate)
    if hydrate < 0 or show < hydrate:
        errors.append("session cvar state must hydrate before the document is shown")

    client = _read(repo_root / CLIENT_RML, errors)
    _require(
        client,
        (
            'Cvar_VariableInteger("ui_dm_menu_active")',
            'Cbuf_AddText(&cmd_buffer, "worr_dm_join_close\\n")',
        ),
        "match-hub close ownership",
        errors,
    )


def _validate_publisher(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / SESSION_PUBLISHER, errors)
    for cvar_name in PUBLISHED_CVARS:
        token = f'AppendCvar("{cvar_name}"'
        if token not in source:
            errors.append(f"session publisher is missing live cvar: {cvar_name}")
    _require(
        source,
        (
            'OpenJoinMenuInternal(ent, DmJoinTitle(), DmJoinSubtitle(), "dm_join")',
            'OpenJoinMenuInternal(ent, title, "", "join")',
            'AppendCommand("pushmenu dm_hostinfo")',
            'AppendCommand("pushmenu dm_matchinfo")',
            "OpenDmJoinMenu(ent);",
        ),
        "session route publisher",
        errors,
    )

    commands = _read(repo_root / SESSION_COMMANDS, errors)
    _require(
        commands,
        (
            'RegisterCommand("worr_dm_join_close"',
            'RegisterCommand("worr_dm_hostinfo"',
            'RegisterCommand("worr_dm_matchinfo"',
            'RegisterCommand("worr_welcome_continue"',
        ),
        "session command registration",
        errors,
    )


def _validate_common_identity(
    route_id: str, root: ElementTree.Element, errors: list[str]
) -> None:
    body = next(root.iter("body"), None)
    if body is None:
        errors.append(f"{route_id} is missing its body")
        return
    if body.attrib.get("data-route-id") != route_id:
        errors.append(f"{route_id} has the wrong route identity")
    if body.attrib.get("data-route-version") != EXPECTED_VERSIONS[route_id]:
        errors.append(f"{route_id} has the wrong live route version")
    if body.attrib.get("data-document-status") != "live-provider":
        errors.append(f"{route_id} must declare data-document-status=live-provider")
    if body.attrib.get("data-controller") != "native-session-cvars":
        errors.append(f"{route_id} must declare native-session-cvars ownership")


def _validate_welcome(root: ElementTree.Element, errors: list[str]) -> None:
    elements = _elements(root)
    screen = elements.get("dm-welcome-screen")
    if screen is None or screen.attrib.get("data-close-command") != (
        "forcemenuoff; worr_welcome_continue"
    ):
        errors.append("dm_welcome must preserve acknowledgement on close/back")
    cont = elements.get("dm-welcome-continue")
    if cont is None or cont.attrib.get("data-command") != (
        "forcemenuoff; worr_welcome_continue"
    ):
        errors.append("dm_welcome Continue must acknowledge the welcome flow")
    if cont is not None and "is-primary" not in cont.attrib.get("class", "").split():
        errors.append("dm_welcome Continue must use the primary action treatment")


def _validate_hub(route_id: str, root: ElementTree.Element, errors: list[str]) -> None:
    elements = _elements(root)
    prefix = "dm-join" if route_id == "dm_join" else "join"
    screen = elements.get(f"{prefix}-screen")
    if screen is None or screen.attrib.get("data-close-command") != "worr_dm_join_close":
        errors.append(f"{route_id} must preserve the live match-hub close command")

    required_ids = (
        f"{prefix}-title",
        f"{prefix}-map",
        f"{prefix}-player-count",
        f"{prefix}-red",
        f"{prefix}-blue",
        f"{prefix}-auto",
        f"{prefix}-free",
        f"{prefix}-spectate",
        f"{prefix}-ready",
        f"{prefix}-close",
        f"{prefix}-leave",
    )
    for element_id in required_ids:
        if element_id not in elements:
            errors.append(f"{route_id} is missing required live control: {element_id}")

    red = elements.get(f"{prefix}-red")
    free = elements.get(f"{prefix}-free")
    if red is None or red.attrib.get("data-visible-if") != (
        "ui_dm_show_join!=0;ui_dm_teamplay!=0"
    ):
        errors.append(f"{route_id} must gate team join controls on live team state")
    if free is None or free.attrib.get("data-visible-if") != (
        "ui_dm_show_join!=0;ui_dm_teamplay=0"
    ):
        errors.append(f"{route_id} must gate free join on live non-team state")

    spectate = elements.get(f"{prefix}-spectate")
    ready = elements.get(f"{prefix}-ready")
    if spectate is None or spectate.attrib.get("data-command-cvar") != (
        "ui_dm_spectate_command"
    ):
        errors.append(f"{route_id} Spectate must resolve its live command cvar")
    if ready is None or ready.attrib.get("data-command-cvar") != "ui_dm_ready_command":
        errors.append(f"{route_id} Ready must resolve its live command cvar")

    leave = elements.get(f"{prefix}-leave")
    if leave is None or leave.attrib.get("data-route-target") != "leave_match_confirm":
        errors.append(f"{route_id} Leave must open the confirmation route")


def _validate_info_page(
    route_id: str,
    root: ElementTree.Element,
    expected_bindings: tuple[str, ...],
    errors: list[str],
) -> None:
    elements = _elements(root)
    prefix = "dm-hostinfo" if route_id == "dm_hostinfo" else "dm-matchinfo"
    if f"{prefix}-empty" not in elements:
        errors.append(f"{route_id} must retain an explicit empty state")
    if f"{prefix}-list" not in elements:
        errors.append(f"{route_id} must use the bounded information-list layout")

    bound = {
        element.attrib.get("data-bind", "").removeprefix("cvars.")
        for element in root.iter()
        if element.attrib.get("data-bind", "").startswith("cvars.")
    }
    for cvar_name in expected_bindings:
        if cvar_name not in bound:
            errors.append(f"{route_id} is missing live binding: {cvar_name}")

    back_controls = [
        element
        for element in root.iter()
        if element.attrib.get("data-command") == "ui.back"
    ]
    if len(back_controls) != 1:
        errors.append(f"{route_id} must expose exactly one standardized back plate")
    if any(
        element.attrib.get("data-command", "").startswith("popmenu")
        for element in root.iter()
    ):
        errors.append(f"{route_id} must not restore a duplicate footer Back action")


def _validate_documents(repo_root: Path, errors: list[str]) -> None:
    roots: dict[str, ElementTree.Element] = {}
    for route_id in DOCUMENTS:
        root = _parse_document(repo_root, route_id, errors)
        if root is None:
            continue
        roots[route_id] = root
        _validate_common_identity(route_id, root, errors)

    if "dm_welcome" in roots:
        _validate_welcome(roots["dm_welcome"], errors)
    for route_id in ("dm_join", "join"):
        if route_id in roots:
            _validate_hub(route_id, roots[route_id], errors)
    if "dm_hostinfo" in roots:
        _validate_info_page(
            "dm_hostinfo",
            roots["dm_hostinfo"],
            ("ui_hostinfo_server", "ui_hostinfo_host", "ui_hostinfo_motd"),
            errors,
        )
    if "dm_matchinfo" in roots:
        _validate_info_page(
            "dm_matchinfo",
            roots["dm_matchinfo"],
            (
                "ui_matchinfo_gametype",
                "ui_matchinfo_map",
                "ui_matchinfo_mapname",
                "ui_matchinfo_author1",
                "ui_matchinfo_author2",
                "ui_matchinfo_ruleset",
                "ui_matchinfo_scorelimit",
                "ui_matchinfo_timelimit",
            ),
            errors,
        )


def _validate_styles_and_capture(repo_root: Path, errors: list[str]) -> None:
    theme = _read(repo_root / SESSION_THEME, errors)
    _require(
        theme,
        (
            ".match-hub-shell",
            ".match-hub-stat-grid",
            ".match-hub-join-actions",
            ".session-info-list",
            ".session-info-row",
            ".session-info-line",
            ".session-info-line\n{\n\tdisplay: block",
            "min-height: 36px",
            "word-break: break-word",
        ),
        "live session layout",
        errors,
    )
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    _require(
        accessibility,
        (
            ".ui-high-visibility .match-hub-shell",
            ".ui-high-visibility .session-info-row",
            ".ui-high-visibility .session-info-line",
        ),
        "session accessibility contract",
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
        errors.append(f"invalid session provider metadata JSON: {exc}")
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
    _validate_publisher(repo_root, errors)
    _validate_documents(repo_root, errors)
    _validate_styles_and_capture(repo_root, errors)
    _validate_metadata(repo_root, errors)

    print("RmlUi live session-entry provider check")
    print(f"Routes checked: {len(DOCUMENTS)}")
    print(f"Sgame-published cvars checked: {len(PUBLISHED_CVARS)}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live session-entry provider check failed.")
        return 1
    print("Result: RmlUi live session-entry provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
