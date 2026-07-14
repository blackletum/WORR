#!/usr/bin/env python3
"""Validate the live two-slot RmlUi keybind provider and route family."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from xml.etree import ElementTree


RUNTIME = Path("src/client/ui_rml/ui_rml_runtime.cpp")
UTILITY_THEME = Path("assets/ui/rml/common/theme/utility.rcss")
ACCESSIBILITY_THEME = Path("assets/ui/rml/common/theme/accessibility.rcss")
BASE_THEME = Path("assets/ui/rml/common/theme/base.rcss")
CAPTURE_HARNESS = Path("tools/ui_smoke/check_rmlui_runtime_capture.py")
COMPONENT = Path("assets/ui/rml/common/components/keybind.rml")
DOCUMENTS = {
    "keys": (
        Path("assets/ui/rml/utility/keys.rml"),
        {
            "+attack", "weapnext", "weapprev", "inven", "invuse",
            "invdrop", "invprev", "invnext", "+forward", "+back",
            "+speed", "+moveleft", "+moveright", "+moveup", "+movedown",
            "help", "pause", "score", "messagemode",
        },
    ),
    "legacykeys": (
        Path("assets/ui/rml/utility/legacykeys.rml"),
        {
            "+left", "+right", "+strafe", "+lookup", "+lookdown",
            "centerview", "+mlook", "+klook",
        },
    ),
    "weapons": (
        Path("assets/ui/rml/utility/weapons.rml"),
        {
            "use Blaster", "use Shotgun", "use Super Shotgun",
            "use Machinegun", "use Chaingun", "use Grenade Launcher",
            "use Rocket Launcher", "use HyperBlaster", "use Railgun",
            "use BFG10K", "use Grenades",
        },
    ),
}


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


def _classes(element: ElementTree.Element) -> set[str]:
    return set(element.attrib.get("class", "").split())


def _validate_runtime(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / RUNTIME, errors)
    _require(
        source,
        (
            "static int UI_Rml_KeybindKeyForSlot",
            '"data-bind-slot", "0"',
            "Key_EnumBindings(key, command)",
            "Key_KeynumToString(keynum)",
            "static void UI_Rml_AssignKeybindSlot",
            "const int other_key =",
            "normalize any\n    // console-created extras back to the two-slot UI contract",
            "static void UI_Rml_ClearKeybindSlot",
            "key == K_BACKSPACE || key == K_DEL",
            "key == K_ESCAPE",
            "Sys_Milliseconds() - ui_rml_keybind_capture_started >= 8000u",
            "Key_GetBindingForKey(key)",
            "Q_stricmp(previous_command, command.c_str())",
            "static void UI_Rml_ShowKeybindConflict",
            "static void UI_Rml_CommitKeybindConflict",
            '"data-keybind-conflict-action", ""',
            'action == "replace"',
            'action == "cancel"',
            "if (ui_rml_keybind_conflict_element)",
            "static void UI_Rml_ApplyPreloadAccessibilityClasses",
            'contents.find("<body")',
            '" class=\\\"" + classes + "\\\""',
            "UI_Rml_ApplyPreloadAccessibilityClasses(document_contents)",
            'body->SetClass("ui-reduced-motion", reduced_motion)',
        ),
        "native keybind capture provider",
        errors,
    )
    _require(
        source,
        (
            '"/gfx/controller/keyboard/%i.png"',
            '"/gfx/controller/mouse/f000%i.png"',
            '"/gfx/controller/generic/f%04x.png"',
            "UI_Rml_MapKeynumToKeyboardIcon",
            "UI_Rml_MapKeynumToMouseIcon",
            "UI_Rml_SetKeybindIcon(element, keynum)",
        ),
        "device-aware keybind icon provider",
        errors,
    )
    if "UI_Rml_UnbindKeybindCommand" in source:
        errors.append("keybind capture must not restore destructive command-wide replacement")


def _validate_document(
    repo_root: Path,
    route_id: str,
    relative_path: Path,
    expected_commands: set[str],
    errors: list[str],
) -> None:
    text = _read(repo_root / relative_path, errors)
    if not text:
        return
    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError as exc:
        errors.append(f"{relative_path.as_posix()}: invalid XML: {exc}")
        return

    body = next(root.iter("body"), None)
    if body is None:
        errors.append(f"{route_id}: missing body")
        return
    if body.attrib.get("data-document-status") != "live-provider":
        errors.append(f"{route_id}: must declare data-document-status=live-provider")
    if body.attrib.get("data-route-version") != "2":
        errors.append(f"{route_id}: live keybind route must use version 2")
    if body.attrib.get("data-controller") != "native-keybind-capture":
        errors.append(f"{route_id}: body must declare native-keybind-capture ownership")

    rows = [element for element in root.iter() if "bind-row" in _classes(element)]
    if len(rows) != len(expected_commands):
        errors.append(
            f"{route_id}: expected {len(expected_commands)} bind rows, found {len(rows)}"
        )

    observed_commands: set[str] = set()
    for row in rows:
        label = next(
            (element for element in row.iter() if "bind-label" in _classes(element)),
            None,
        )
        if label is None or not "".join(label.itertext()).strip():
            errors.append(f"{route_id}: every binding row requires a visible label")

        slots = [
            element
            for element in row.iter("button")
            if "bind-slot" in _classes(element)
        ]
        if len(slots) != 2:
            errors.append(f"{route_id}: every binding row requires two binding slots")
            continue

        commands = {slot.attrib.get("data-bind-command", "") for slot in slots}
        slot_ids = {slot.attrib.get("data-bind-slot", "") for slot in slots}
        if len(commands) != 1 or "" in commands:
            errors.append(f"{route_id}: paired slots must share one non-empty command")
        else:
            observed_commands.update(commands)
        if slot_ids != {"0", "1"}:
            errors.append(f"{route_id}: paired slots must be Primary=0 and Alternate=1")

        for slot in slots:
            if slot.attrib.get("data-event-click") != "keybind.capture":
                errors.append(f"{route_id}: every slot must dispatch keybind.capture")
            if not any("bind-icon" in _classes(element) for element in slot.iter("img")):
                errors.append(f"{route_id}: every slot requires a device icon")
            if not any("bind-key" in _classes(element) for element in slot.iter("span")):
                errors.append(f"{route_id}: every slot requires text fallback")

        conflicts = [
            element for element in row.iter()
            if element.attrib.get("data-keybind-conflict") == "true"
        ]
        if len(conflicts) != 1:
            errors.append(f"{route_id}: every binding row requires one conflict row")
        else:
            actions = {
                element.attrib.get("data-keybind-conflict-action")
                for element in conflicts[0].iter("button")
            }
            if actions != {"replace", "cancel"}:
                errors.append(f"{route_id}: conflict row requires Replace and Cancel")

    if observed_commands != expected_commands:
        missing = sorted(expected_commands - observed_commands)
        extra = sorted(observed_commands - expected_commands)
        errors.append(f"{route_id}: command parity mismatch missing={missing} extra={extra}")

    action_footers = [
        element for element in root.iter("footer")
        if "actions" in _classes(element)
    ]
    for footer in action_footers:
        if not list(footer):
            errors.append(f"{route_id}: must not retain an empty actions footer")


def _validate_styles(repo_root: Path, errors: list[str]) -> None:
    source = _read(repo_root / UTILITY_THEME, errors)
    _require(
        source,
        (
            ".bind-row",
            ".bind-slots",
            ".bind-slot-label",
            ".bind-icon",
            ".bind-slot.is-capturing",
            "@keyframes bind-capture-pulse",
            ".bind-slot.has-conflict",
            ".bind-row.has-bind-conflict",
            ".bind-conflict-detail",
            ".bind-conflict-actions button",
        ),
        "keybind family layout and state styling",
        errors,
    )

    accessibility = _read(repo_root / ACCESSIBILITY_THEME, errors)
    _require(
        accessibility,
        (
            "Pin animated containers to their completed",
            ".ui-reduced-motion .screen",
            ".ui-reduced-motion .content",
            ".ui-reduced-motion .worr-topbar",
            ".ui-reduced-motion .worr-statusbar",
            "opacity: 1;",
        ),
        "reduced-motion completed visual state",
        errors,
    )

    base_theme = _read(repo_root / BASE_THEME, errors)
    forbidden_entrance_animations = (
        "animation: 0.18s cubic-out route-enter",
        "animation: 0.3s cubic-out header-enter",
        "animation: 0.45s cubic-out content-enter",
        "animation: 0.55s cubic-out footer-enter",
        "animation: 0.14s cubic-out popup-enter",
        "animation: 0.2s cubic-out panel-fade",
        "animation: 0.1s cubic-out panel-fade",
    )
    for token in forbidden_entrance_animations:
        if token in base_theme:
            errors.append(
                "shared theme must not restore unreliable load-time entrance animation: "
                + token
            )


def _validate_support_files(repo_root: Path, errors: list[str]) -> None:
    harness = _read(repo_root / CAPTURE_HARNESS, errors)
    for route_id in DOCUMENTS:
        if f'"{route_id}": Path("utility/{route_id}.rml")' not in harness:
            errors.append(f"capture harness is missing the {route_id} route")

    component = _read(repo_root / COMPONENT, errors)
    if "No C++ capture controller is implemented" in component:
        errors.append("shared keybind component still denies the live native controller")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors: list[str] = []

    _validate_runtime(repo_root, errors)
    for route_id, (relative_path, expected_commands) in DOCUMENTS.items():
        _validate_document(
            repo_root, route_id, relative_path, expected_commands, errors
        )
    _validate_styles(repo_root, errors)
    _validate_support_files(repo_root, errors)

    print("RmlUi live keybind-provider check")
    print(f"Routes checked: {len(DOCUMENTS)}")
    print(f"Commands checked: {sum(len(commands) for _, commands in DOCUMENTS.values())}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print("Result: RmlUi live keybind-provider check failed.")
        return 1
    print("Result: RmlUi live keybind-provider check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
