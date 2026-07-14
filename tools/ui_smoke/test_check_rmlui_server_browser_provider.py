from __future__ import annotations

import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_server_browser_provider as server_provider  # noqa: E402


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _valid_document() -> str:
    headers = []
    for index, name in enumerate(("hostname", "mod", "map", "players", "ping")):
        headers.append(
            f'<th data-sort-key="{name}"><button data-server-action="sort" '
            f'data-server-sort="{index}">{name}</button></th>'
        )
    toolbar = "".join(
        f'<button id="{element_id}" data-server-action="{action}">{action}</button>'
        for element_id, action in (
            ("servers-refresh", "refresh"),
            ("servers-source", "toggle-source"),
            ("servers-connect", "connect"),
            ("servers-previous", "previous-page"),
            ("servers-next", "next-page"),
        )
    )
    return (
        '<rml><body data-document-status="live-provider">'
        + toolbar
        + '<table id="servers-table"><thead><tr>'
        + "".join(headers)
        + '</tr></thead><tbody id="servers-table-body"></tbody></table>'
        + "</body></rml>"
    )


def _write_valid_repo(repo_root: Path) -> None:
    _write(
        repo_root / server_provider.RUNTIME,
        r'''
static constexpr size_t UI_RML_SERVER_MAX_ENTRIES = 1024;
static constexpr size_t UI_RML_SERVER_PAGE_SIZE = 8;
static constexpr int UI_RML_SERVER_PING_STAGES = 3;
void provider() {
  UI_Rml_ParseServerSources();
  "favorites://"; "broadcast://"; "quake2://";
  HTTP_FetchFile(source, &data); FS_LoadFile(source + 7, &data);
  CL_SendStatusRequest(&entry.address);
  UI_Rml_ServerStatusEvent(); UI_Rml_ServerErrorEvent();
  UI_Rml_SortServerEntries();
  Cvar_Get("ui_sortservers", "1", 0);
  Cvar_Get("ui_pingrate", "0", 0);
  Cvar_Get("ui_server_source", "local", 0);
  if (action == "toggle-source") {} if (action == "connect") {}
  if (action == "previous-page") {} if (action == "next-page") {}
  if (action == "sort") {} if (action == "select") {}
  "data-server-action=\"select\"";
  QuerySelector(".server-row.is-selected");
  UI_Rml_QueueCommand(va("connect \"%s\"", address.c_str()));
  address.find_first_of("\n\r\";");
}
UI_Rml_InitializeServerBrowser(ui_rml_document);
ui_rml_document->Show();
''',
    )
    _write(
        repo_root / server_provider.RUNTIME_BRIDGE,
        """
UI_Rml_OpenRouteWithArguments(); UI_Rml_RouteArguments();
ui_rml_runtime.StatusEvent(status); ui_rml_runtime.ErrorEvent(from);
""",
    )
    _write(
        repo_root / server_provider.UI_BRIDGE,
        "if (UI_Rml_StatusEvent(status)) return;\nif (UI_Rml_ErrorEvent(from)) return;\n",
    )
    _write(
        repo_root / server_provider.CGAME_UI,
        "const char *arguments = Cmd_RawArgsFrom(2); if (arguments && arguments[0]) {}\n",
    )
    _write(repo_root / server_provider.DOCUMENT, _valid_document())
    _write(
        repo_root / server_provider.UTILITY_THEME,
        """
#servers-source {}
#servers-connect { white-space: nowrap; }
#servers-table th:nth-child(5) {}
#servers-table tr.is-pending td {}
#servers-table tr.is-error td {}
.data-table .table-sort-button {}
.data-table .data-table-row-action {}
.data-table tr.is-selected td { border-left-color: #83d18f; }
""",
    )


def test_valid_server_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert server_provider.main(["--repo-root", str(repo_root)]) == 0
    captured = capsys.readouterr()
    assert "Columns checked: 5" in captured.out
    assert "Result: RmlUi server-browser live-provider check passed." in captured.out


def test_placeholder_toolbar_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / server_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'id="servers-refresh" data-server-action="refresh"',
            'id="servers-refresh" disabled="disabled"',
        ),
        encoding="utf-8",
    )
    assert server_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "servers-refresh must dispatch server action refresh" in capsys.readouterr().err


def test_four_column_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / server_provider.DOCUMENT
    text = document.read_text(encoding="utf-8")
    text = text.replace(
        '<th data-sort-key="mod"><button data-server-action="sort" '
        'data-server-sort="1">mod</button></th>',
        "",
    )
    document.write_text(text, encoding="utf-8")
    assert server_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "server columns must be" in capsys.readouterr().err


def test_missing_connect_safety_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / server_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace(
            'address.find_first_of("\\n\\r\\\";");', "false;"
        ),
        encoding="utf-8",
    )
    assert server_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "address.find_first_of" in capsys.readouterr().err
