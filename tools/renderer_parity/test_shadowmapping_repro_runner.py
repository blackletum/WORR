#!/usr/bin/env python3
"""Headless contract checks for the shadow smoke launcher."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNNER = (ROOT / "tools/shadowmapping_repro_smoke.py").read_text(encoding="utf-8")


class ShadowmappingReproRunnerTests(unittest.TestCase):
    def test_shadow_smoke_keeps_the_native_surface_hidden(self) -> None:
        self.assertIn('"win_headless", "1"', RUNNER)
        self.assertIn('"vid_renderer", renderer', RUNNER)
        self.assertIn('"s_enable", "0"', RUNNER)
        self.assertIn('"homedir", str(job_home(args, renderer, scene, filter_value))', RUNNER)
        self.assertIn('default=".tmp/shadowmapping-repro"', RUNNER)
        self.assertIn("CREATE_NO_WINDOW", RUNNER)
        self.assertIn('environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"', RUNNER)
        self.assertIn("capture_output=True", RUNNER)
        self.assertIn("def job_log", RUNNER)


if __name__ == "__main__":
    unittest.main()
