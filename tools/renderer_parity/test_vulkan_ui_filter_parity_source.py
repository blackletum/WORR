#!/usr/bin/env python3
"""Guard native Vulkan 2D filtering against the OpenGL picture contract."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UI_SOURCE = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
HUD_CONFIG = (ROOT / "assets/renderer_parity/fr01_hud_crosshair.cfg").read_text(
    encoding="utf-8"
)
HUD_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_hud_crosshair_manifest.json").read_text(
        encoding="utf-8"
    )
)
STATUSBAR_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_hud_statusbar_manifest.json").read_text(
        encoding="utf-8"
    )
)


class VulkanUiFilterParitySourceTests(unittest.TestCase):
    def test_native_filter_policy_has_matching_vk_cvars_and_samplers(self) -> None:
        self.assertIn('Cvar_Get("vk_bilerp_chars", "0", CVAR_ARCHIVE)', UI_SOURCE)
        self.assertIn('Cvar_Get("vk_bilerp_pics", "0", CVAR_ARCHIVE)', UI_SOURCE)
        self.assertIn('Cvar_Get("vk_bilerp_skies", "1", CVAR_ARCHIVE)', UI_SOURCE)
        self.assertIn("sampler_nearest_repeat", UI_SOURCE)
        self.assertIn("sampler_nearest_clamp", UI_SOURCE)
        self.assertIn("VK_FILTER_NEAREST", UI_SOURCE)
        self.assertIn("image->type == IT_FONT", UI_SOURCE)
        self.assertIn("image->type == IT_PIC", UI_SOURCE)
        self.assertIn("image->type == IT_SKY", UI_SOURCE)

    def test_filter_change_rebinds_native_descriptors_after_safe_idle(self) -> None:
        self.assertIn("VK_UI_BilerpChanged", UI_SOURCE)
        self.assertIn("vkDeviceWaitIdle(filter update)", UI_SOURCE)
        self.assertIn("VK_UI_UpdateDescriptorSet(image)", UI_SOURCE)

    def test_headless_hud_fixture_locks_crosshair_presence_and_exact_parity(self) -> None:
        self.assertIn("set cl_draw2d 1", HUD_CONFIG)
        self.assertIn("set cl_alpha 1", HUD_CONFIG)
        self.assertIn("set crosshair 3", HUD_CONFIG)
        self.assertIn("set cl_crosshair_pulse 0", HUD_CONFIG)
        scene = HUD_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "gameplay_hud_crosshair")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 0)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0, 0, 0])
        probe = scene["probes"][0]
        self.assertEqual(probe["color"], [255, 255, 255])
        self.assertEqual(probe["min_pixels_per_backend"], 12)
        self.assertEqual(probe["min_backend_intersection_over_union"], 1.0)

    def test_headless_hud_fixture_locks_classic_statusbar_data_and_exact_parity(self) -> None:
        scene = STATUSBAR_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "gameplay_hud_statusbar")
        self.assertEqual(scene["config"], "renderer_parity/fr01_hud_crosshair.cfg")
        self.assertEqual(scene["crop"], [280, 660, 400, 60])
        self.assertEqual(scene["metrics"]["pixel_threshold"], 0)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0, 0, 0])
        probe = scene["probes"][0]
        self.assertEqual(probe["color"], [0, 0, 0])
        self.assertEqual(probe["min_pixels_per_backend"], 200)
        self.assertEqual(probe["min_backend_intersection_over_union"], 1.0)


if __name__ == "__main__":
    unittest.main()
