#!/usr/bin/env python3
"""Validate the live sgame-published RmlUi fixed-list provider contract."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from xml.etree import ElementTree


SGAME_PROVIDER = Path("src/game/sgame/menu/menu_ui_list.cpp")
SGAME_COMMANDS = Path("src/game/sgame/commands/command_client.cpp")
RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
DOCUMENT = Path("assets/ui/rml/utility/ui_list.rml")
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


def _validate_sgame(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / SGAME_PROVIDER, errors)
    _require(
        source,
        (
            "constexpr int kUiListPageSize = 8;",
            "constexpr int kUiListPublishedSlots = 12;",
            "for (int i = 0; i < kUiListPublishedSlots; ++i)",
            "i < kUiListPageSize",
            'cmd.AppendCvar("ui_list_state"',
            'cmd.AppendCvar("ui_list_status", status)',
            'errorState ? "error" : (entries.empty() ? "empty" : "ready")',
            'fmt::format("ui_list_item_label_{}", i)',
            'fmt::format("ui_list_item_cmd_{}", i)',
            'fmt::format("ui_list_item_show_{}", i)',
            'cmd.AppendCvar("ui_list_has_prev"',
            'cmd.AppendCvar("ui_list_has_next"',
            'cmd.AppendCommand("pushmenu ui_list")',
        ),
        "sgame fixed-list provider",
        errors,
    )
    if re.search(r'entries\.push_back\(\{\s*"No [^"]+",\s*""\s*\}\)', source):
        errors.append("sgame provider must publish empty/error status instead of focusable actionless rows")

    commands = _read(repo_root / SGAME_COMMANDS, errors)
    _require(
        commands,
        (
            'RegisterCommand("worr_ui_list_prev", &WorrUiListPrev',
            'RegisterCommand("worr_ui_list_next", &WorrUiListNext',
            'RegisterCommand("worr_ui_list_close", &WorrUiListClose',
            "UiList_Prev(ent)",
            "UiList_Next(ent)",
            "ent->client->ui.list.kind = UiListKind::None",
        ),
        "sgame fixed-list commands",
        errors,
    )


def _validate_runtime(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / RUNTIME, errors)
    _require(
        source,
        (
            'GetAttribute<Rml::String>("data-command-cvar", "")',
            "UI_Rml_RefreshDocumentCvarDisplays(ui_rml_document)",
            'UI_Rml_ElementCondition(element, "data-enable-if", "data-enabled-if")',
            "UI_Rml_ElementIsDisabled(element)",
            'if (command == "ui.back")',
            "UI_Rml_CompiledRuntimeHandleBackKey(K_ESCAPE)",
            "UI_Rml_ActiveDocumentCloseCommand()",
            "Cbuf_InsertText(&cmd_buffer, sequence.c_str())",
        ),
        "RmlUi cvar command runtime",
        errors,
    )


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
    if body is None:
        errors.append("ui_list document is missing its body")
        return
    if body.attrib.get("data-document-status") != "live-provider":
        errors.append("ui_list document must declare data-document-status=live-provider")
    if body.attrib.get("data-route-version") != "2":
        errors.append("ui_list live-provider document must use route version 2")

    elements = {element.attrib.get("id"): element for element in root.iter() if element.attrib.get("id")}
    screen = elements.get("ui-list-screen")
    if screen is None or screen.attrib.get("data-close-command") != "popmenu; worr_ui_list_close":
        errors.append("ui_list screen must close through popmenu plus worr_ui_list_close")
    back = elements.get("utility-ui-list-backplate")
    if back is None or back.attrib.get("data-command") != "ui.back":
        errors.append("ui_list document requires the standard ui.back top backplate")

    toolbar = elements.get("ui-list-extra-actions")
    if toolbar is None or toolbar.attrib.get("data-visible-if") != "ui_list_extra_show_0!=0":
        errors.append("ui_list extra toolbar must disappear when the owner publishes no extras")

    expected_states = {
        "ui-list-loading": ("ui-loading-state", "ui_list_state=loading"),
        "ui-list-empty": ("ui-empty-state", "ui_list_state=empty"),
        "ui-list-error": ("ui-error-state", "ui_list_state=error"),
    }
    for element_id, (class_name, condition) in expected_states.items():
        element = elements.get(element_id)
        if element is None:
            errors.append(f"ui_list document is missing {element_id}")
            continue
        if class_name not in element.attrib.get("class", "").split():
            errors.append(f"{element_id} must use {class_name}")
        if element.attrib.get("data-visible-if") != condition:
            errors.append(f"{element_id} must be controlled by {condition}")
    for element_id in ("ui-list-empty", "ui-list-error"):
        element = elements.get(element_id)
        if element is not None and element.attrib.get("data-bind") != "cvars.ui_list_status":
            errors.append(f"{element_id} must display the owner-published ui_list_status")

    item_buttons = [
        element
        for element in root.iter("button")
        if element.attrib.get("id", "").startswith("ui-list-item-")
    ]
    expected_ids = [f"ui-list-item-{index}" for index in range(8)]
    actual_ids = [button.attrib.get("id") for button in item_buttons]
    if actual_ids != expected_ids:
        errors.append(f"ui_list must author exactly eight ordered rows, got {actual_ids}")
    for index, button in enumerate(item_buttons[:8]):
        expected = {
            "data-label-cvar": f"ui_list_item_label_{index}",
            "data-command-cvar": f"ui_list_item_cmd_{index}",
            "data-visible-if": f"ui_list_item_show_{index}!=0",
            "data-enable-if": f"ui_list_item_cmd_{index}",
        }
        for attribute, value in expected.items():
            if button.attrib.get(attribute) != value:
                errors.append(f"ui-list-item-{index} must declare {attribute}={value}")

    if "ui-list-return" in elements:
        errors.append("ui_list must not duplicate the top backplate with a footer Back control")
    actions = elements.get("ui-list-actions")
    if actions is None or actions.attrib.get("data-visible-if") != "ui_list_page_label":
        errors.append("ui_list paging footer must disappear for a single page")
    elif [child.attrib.get("id") for child in list(actions)] != [
        "ui-list-prev",
        "ui-list-page-label",
        "ui-list-next",
    ]:
        errors.append("ui_list paging controls must be ordered Previous, page label, Next")


def _validate_styles(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / UTILITY_THEME, errors)
    _require(
        source,
        (
            "#ui-list-extra-actions",
            "#ui-list-content",
            "#ui-list-content button",
            "#ui-list-actions",
            "width: 720px",
            "min-height: 36px",
            "white-space: normal",
            "word-break: break-word",
            'font-family: "WORR Mono"',
            "#ui-list-content button:focus",
            "border-left-color: #ffd967",
        ),
        "utility fixed-list theme",
        errors,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    errors: list[str] = []
    repo_root = args.repo_root.resolve()

    _validate_sgame(repo_root, errors)
    _validate_runtime(repo_root, errors)
    _validate_document(repo_root, errors)
    _validate_styles(repo_root, errors)

    print("RmlUi fixed-list live-provider check")
    print("Authored rows checked: 8")
    print("Provider states checked: loading, empty, error")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi fixed-list live-provider check failed.")
        return 1
    print("Result: RmlUi fixed-list live-provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
