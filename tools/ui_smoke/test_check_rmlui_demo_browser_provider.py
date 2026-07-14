from __future__ import annotations

import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_demo_browser_provider as demo_provider  # noqa: E402


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _valid_document() -> str:
    headers = []
    for index, name in enumerate(("name", "date", "size", "map", "pov")):
        headers.append(
            f'<th data-sort-key="{name}"><button data-demo-action="sort" '
            f'data-demo-sort="{index}">{name}</button></th>'
        )
    toolbar = "".join(
        f'<button id="{element_id}" data-demo-action="{action}">{action}</button>'
        for element_id, action in (
            ("demos-up", "up"),
            ("demos-refresh", "refresh"),
            ("demos-source", "toggle-source"),
            ("demos-previous", "previous-page"),
            ("demos-next", "next-page"),
        )
    )
    return (
        '<rml><body data-document-status="live-provider">'
        + toolbar
        + '<table id="demos-table"><thead><tr>'
        + "".join(headers)
        + '</tr></thead><tbody id="demos-table-body"></tbody></table>'
        + "</body></rml>"
    )


def _write_valid_repo(repo_root: Path) -> None:
    _write(
        repo_root / demo_provider.RUNTIME,
        r'''
static constexpr const char *UI_RML_DEMO_EXTENSIONS =
    ".dm2;.dm2.gz;.mvd2;.mvd2.gz";
static constexpr int UI_RML_DEMO_PAGE_SIZE = 8;
static void UI_Rml_BuildDemoEntries() {
  FS_SEARCH_DIRSONLY; FS_SEARCH_EXTRAINFO;
  CL_GetDemoInfo(path.c_str(), &metadata.info);
  ui_rml_demo_metadata_cache.insert_or_assign(path, metadata);
  UI_Rml_SortDemoEntries();
  UI_Rml_DemoFilenameIsSafe(name);
  UI_Rml_QueueCommand(va("demo \"%s\"", path.c_str()));
  if (action == "toggle-source") {}
  if (action == "previous-page") {}
  if (action == "next-page") {}
  if (action == "sort") {}
  if (action != "activate") {}
  QuerySelector(".demo-row.is-selected");
  row->SetClass("is-selected", true);
  if (character == '\n' || character == '\r' ||
      character == '"' || character == ';') {}
}
UI_Rml_PopulateDemoBrowser(ui_rml_document);
ui_rml_document->Show();
''',
    )
    _write(repo_root / demo_provider.DOCUMENT, _valid_document())
    _write(
        repo_root / demo_provider.UTILITY_THEME,
        """
#demos-table th:nth-child(5) {}
.data-table .table-sort-button {}
.data-table .data-table-row-action {}
.utility-page-label {}
.data-table td { min-height: 36px; }
.data-table tr.is-selected td { border-left-color: #83d18f; }
""",
    )


def test_valid_demo_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)

    assert demo_provider.main(["--repo-root", str(repo_root)]) == 0
    captured = capsys.readouterr()
    assert "Columns checked: 5" in captured.out
    assert "Toolbar actions checked: 5" in captured.out
    assert "Result: RmlUi demo-browser live-provider check passed." in captured.out


def test_placeholder_toolbar_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / demo_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            'id="demos-refresh" data-demo-action="refresh"',
            'id="demos-refresh" disabled="disabled"',
        ),
        encoding="utf-8",
    )

    assert demo_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "demos-refresh must dispatch demo action refresh" in capsys.readouterr().err


def test_missing_security_check_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / demo_provider.RUNTIME
    runtime.write_text(
        runtime.read_text(encoding="utf-8").replace("character == ';'", "false"),
        encoding="utf-8",
    )

    assert demo_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "demo filename validation is missing" in capsys.readouterr().err


def test_show_before_population_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime = repo_root / demo_provider.RUNTIME
    source = runtime.read_text(encoding="utf-8")
    runtime.write_text(
        source.replace(
            "UI_Rml_PopulateDemoBrowser(ui_rml_document);\nui_rml_document->Show();",
            "ui_rml_document->Show();\nUI_Rml_PopulateDemoBrowser(ui_rml_document);",
        ),
        encoding="utf-8",
    )

    assert demo_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "before the document is shown" in capsys.readouterr().err
