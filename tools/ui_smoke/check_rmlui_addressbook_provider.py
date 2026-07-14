#!/usr/bin/env python3
"""Validate the live RmlUi archived-cvar Address Book provider."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
CLIENT_MAIN = Path("src/client/main.cpp")
DOCUMENT = Path("assets/ui/rml/utility/addressbook.rml")
UTILITY_THEME = Path("assets/ui/rml/common/theme/utility.rcss")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")


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
            "static void UI_Rml_ApplyDocumentCvarBindings",
            "UI_Rml_RefreshCvarBindings(document, nullptr, true)",
            "static void UI_Rml_SetCvarFromControl",
            "UI_Rml_SetCvarString(cvar_name, var, value)",
            "UI_Rml_AttachElementCvarListeners(ui_rml_document)",
            "UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)",
            "ui_rml_document->Show()",
            "UI_RML_SERVER_LOCAL_ARGUMENTS",
            'arguments.find("favorites://")',
            'arguments.find("broadcast://")',
            'arguments.find("servers.lst")',
            "UI_Rml_ParseServerAddressBook()",
        ),
        "native address field and favorites-route bridge",
        errors,
    )

    hydrate = source.find("UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)")
    show = source.find("ui_rml_document->Show()", hydrate)
    listeners = source.find("UI_Rml_AttachElementCvarListeners(ui_rml_document)", show)
    if hydrate < 0 or show < hydrate or listeners < show:
        errors.append(
            "Address Book controls must hydrate before first display and attach live listeners"
        )

    client = _read(repo_root / CLIENT_MAIN, errors)
    _require(
        client,
        (
            "for (i = 0; i < MAX_LOCAL_SERVERS; i++)",
            'Cvar_Get(va("adr%i", i), "", CVAR_ARCHIVE)',
            "var->generator = Com_Address_g",
        ),
        "archived address cvar registration",
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
        errors.append("Address Book is missing its body")
        return
    if body.attrib.get("data-document-status") != "live-provider":
        errors.append("Address Book must declare data-document-status=live-provider")
    if body.attrib.get("data-route-version") != "2":
        errors.append("live Address Book must use route version 2")
    if body.attrib.get("data-controller") != "native-cvar-fields":
        errors.append("Address Book must declare native-cvar-fields ownership")

    elements = {
        element.attrib.get("id"): element
        for element in root.iter()
        if element.attrib.get("id")
    }
    for index in range(16):
        element_id = f"addressbook-adr{index}"
        field = elements.get(element_id)
        if field is None:
            errors.append(f"Address Book is missing {element_id}")
            continue
        if field.tag != "input" or field.attrib.get("type") != "text":
            errors.append(f"{element_id} must remain a text input")
        if field.attrib.get("data-cvar") != f"adr{index}":
            errors.append(f"{element_id} must bind archived cvar adr{index}")
        if field.attrib.get("maxlength") != "32":
            errors.append(f"{element_id} must preserve the 32-character address limit")
        if field.attrib.get("data-menu-sound-change") != "change":
            errors.append(f"{element_id} must retain change feedback")

    browse = elements.get("addressbook-browse")
    expected_command = (
        'pushmenu servers "favorites://" "file:///servers.lst" "broadcast://"'
    )
    if browse is None:
        errors.append("Address Book requires the Browse Favorites action")
    else:
        if browse.attrib.get("data-command") != expected_command:
            errors.append("Browse Favorites must preserve all three legacy server sources")
        if browse.attrib.get("data-route-target") != "servers":
            errors.append("Browse Favorites must target the live servers route")
        if "Favorites" not in "".join(browse.itertext()):
            errors.append("Browse action must identify Favorites to the player")


def _validate_styles_and_capture(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / UTILITY_THEME, errors)
    _require(
        source,
        (
            "#addressbook-content",
            "#addressbook-fields",
            "#addressbook-field-grid .utility-field",
            "width: 206px",
            "#addressbook-field-grid .utility-field input",
            'font-family: "WORR Mono"',
            "font-size: 13px",
        ),
        "Address Book four-column address layout",
        errors,
    )

    harness = _read(repo_root / CAPTURE_HARNESS, errors)
    if '"addressbook": Path("utility/addressbook.rml")' not in harness:
        errors.append("capture harness is missing the addressbook route")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []

    _validate_runtime(repo_root, errors)
    _validate_document(repo_root, errors)
    _validate_styles_and_capture(repo_root, errors)

    print("RmlUi live Address Book provider check")
    print("Archived address fields checked: 16")
    print("Browse source tokens checked: favorites, saved file, broadcast")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live Address Book provider check failed.")
        return 1
    print("Result: RmlUi live Address Book provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
