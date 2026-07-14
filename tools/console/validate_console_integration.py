#!/usr/bin/env python3
"""Validate the FnQuake3-to-WORR in-game console integration contract."""

from __future__ import annotations

import argparse
from pathlib import Path


REQUIRED_CVARS = (
    "con_scroll_lines",
    "con_scroll_smooth",
    "con_scroll_smooth_speed",
    "con_completion_popup",
    "con_screen_extents",
    "con_background_style",
    "con_background_color",
    "con_background_opacity",
    "con_line_color",
    "con_version_color",
    "con_show_version",
    "con_fade",
    "con_say_raw",
)

REQUIRED_CONSOLE_SYMBOLS = (
    "Con_UpdateDisplayLine",
    "Con_RefreshCompletionState",
    "Con_FuzzyCompletionScore",
    "Con_ApplySelectedCompletion",
    "Con_DrawCompletionPopup",
    "Con_UpdateCompletionScrollbarDrag",
    "Con_UpdateScrollbarDrag",
    "Con_DrawLogSelectionRow",
    "Con_CopyLogSelection",
    "Con_InsertInputText",
    "Con_MouseEvent",
    "Con_MouseMove",
    "Con_MouseButton",
    '"con_test"',
)


def require(text: str, needles: tuple[str, ...], label: str, failures: list[str]) -> None:
    for needle in needles:
        if needle not in text:
            failures.append(f"{label}: missing {needle}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.root.resolve()

    console = (root / "src/client/console.cpp").read_text(encoding="utf-8")
    input_source = (root / "src/client/input.cpp").read_text(encoding="utf-8")
    keys = (root / "src/client/keys.cpp").read_text(encoding="utf-8")
    ui_bridge = (root / "src/client/ui_bridge.cpp").read_text(encoding="utf-8")
    user_doc = (root / "docs-user/console.md").read_text(encoding="utf-8")
    plan = (root / "docs-dev/plans/fnquake3-console-integration-roadmap.md").read_text(encoding="utf-8")

    failures: list[str] = []
    require(console, REQUIRED_CVARS, "console cvar contract", failures)
    require(console, REQUIRED_CONSOLE_SYMBOLS, "console feature contract", failures)
    require(
        console,
        ("Cmd_Command_g", "Cvar_Variable_g", "Cmd_Alias_g", "Com_Generic_c"),
        "completion generator integration",
        failures,
    )
    require(
        input_source,
        ("IN_MouseGrabbed", "Con_MouseMove", "KEY_CONSOLE"),
        "captured console pointer routing",
        failures,
    )
    require(keys, ("Con_MouseButton(true)", "Con_MouseButton(false)"),
            "console mouse button routing", failures)
    require(ui_bridge, ("Con_MouseEvent(x, y)", "KEY_CONSOLE"),
            "absolute console pointer routing", failures)
    require(user_doc, REQUIRED_CVARS, "user documentation", failures)
    require(plan, tuple(f"FR-11-T0{i}" for i in range(1, 8)),
            "project task coverage", failures)

    forbidden = ("rend_gl", "gl_", "REF_GL")
    console_renderer_section = console[console.find("Con_DrawSolidConsole"):]
    for token in forbidden:
        if token in console_renderer_section:
            failures.append(
                f"renderer neutrality: console drawing unexpectedly references {token}"
            )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"console integration validation failed: {len(failures)} issue(s)")
        return 1

    print("console integration validation passed")
    print(f"  cvars: {len(REQUIRED_CVARS)}")
    print(f"  feature symbols: {len(REQUIRED_CONSOLE_SYMBOLS)}")
    print("  completion generators: commands, cvars, aliases, command arguments")
    print("  input routes: captured relative and ungrabbed absolute mouse")
    print("  renderer contract: shared renderer only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
