#!/usr/bin/env python3
"""Lock native Vulkan lightmap-brightness control to the shared Video cvar."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
GL_SHADER = (ROOT / "src/rend_gl/shader.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads(
    (ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8")
)
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(
    encoding="utf-8"
)
BRIGHTNESS_CONFIG = (
    ROOT / "assets/renderer_parity/fr01_glowmap_lightmap_brightness.cfg"
).read_text(encoding="utf-8")
BRIGHTNESS_MANIFEST = json.loads(
    (
        ROOT
        / "assets/renderer_parity/fr01_glowmap_lightmap_brightness_manifest.json"
    ).read_text(encoding="utf-8")
)


class SharedLightmapBrightnessControlSourceTests(unittest.TestCase):
    def test_video_routes_bind_shared_lightmap_brightness(self) -> None:
        self.assertIn(
            'range "lightmap brightness" r_lightmap_brightness', LEGACY_MENU
        )
        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        brightness = next(
            item
            for item in video_menu["items"]
            if item.get("label") == "lightmap brightness"
        )
        self.assertEqual(brightness["cvar"], "r_lightmap_brightness")
        self.assertIn('data-cvar="r_lightmap_brightness"', RML_VIDEO)

    def test_opengl_keeps_gl_brightness_as_a_synchronized_alias(self) -> None:
        self.assertIn("cvar_t *r_lightmap_brightness;", GL_MAIN)
        self.assertIn("gl_sync_lightmap_brightness_defaults", GL_MAIN)
        self.assertIn("gl_lightmap_brightness_changed", GL_MAIN)
        self.assertIn('Cvar_Get("r_lightmap_brightness",', GL_MAIN)
        self.assertIn("self == r_lightmap_brightness", GL_MAIN)
        self.assertIn("r_lightmap_brightness ? r_lightmap_brightness : gl_brightness", GL_SHADER)

    def test_vulkan_uses_the_shared_value_in_native_receiver_uniforms(self) -> None:
        self.assertIn("static cvar_t *vk_r_lightmap_brightness;", VK_WORLD)
        self.assertIn("VK_World_RegisterLightmapBrightnessCvars();", VK_WORLD)
        self.assertIn("VK_World_UnregisterLightmapBrightnessCvars();", VK_WORLD)
        self.assertIn("VK_World_LightmapBrightnessChanged", VK_WORLD)
        self.assertIn(
            "return Cvar_ClampValue(vk_r_lightmap_brightness, -1, 1);",
            VK_WORLD,
        )

    def test_headless_fixture_uses_a_nonzero_shared_lightmap_add(self) -> None:
        self.assertIn("set r_lightmap_brightness 0.2", BRIGHTNESS_CONFIG)
        scene = BRIGHTNESS_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "wall_lightmap_brightness_add")
        self.assertEqual(
            scene["config"],
            "renderer_parity/fr01_glowmap_lightmap_brightness.cfg",
        )
        self.assertEqual(scene["metrics"]["pixel_threshold"], 0)
        self.assertEqual(scene["probes"][0]["color"], [58, 96, 173])
        self.assertEqual(scene["probes"][0]["min_pixels_per_backend"], 50000)


if __name__ == "__main__":
    unittest.main()
