#!/usr/bin/env python3
"""Validate the live RmlUi demo-browser provider and authored table contract."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
DOCUMENT = Path("assets/ui/rml/utility/demos.rml")
UTILITY_THEME = Path("assets/ui/rml/common/theme/utility.rcss")


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
        'UI_RML_DEMO_EXTENSIONS =\n    ".dm2;.dm2.gz;.mvd2;.mvd2.gz"',
        "static void UI_Rml_BuildDemoEntries",
        "FS_SEARCH_DIRSONLY",
        "FS_SEARCH_EXTRAINFO",
        "CL_GetDemoInfo(path.c_str(), &metadata.info)",
        "ui_rml_demo_metadata_cache.insert_or_assign",
        "UI_Rml_SortDemoEntries()",
        "UI_RML_DEMO_PAGE_SIZE",
        "UI_Rml_DemoFilenameIsSafe",
        "UI_Rml_QueueCommand(va(\"demo \\\"%s\\\"\"",
        'action == "toggle-source"',
        'action == "previous-page"',
        'action == "next-page"',
        'action == "sort"',
        'action != "activate"',
        'QuerySelector(".demo-row.is-selected")',
        'row->SetClass("is-selected", true)',
        "UI_Rml_PopulateDemoBrowser(ui_rml_document)",
    )
    for token in required_tokens:
        if token not in source:
            errors.append(f"runtime demo provider is missing token: {token}")

    populate = source.find("UI_Rml_PopulateDemoBrowser(ui_rml_document)")
    show = source.find("ui_rml_document->Show()", populate)
    if populate < 0 or show < 0 or populate > show:
        errors.append("demo rows must be populated before the document is shown")

    unsafe_checks = (
        "character == '\\n'",
        "character == '\\r'",
        "character == '\"'",
        "character == ';'",
    )
    for token in unsafe_checks:
        if token not in source:
            errors.append(f"demo filename validation is missing: {token}")


def _validate_document(repo_root: Path, errors: list[str]) -> None:
    path = repo_root / DOCUMENT
    text = _read(path, errors)
    if not text:
        return

    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError as exc:
        errors.append(f"{DOCUMENT.as_posix()}: invalid XML: {exc}")
        return

    body = next(root.iter("body"), None)
    if body is None or body.attrib.get("data-document-status") != "live-provider":
        errors.append("demo document must declare data-document-status=live-provider")

    buttons = {element.attrib.get("id"): element for element in root.iter("button")}
    expected_actions = {
        "demos-up": "up",
        "demos-refresh": "refresh",
        "demos-source": "toggle-source",
        "demos-previous": "previous-page",
        "demos-next": "next-page",
    }
    for element_id, action in expected_actions.items():
        element = buttons.get(element_id)
        if element is None:
            errors.append(f"demo document is missing toolbar control {element_id}")
        elif element.attrib.get("data-demo-action") != action:
            errors.append(f"{element_id} must dispatch demo action {action}")
        elif "disabled" in element.attrib and element_id in {
            "demos-refresh",
            "demos-source",
        }:
            errors.append(f"{element_id} must be available in the authored document")

    table = next(
        (element for element in root.iter("table") if element.attrib.get("id") == "demos-table"),
        None,
    )
    if table is None:
        errors.append("demo document is missing demos-table")
        return

    headers = [
        header.attrib.get("data-sort-key")
        for header in table.iter("th")
    ]
    if headers != ["name", "date", "size", "map", "pov"]:
        errors.append(f"demo columns must be name/date/size/map/pov, got {headers}")

    tbody = next(
        (element for element in table.iter("tbody") if element.attrib.get("id") == "demos-table-body"),
        None,
    )
    if tbody is None:
        errors.append("demo table requires the demos-table-body provider target")

    sort_buttons = [
        element for element in table.iter("button")
        if element.attrib.get("data-demo-action") == "sort"
    ]
    if [element.attrib.get("data-demo-sort") for element in sort_buttons] != [
        "0", "1", "2", "3", "4"
    ]:
        errors.append("demo sort controls must cover all five ordered columns")


def _validate_styles(repo_root: Path, errors: list[str]) -> None:
    styles = _read(repo_root / UTILITY_THEME, errors)
    required_tokens = (
        "#demos-table th:nth-child(5)",
        ".data-table .table-sort-button",
        ".data-table .data-table-row-action",
        ".utility-page-label",
        "min-height: 36px",
        "border-left-color: #83d18f",
    )
    for token in required_tokens:
        if token not in styles:
            errors.append(f"utility theme is missing demo table token: {token}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []

    _validate_runtime(repo_root, errors)
    _validate_document(repo_root, errors)
    _validate_styles(repo_root, errors)

    print("RmlUi demo-browser live-provider check")
    print("Columns checked: 5")
    print("Toolbar actions checked: 5")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi demo-browser live-provider check failed.")
        return 1

    print("Result: RmlUi demo-browser live-provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
