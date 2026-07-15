#!/usr/bin/env python3
"""Regression contract for automation-safe client input initialization."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INPUT = (ROOT / "src/client/input.cpp").read_text(encoding="utf-8")


class HeadlessInputContractTests(unittest.TestCase):
    def test_headless_mode_disables_input_before_platform_mouse_setup(self) -> None:
        self.assertIn('Cvar_VariableInteger("win_headless") != 0', INPUT)
        init = INPUT[INPUT.index("void IN_Init(void)"):]
        self.assertIn("!in_enable->integer || IN_HeadlessAutomation()", init)
        self.assertLess(
            init.index("!in_enable->integer || IN_HeadlessAutomation()"),
            init.index("vid->init_mouse"),
        )

    def test_activation_fails_closed_when_headless_input_skips_grab_cvar_setup(self) -> None:
        grab = INPUT[INPUT.index("static bool IN_GetCurrentGrab(void)"):]
        self.assertIn("!in_enable || !in_enable->integer || !in_grab", grab)
        self.assertIn("IN_HeadlessAutomation()", grab)
        self.assertLess(
            grab.index("IN_HeadlessAutomation()"),
            grab.index("if (cls.key_dest & KEY_CONSOLE)"),
        )


if __name__ == "__main__":
    unittest.main()
