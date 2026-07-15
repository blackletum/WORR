#!/usr/bin/env python3
"""Headless structural coverage for the Windows renderer-capture window mode."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WIN_CLIENT = (ROOT / "src/windows/client.c").read_text(encoding="utf-8")
CAPTURE_MATRIX = (
    ROOT / "tools/renderer_parity/run_capture_matrix.py"
).read_text(encoding="utf-8")
DEBUG_SMOKE = (
    ROOT / "tools/renderer_parity/run_vk_debug_smoke.py"
).read_text(encoding="utf-8")


class WindowsHeadlessRendererSourceTests(unittest.TestCase):
    def test_hidden_window_mode_keeps_a_real_surface_without_focus_or_desktop_mode_changes(self) -> None:
        self.assertIn('Cvar_Get("win_headless", "0", CVAR_NOARCHIVE)', WIN_CLIENT)
        self.assertIn("static bool win_is_headless(void)", WIN_CLIENT)
        self.assertIn("SWP_NOACTIVATE : SWP_SHOWWINDOW", WIN_CLIENT)
        self.assertIn("ShowWindow(win.wnd, SW_HIDE)", WIN_CLIENT)
        self.assertIn("active == ACT_ACTIVATED && !win_is_headless()", WIN_CLIENT)
        self.assertIn("win_headless overrides r_fullscreen for capture", WIN_CLIENT)
        self.assertIn("if (r_hwgamma->integer && !win_is_headless())", WIN_CLIENT)

    def test_all_renderer_capture_launchers_select_the_explicit_no_window_mode(self) -> None:
        self.assertIn('"win_headless",\n        "1"', CAPTURE_MATRIX)
        self.assertIn('"win_headless", "1"', DEBUG_SMOKE)
        self.assertIn("CREATE_NO_WINDOW", CAPTURE_MATRIX)
        self.assertIn("CREATE_NO_WINDOW", DEBUG_SMOKE)


if __name__ == "__main__":
    unittest.main()
