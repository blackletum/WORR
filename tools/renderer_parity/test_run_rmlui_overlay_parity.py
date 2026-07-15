#!/usr/bin/env python3
"""Structural coverage for the paired guarded RmlUi parity runner."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNNER = (ROOT / "tools/renderer_parity/run_rmlui_overlay_parity.py").read_text(
    encoding="utf-8"
)
MANIFEST = (ROOT / "assets/renderer_parity/fr01_rmlui_overlay_manifest.json").read_text(
    encoding="utf-8"
)


class RmlUiOverlayParityRunnerTests(unittest.TestCase):
    def test_runner_uses_isolated_headless_captures_and_vulkan_validation(self) -> None:
        self.assertIn('for renderer in ("opengl", "vulkan")', RUNNER)
        self.assertIn('"leave_match_confirm",', RUNNER)
        self.assertIn('"forfeit_confirm",', RUNNER)
        self.assertIn('"quit_confirm": "rmlui-quit-confirm-popup"', RUNNER)
        self.assertIn('"leave_match_confirm": "rmlui-leave-match-confirm-popup"', RUNNER)
        self.assertIn('"forfeit_confirm": "rmlui-forfeit-confirm-popup"', RUNNER)
        self.assertIn('"--route-id", route_id', RUNNER)
        self.assertIn('def capture_name(route_id: str)', RUNNER)
        self.assertIn('CAPTURE_ATTEMPTS = 2', RUNNER)
        self.assertIn('time.sleep(CAPTURE_RETRY_DELAY_SECONDS)', RUNNER)
        self.assertIn('f"ATTEMPT {attempt + 1}/{len(results)}', RUNNER)
        self.assertIn('compare_command.extend(("--scene", ROUTE_SCENES[route_id]))', RUNNER)
        self.assertIn('environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"', RUNNER)
        self.assertIn('"--capture-root", str(args.capture_root)', RUNNER)
        self.assertIn("CREATE_NO_WINDOW", RUNNER)

    def test_manifest_locks_the_deterministic_runtime_overlay(self) -> None:
        self.assertIn('"capture": "rmlui_core_runtime_smoke.tga"', MANIFEST)
        self.assertIn('"capture": "rmlui_main.tga"', MANIFEST)
        self.assertIn('"capture": "rmlui_performance.tga"', MANIFEST)
        self.assertIn('"capture": "rmlui_quit_confirm.tga"', MANIFEST)
        self.assertIn('"capture": "rmlui_leave_match_confirm.tga"', MANIFEST)
        self.assertIn('"capture": "rmlui_forfeit_confirm.tga"', MANIFEST)
        self.assertIn('"crop": [0, 0, 960, 720]', MANIFEST)
        self.assertIn('"pixel_threshold": 0', MANIFEST)
        self.assertIn('"max_mean_absolute_rgb": [0, 0, 0]', MANIFEST)
        self.assertIn('"max_pixels_over_threshold_percent": 0', MANIFEST)


if __name__ == "__main__":
    unittest.main()
