#!/usr/bin/env python3
"""Validate the live RmlUi save/load slot provider and authored slot contract."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
LOAD_DOCUMENT = Path("assets/ui/rml/singleplayer/loadgame.rml")
SAVE_DOCUMENT = Path("assets/ui/rml/singleplayer/savegame.rml")
SINGLEPLAYER_THEME = Path("assets/ui/rml/common/theme/singleplayer.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def _read(path: Path, errors: list[str]) -> str:
    if not path.is_file():
        errors.append(f"missing required file: {path.as_posix()}")
        return ""
    return path.read_text(encoding="utf-8")


def _validate_runtime(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / RUNTIME, errors)
    required_tokens = (
        '#include "server/server.h"',
        "static void UI_Rml_PopulateSaveSlots",
        'QuerySelectorAll(elements, ".save-slot[data-slot]")',
        "SV_GetSaveInfo(slot.c_str())",
        'SetAttribute("data-save-state", occupied ? "ready" : "empty")',
        'action == "loadgame" && !occupied',
        'SetAttribute("disabled", "")',
        'RemoveAttribute("disabled")',
        "Z_Free(save_info)",
        "UI_Rml_PopulateSaveSlots(ui_rml_document)",
    )
    for token in required_tokens:
        if token not in source:
            errors.append(f"runtime save/load provider is missing token: {token}")

    populate = source.find("UI_Rml_PopulateSaveSlots(ui_rml_document)")
    show = source.find("ui_rml_document->Show()", populate)
    focus = source.find("FindNextTabElement", populate)
    if populate < 0 or show < 0 or focus < 0 or not (populate < show < focus):
        errors.append(
            "save slots must be populated before the document is shown and focused"
        )


def _validate_document(
    repo_root: Path,
    relative_path: Path,
    action: str,
    expected_slots: list[str],
    errors: list[str],
) -> None:
    path = repo_root / relative_path
    text = _read(path, errors)
    if not text:
        return

    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError as exc:
        errors.append(f"{relative_path.as_posix()}: invalid XML: {exc}")
        return

    rows = [
        element
        for element in root.iter("button")
        if "save-slot" in element.attrib.get("class", "").split()
    ]
    slots = [row.attrib.get("data-slot", "") for row in rows]
    if slots != expected_slots:
        errors.append(
            f"{relative_path.as_posix()}: expected slots {expected_slots}, got {slots}"
        )

    for row in rows:
        slot = row.attrib.get("data-slot", "")
        if row.attrib.get("data-action-type") != action:
            errors.append(
                f"{relative_path.as_posix()}: {slot or '<missing>'} has wrong data-action-type"
            )
        command = row.attrib.get("data-command", "")
        expected_command = (
            f'load "{slot}"'
            if action == "loadgame"
            else f'save "{slot}"; forcemenuoff'
        )
        if command != expected_command:
            errors.append(
                f"{relative_path.as_posix()}: {slot or '<missing>'} command "
                f"must be {expected_command!r}, got {command!r}"
            )


def _validate_styles(repo_root: Path, errors: list[str]) -> None:
    singleplayer = _read(repo_root / SINGLEPLAYER_THEME, errors)
    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    for state in ("ready", "empty"):
        selector = f'.save-slot[data-save-state="{state}"]'
        if selector not in singleplayer:
            errors.append(f"single-player theme is missing save state selector: {selector}")
        if selector not in accessibility:
            errors.append(f"accessibility theme is missing save state selector: {selector}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []

    _validate_runtime(repo_root, errors)
    _validate_document(
        repo_root,
        LOAD_DOCUMENT,
        "loadgame",
        [f"save{index}" for index in range(16)],
        errors,
    )
    _validate_document(
        repo_root,
        SAVE_DOCUMENT,
        "savegame",
        [f"save{index}" for index in range(1, 16)],
        errors,
    )
    _validate_styles(repo_root, errors)

    print("RmlUi save/load live-provider check")
    print("Load slots checked: 16")
    print("Save slots checked: 15")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi save/load live-provider check failed.")
        return 1

    print("Result: RmlUi save/load live-provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
