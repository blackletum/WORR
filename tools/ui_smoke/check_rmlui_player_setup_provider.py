#!/usr/bin/env python3
"""Validate the live RmlUi Player Setup provider and native preview contract."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
RENDERER_BRIDGE = Path("src/renderer/rmlui_bridge.cpp")
DOCUMENT = Path("assets/ui/rml/utility/players.rml")
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
            "static void UI_Rml_LoadPlayerModels(void)",
            'FS_ListFiles("players", NULL, FS_SEARCH_DIRSONLY',
            'Q_concat(scratch, sizeof(scratch), "players/", dir, "/tris.md2")',
            'strstr(name, "_i.")',
            'Q_stricmp(a.directory.c_str(), "male")',
            'Q_stricmp(a.directory.c_str(), "female")',
            "static void UI_Rml_PopulatePlayerConfig",
            'Cvar_VariableStringBuffer("skin"',
            'Cvar_Get("skin", "male/grunt"',
            'model_select->AddEventListener(Rml::EventId::Change',
            'skin_select->AddEventListener(Rml::EventId::Change',
            "static void UI_Rml_PopulateDirectorySelects",
            'QuerySelectorAll(elements, "select[data-source-dir]")',
            "UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)",
            "UI_Rml_AttachElementCvarListeners(ui_rml_document)",
        ),
        "native player configuration provider",
        errors,
    )

    _require(
        source,
        (
            "ui_rml_player_preview_stages[]",
            "FRAME_stand01, FRAME_stand40",
            "FRAME_attack1, FRAME_attack8",
            "FRAME_death101, FRAME_death106",
            "static void UI_Rml_BuildPlayerPreviewWeapons",
            'FS_ListFiles(directory, ".md2"',
            "UI_Rml_IsPlayerWeaponModel",
            "ui_rml_player_preview_weapon_index",
            "ui_rml_player_preview_muzzle_flash_until",
            "refdef.num_entities = num_entities",
            "refdef.num_dlights = num_dlights",
            "RDF_NOWORLDMODEL",
            "R_RenderFrame(&refdef)",
            "ui_rml_reduced_motion->integer",
            'UI_Rml_SetPlayerPreviewState(document, "empty")',
            '? "ready"\n            : "error"',
            "UI_Rml_RenderPlayerPreview()",
        ),
        "native player preview",
        errors,
    )

    populate = source.find("UI_Rml_PopulatePlayerConfig(ui_rml_document)")
    bindings = source.find("UI_Rml_ApplyDocumentCvarBindings(ui_rml_document)", populate)
    show = source.find("ui_rml_document->Show()", populate)
    if populate < 0 or bindings < populate or show < bindings:
        errors.append("player provider must populate before cvar binding and first document display")

    document_render = source.find("const bool rendered = ui_rml_context->Render()")
    preview_render = source.find("UI_Rml_RenderPlayerPreview()", document_render)
    if document_render < 0 or preview_render < document_render:
        errors.append("native player preview must render after the RmlUi document")


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
        errors.append("Player Setup document is missing its body")
        return
    if body.attrib.get("data-document-status") != "live-provider":
        errors.append("Player Setup must declare data-document-status=live-provider")
    if body.attrib.get("data-route-version") != "2":
        errors.append("live Player Setup must use route version 2")
    if body.attrib.get("data-controller") != "native-player-config":
        errors.append("Player Setup must declare native-player-config ownership")

    elements = {
        element.attrib.get("id"): element
        for element in root.iter()
        if element.attrib.get("id")
    }

    expected_cvars = {
        "players-name": "name",
        "players-dogtag": "dogtag",
        "players-hand": "hand",
    }
    for element_id, cvar in expected_cvars.items():
        element = elements.get(element_id)
        if element is None or element.attrib.get("data-cvar") != cvar:
            errors.append(f"{element_id} must bind the live {cvar} cvar")

    name = elements.get("players-name")
    if name is not None and name.attrib.get("maxlength") != "15":
        errors.append("player name input must preserve the 15-character client-name limit")

    for element_id in ("players-model", "players-skin"):
        element = elements.get(element_id)
        if element is None:
            errors.append(f"Player Setup is missing provider select {element_id}")
        elif "data-cvar" in element.attrib:
            errors.append(f"{element_id} must use the composite model/skin provider, not a scalar cvar")

    dogtag = elements.get("players-dogtag")
    if dogtag is not None and dogtag.attrib.get("data-source-dir") != "tags":
        errors.append("dogtag select must enumerate the tags directory")

    thumbnail_contracts = {
        "players-skin-thumbnail": ("skin", "/players/", "_i.pcx"),
        "players-dogtag-thumbnail": ("dogtag", "/tags/", ".pcx"),
    }
    for element_id, (cvar, prefix, suffix) in thumbnail_contracts.items():
        element = elements.get(element_id)
        if element is None:
            errors.append(f"Player Setup is missing live image {element_id}")
            continue
        if element.attrib.get("data-src-cvar") != cvar:
            errors.append(f"{element_id} must follow the live {cvar} cvar")
        if element.attrib.get("data-src-prefix") != prefix:
            errors.append(f"{element_id} must use image prefix {prefix}")
        if suffix is not None and element.attrib.get("data-src-suffix") != suffix:
            errors.append(f"{element_id} must use image suffix {suffix}")

    hand = elements.get("players-hand")
    hand_options = [] if hand is None else [
        option.attrib.get("value") for option in hand.iter("option")
    ]
    if hand_options != ["0", "1", "2"]:
        errors.append("weapon-hand options must remain Right/Left/Center values 0/1/2")

    preview = elements.get("players-preview")
    if preview is None or preview.attrib.get("data-component") != "player_preview":
        errors.append("Player Setup requires the native player_preview component")
    if elements.get("players-preview-surface") is None:
        errors.append("Player Setup requires the native preview surface anchor")

    expected_states = {
        "players-preview-loading": "ui-loading-state",
        "players-preview-empty": "ui-empty-state",
        "players-preview-error": "ui-error-state",
    }
    for element_id, class_name in expected_states.items():
        element = elements.get(element_id)
        if element is None or class_name not in element.attrib.get("class", "").split():
            errors.append(f"Player Setup is missing authored {class_name} state {element_id}")

    if "players-actions" in elements:
        errors.append("live-immediate Player Setup must not leave an empty actions footer")


def _validate_renderer_bridge(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / RENDERER_BRIDGE, errors)
    _require(
        source,
        (
            "const R_RmlUiTexture texture_info = TextureForHandle(texture)",
            "texture_info.sl + src.st[0] * (texture_info.sh - texture_info.sl)",
            "texture_info.tl + src.st[1] * (texture_info.th - texture_info.tl)",
            "IF_REPEAT | IF_NOSCRAP",
            "image->sl, image->sh",
            "image->tl, image->th",
            "float sl = 0.0f",
            "float sh = 1.0f",
        ),
        "OpenGL RmlUi cached scrap-atlas bridge",
        errors,
    )


def _validate_styles(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / UTILITY_THEME, errors)
    _require(
        source,
        (
            "#players-content",
            "#players-form",
            "#players-preview",
            "#players-preview-surface",
            "#players-appearance-thumbnails",
            "#players-skin-thumbnail",
            "#players-dogtag-thumbnail",
            "min-height: 320px",
            ".preview-surface",
            "word-break: break-word",
        ),
        "Player Setup utility layout",
        errors,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []

    _validate_runtime(repo_root, errors)
    _validate_renderer_bridge(repo_root, errors)
    _validate_document(repo_root, errors)
    _validate_styles(repo_root, errors)

    print("RmlUi Player Setup live-provider check")
    print("Live cvar controls checked: 3")
    print("Preview states checked: loading, empty, error")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi Player Setup live-provider check failed.")
        return 1
    print("Result: RmlUi Player Setup live-provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
