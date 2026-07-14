from __future__ import annotations

import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_ui_list_provider as ui_list_provider  # noqa: E402


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _document() -> str:
    rows = "".join(
        f'<button id="ui-list-item-{index}" '
        f'data-label-cvar="ui_list_item_label_{index}" '
        f'data-command-cvar="ui_list_item_cmd_{index}" '
        f'data-visible-if="ui_list_item_show_{index}!=0" '
        f'data-enable-if="ui_list_item_cmd_{index}">Item</button>'
        for index in range(8)
    )
    return f'''<rml><body data-route-version="2" data-document-status="live-provider">
<div id="ui-list-screen" data-close-command="popmenu; worr_ui_list_close">
<button id="utility-ui-list-backplate" data-command="ui.back"></button>
<section id="ui-list-extra-actions" data-visible-if="ui_list_extra_show_0!=0"></section>
<section>
<p id="ui-list-loading" class="ui-loading-state" data-visible-if="ui_list_state=loading"></p>
<p id="ui-list-empty" class="ui-empty-state" data-visible-if="ui_list_state=empty" data-bind="cvars.ui_list_status"></p>
<p id="ui-list-error" class="ui-error-state" data-visible-if="ui_list_state=error" data-bind="cvars.ui_list_status"></p>
{rows}
</section>
<footer id="ui-list-actions" data-visible-if="ui_list_page_label"><button id="ui-list-prev"></button><span id="ui-list-page-label"></span><button id="ui-list-next"></button></footer>
</div></body></rml>'''


def _write_valid_repo(repo_root: Path) -> None:
    _write(
        repo_root / ui_list_provider.SGAME_PROVIDER,
        '''
constexpr int kUiListPageSize = 8;
constexpr int kUiListPublishedSlots = 12;
for (int i = 0; i < kUiListPublishedSlots; ++i) { bool has = i < kUiListPageSize; }
cmd.AppendCvar("ui_list_state", errorState ? "error" : (entries.empty() ? "empty" : "ready"));
cmd.AppendCvar("ui_list_status", status);
fmt::format("ui_list_item_label_{}", i); fmt::format("ui_list_item_cmd_{}", i);
fmt::format("ui_list_item_show_{}", i);
cmd.AppendCvar("ui_list_has_prev", "0"); cmd.AppendCvar("ui_list_has_next", "0");
cmd.AppendCommand("pushmenu ui_list");
''',
    )
    _write(
        repo_root / ui_list_provider.SGAME_COMMANDS,
        '''
RegisterCommand("worr_ui_list_prev", &WorrUiListPrev, 0);
RegisterCommand("worr_ui_list_next", &WorrUiListNext, 0);
RegisterCommand("worr_ui_list_close", &WorrUiListClose, 0);
UiList_Prev(ent); UiList_Next(ent); ent->client->ui.list.kind = UiListKind::None;
''',
    )
    _write(
        repo_root / ui_list_provider.RUNTIME,
        '''
GetAttribute<Rml::String>("data-command-cvar", "");
UI_Rml_RefreshDocumentCvarDisplays(ui_rml_document);
UI_Rml_ElementCondition(element, "data-enable-if", "data-enabled-if");
UI_Rml_ElementIsDisabled(element);
if (command == "ui.back") UI_Rml_CompiledRuntimeHandleBackKey(K_ESCAPE);
UI_Rml_ActiveDocumentCloseCommand();
Cbuf_InsertText(&cmd_buffer, sequence.c_str());
''',
    )
    _write(repo_root / ui_list_provider.DOCUMENT, _document())
    _write(
        repo_root / ui_list_provider.UTILITY_THEME,
        '''
#ui-list-extra-actions { width: 720px; }
#ui-list-content { width: 720px; }
#ui-list-content button { min-height: 36px; white-space: normal; word-break: break-word; }
#ui-list-content button:focus { border-left-color: #ffd967; }
#ui-list-actions { width: 720px; }
#ui-list-page-label { font-family: "WORR Mono"; }
''',
    )


def test_valid_ui_list_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    assert ui_list_provider.main(["--repo-root", str(repo_root)]) == 0
    output = capsys.readouterr().out
    assert "Authored rows checked: 8" in output
    assert "Result: RmlUi fixed-list live-provider check passed." in output


def test_twelfth_authored_row_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / ui_list_provider.DOCUMENT
    text = document.read_text(encoding="utf-8").replace(
        "</section>", '<button id="ui-list-item-8"></button></section>', 1
    )
    document.write_text(text, encoding="utf-8")
    assert ui_list_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "exactly eight ordered rows" in capsys.readouterr().err


def test_actionless_placeholder_row_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    provider = repo_root / ui_list_provider.SGAME_PROVIDER
    provider.write_text(
        provider.read_text(encoding="utf-8")
        + '\nentries.push_back({ "No maps available", "" });\n',
        encoding="utf-8",
    )
    assert ui_list_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "focusable actionless rows" in capsys.readouterr().err


def test_duplicate_footer_back_regression_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    document = repo_root / ui_list_provider.DOCUMENT
    document.write_text(
        document.read_text(encoding="utf-8").replace(
            '<button id="ui-list-next"></button>',
            '<button id="ui-list-next"></button><button id="ui-list-return"></button>',
        ),
        encoding="utf-8",
    )
    assert ui_list_provider.main(["--repo-root", str(repo_root)]) == 1
    assert "must not duplicate" in capsys.readouterr().err
