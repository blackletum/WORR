from __future__ import annotations

import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_save_load_provider as save_load_provider  # noqa: E402


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _valid_document(action: str, slots: range) -> str:
    rows = []
    for index in slots:
        slot = f"save{index}"
        command = (
            f'load &quot;{slot}&quot;'
            if action == "loadgame"
            else f'save &quot;{slot}&quot;; forcemenuoff'
        )
        rows.append(
            f'<button class="save-slot" data-action-type="{action}" '
            f'data-slot="{slot}" data-command="{command}">Slot</button>'
        )
    return "<rml><body>" + "".join(rows) + "</body></rml>"


def _write_valid_repo(repo_root: Path) -> None:
    _write(
        repo_root / save_load_provider.RUNTIME,
        """
#include "server/server.h"
static void UI_Rml_PopulateSaveSlots() {
  QuerySelectorAll(elements, ".save-slot[data-slot]");
  SV_GetSaveInfo(slot.c_str());
  SetAttribute("data-save-state", occupied ? "ready" : "empty");
  if (action == "loadgame" && !occupied) SetAttribute("disabled", "");
  else RemoveAttribute("disabled");
  Z_Free(save_info);
}
UI_Rml_PopulateSaveSlots(ui_rml_document);
ui_rml_document->Show();
FindNextTabElement();
""",
    )
    _write(
        repo_root / save_load_provider.LOAD_DOCUMENT,
        _valid_document("loadgame", range(16)),
    )
    _write(
        repo_root / save_load_provider.SAVE_DOCUMENT,
        _valid_document("savegame", range(1, 16)),
    )
    styles = (
        '.save-slot[data-save-state="ready"] {}\n'
        '.save-slot[data-save-state="empty"] {}\n'
    )
    _write(repo_root / save_load_provider.SINGLEPLAYER_THEME, styles)
    _write(repo_root / save_load_provider.ACCESSIBILITY_THEME, styles)


def test_valid_live_provider_passes(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)

    assert save_load_provider.main(["--repo-root", str(repo_root)]) == 0
    captured = capsys.readouterr()
    assert "Load slots checked: 16" in captured.out
    assert "Save slots checked: 15" in captured.out
    assert "Result: RmlUi save/load live-provider check passed." in captured.out


def test_missing_runtime_hydration_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime_path = repo_root / save_load_provider.RUNTIME
    runtime_path.write_text(
        runtime_path.read_text(encoding="utf-8").replace("SV_GetSaveInfo(slot.c_str());", ""),
        encoding="utf-8",
    )

    assert save_load_provider.main(["--repo-root", str(repo_root)]) == 1
    captured = capsys.readouterr()
    assert "SV_GetSaveInfo(slot.c_str())" in captured.err


def test_slot_command_drift_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    load_path = repo_root / save_load_provider.LOAD_DOCUMENT
    load_path.write_text(
        load_path.read_text(encoding="utf-8").replace(
            'data-command="load &quot;save4&quot;"',
            'data-command="load &quot;wrong&quot;"',
        ),
        encoding="utf-8",
    )

    assert save_load_provider.main(["--repo-root", str(repo_root)]) == 1
    captured = capsys.readouterr()
    assert "save4 command" in captured.err


def test_show_before_hydration_fails(tmp_path: Path, capsys) -> None:
    repo_root = tmp_path / "repo"
    _write_valid_repo(repo_root)
    runtime_path = repo_root / save_load_provider.RUNTIME
    source = runtime_path.read_text(encoding="utf-8")
    runtime_path.write_text(
        source.replace(
            "UI_Rml_PopulateSaveSlots(ui_rml_document);\nui_rml_document->Show();",
            "ui_rml_document->Show();\nUI_Rml_PopulateSaveSlots(ui_rml_document);",
        ),
        encoding="utf-8",
    )

    assert save_load_provider.main(["--repo-root", str(repo_root)]) == 1
    captured = capsys.readouterr()
    assert "before the document is shown and focused" in captured.err
