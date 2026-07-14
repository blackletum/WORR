#!/usr/bin/env python3
"""Validate the live RmlUi server-browser provider and authored table contract."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
RUNTIME_BRIDGE = Path("src/client/ui_rml/ui_rml.cpp")
UI_BRIDGE = Path("src/client/ui_bridge.cpp")
CGAME_UI = Path("src/game/cgame/ui/ui_core.cpp")
DOCUMENT = Path("assets/ui/rml/utility/servers.rml")
UTILITY_THEME = Path("assets/ui/rml/common/theme/utility.rcss")


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


def _validate_runtime(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / RUNTIME, errors)
    _require(
        source,
        (
            "UI_RML_SERVER_MAX_ENTRIES = 1024",
            "UI_RML_SERVER_PAGE_SIZE = 8",
            "UI_RML_SERVER_PING_STAGES = 3",
            "UI_Rml_ParseServerSources",
            '"favorites://"',
            '"broadcast://"',
            '"quake2://"',
            "HTTP_FetchFile(source, &data)",
            "FS_LoadFile(source + 7, &data)",
            "CL_SendStatusRequest(&entry.address)",
            "UI_Rml_ServerStatusEvent",
            "UI_Rml_ServerErrorEvent",
            "UI_Rml_SortServerEntries",
            'Cvar_Get("ui_sortservers", "1", 0)',
            'Cvar_Get("ui_pingrate", "0", 0)',
            'Cvar_Get("ui_server_source", "local", 0)',
            'action == "toggle-source"',
            'action == "connect"',
            'action == "previous-page"',
            'action == "next-page"',
            'action == "sort"',
            'action == "select"',
            'data-server-action=\\"select\\"',
            'QuerySelector(".server-row.is-selected")',
            'UI_Rml_QueueCommand(va("connect \\"%s\\""',
            'address.find_first_of("\\n\\r\\\";")',
            "UI_Rml_InitializeServerBrowser(ui_rml_document)",
        ),
        "runtime server provider",
        errors,
    )

    hydrate = source.find("UI_Rml_InitializeServerBrowser(ui_rml_document)")
    show = source.find("ui_rml_document->Show()", hydrate)
    if hydrate < 0 or show < 0 or hydrate > show:
        errors.append("server rows must be initialized before the document is shown")

    runtime_bridge = _read(repo_root / RUNTIME_BRIDGE, errors)
    _require(
        runtime_bridge,
        (
            "UI_Rml_OpenRouteWithArguments",
            "UI_Rml_RouteArguments",
            "ui_rml_runtime.StatusEvent(status)",
            "ui_rml_runtime.ErrorEvent(from)",
        ),
        "RmlUi runtime bridge",
        errors,
    )
    ui_bridge = _read(repo_root / UI_BRIDGE, errors)
    _require(
        ui_bridge,
        ("if (UI_Rml_StatusEvent(status))", "if (UI_Rml_ErrorEvent(from))"),
        "client UI event bridge",
        errors,
    )
    cgame_ui = _read(repo_root / CGAME_UI, errors)
    if "Cmd_RawArgsFrom(2)" not in cgame_ui or "arguments && arguments[0]" not in cgame_ui:
        errors.append("cgame pushmenu bridge must preserve server source arguments")


def _validate_document(repo_root: Path, errors: list[str]) -> None:
    text = _read(repo_root / DOCUMENT, errors)
    if not text:
        return
    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError as exc:
        errors.append(f"{DOCUMENT.as_posix()}: invalid XML: {exc}")
        return

    body = next(root.iter("body"), None)
    if body is None or body.attrib.get("data-document-status") != "live-provider":
        errors.append("server document must declare data-document-status=live-provider")

    buttons = {element.attrib.get("id"): element for element in root.iter("button")}
    expected = {
        "servers-refresh": "refresh",
        "servers-source": "toggle-source",
        "servers-connect": "connect",
        "servers-previous": "previous-page",
        "servers-next": "next-page",
    }
    for element_id, action in expected.items():
        element = buttons.get(element_id)
        if element is None:
            errors.append(f"server document is missing toolbar control {element_id}")
        elif element.attrib.get("data-server-action") != action:
            errors.append(f"{element_id} must dispatch server action {action}")
        elif element_id in {"servers-refresh", "servers-source"} and "disabled" in element.attrib:
            errors.append(f"{element_id} must be available in the authored document")

    table = next((e for e in root.iter("table") if e.attrib.get("id") == "servers-table"), None)
    if table is None:
        errors.append("server document is missing servers-table")
        return
    headers = [header.attrib.get("data-sort-key") for header in table.iter("th")]
    if headers != ["hostname", "mod", "map", "players", "ping"]:
        errors.append(f"server columns must be hostname/mod/map/players/ping, got {headers}")
    tbody = next((e for e in table.iter("tbody") if e.attrib.get("id") == "servers-table-body"), None)
    if tbody is None:
        errors.append("server table requires the servers-table-body provider target")
    sorts = [e.attrib.get("data-server-sort") for e in table.iter("button")
             if e.attrib.get("data-server-action") == "sort"]
    if sorts != ["0", "1", "2", "3", "4"]:
        errors.append("server sort controls must cover all five ordered columns")
    if "upcoming update" in text.lower():
        errors.append("server document still contains placeholder status copy")


def _validate_styles(repo_root: Path, errors: list[str]) -> None:
    styles = _read(repo_root / UTILITY_THEME, errors)
    _require(
        styles,
        (
            "#servers-source",
            "#servers-connect",
            "white-space: nowrap",
            "#servers-table th:nth-child(5)",
            "#servers-table tr.is-pending td",
            "#servers-table tr.is-error td",
            ".data-table .table-sort-button",
            ".data-table .data-table-row-action",
            "border-left-color: #83d18f",
        ),
        "utility server-table theme",
        errors,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    errors: list[str] = []
    repo_root = args.repo_root.resolve()
    _validate_runtime(repo_root, errors)
    _validate_document(repo_root, errors)
    _validate_styles(repo_root, errors)

    print("RmlUi server-browser live-provider check")
    print("Columns checked: 5")
    print("Toolbar actions checked: 5")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi server-browser live-provider check failed.")
        return 1
    print("Result: RmlUi server-browser live-provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
