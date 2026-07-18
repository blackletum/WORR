#!/usr/bin/env python3
"""Lock native Vulkan lightmap saturation to the shared Video cvar."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
FIXTURE_GENERATOR = (ROOT / "tools/renderer_parity/generate_lightmap_saturation_fixture.py").read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads((ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8"))
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(encoding="utf-8")
CONFIG = (ROOT / "assets/renderer_parity/fr01_glowmap_lightmap_saturation.cfg").read_text(encoding="utf-8")
MANIFEST = json.loads((ROOT / "assets/renderer_parity/fr01_glowmap_lightmap_saturation_manifest.json").read_text(encoding="utf-8"))


class SharedLightmapSaturationControlSourceTests(unittest.TestCase):
    def test_video_routes_bind_shared_lightmap_saturation(self) -> None:
        self.assertIn('range "lightmap saturation" r_lightmap_saturation', LEGACY_MENU)
        video = next(menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video")
        control = next(item for item in video["items"] if item.get("label") == "lightmap saturation")
        self.assertEqual(control["cvar"], "r_lightmap_saturation")
        self.assertIn('data-cvar="r_lightmap_saturation"', RML_VIDEO)

    def test_opengl_keeps_legacy_alias_and_rebuilds_lightmaps(self) -> None:
        self.assertIn("cvar_t *r_lightmap_saturation;", GL_MAIN)
        self.assertIn("gl_lightmap_saturation_changed", GL_MAIN)
        self.assertIn("gl_sync_lightmap_saturation_defaults", GL_MAIN)
        self.assertIn("r_lightmap_saturation ? r_lightmap_saturation : gl_coloredlightmaps", GL_MAIN)
        self.assertIn("lm.dirty = true", GL_MAIN)

    def test_vulkan_rebuilds_native_atlas_pixels_on_change(self) -> None:
        self.assertIn("VK_World_RegisterLightmapSaturationCvars();", VK_WORLD)
        self.assertIn("VK_World_UnregisterLightmapSaturationCvars();", VK_WORLD)
        self.assertIn("VK_World_LightmapSaturationChanged", VK_WORLD)
        self.assertIn("vk_world.style_cache_valid = false;", VK_WORLD)
        self.assertIn("static float VK_World_LightmapSaturation", VK_WORLD)
        self.assertIn("const float luminance = LUMINANCE(light[0], light[1], light[2]);", VK_WORLD)
        start = VK_WORLD.index("static inline void VK_World_AdjustEntityLightColor")
        entity_adjust = VK_WORLD[start:start + 900]
        self.assertIn("VK_World_LightmapSaturation()", entity_adjust)
        self.assertIn("const float luminance = LUMINANCE", entity_adjust)

    def test_fixture_requests_full_desaturation(self) -> None:
        self.assertIn("set r_lightmap_saturation 0", CONFIG)
        self.assertIn("map worr_fr01_lightmap_saturation", CONFIG)
        self.assertEqual(MANIFEST["scenes"][0]["id"], "lightmapped_inline_bsp_saturation_zero")
        self.assertEqual(MANIFEST["scenes"][0]["crop"], [220, 150, 520, 420])
        probe = MANIFEST["scenes"][0]["probes"][0]
        self.assertEqual(probe["min_color"], [39, 184, 80])
        self.assertEqual(probe["max_color"], [42, 187, 82])
        self.assertIn("light_data_prefix_bytes=3", FIXTURE_GENERATOR)


if __name__ == "__main__":
    unittest.main()
