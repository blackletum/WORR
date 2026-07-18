#!/usr/bin/env python3
"""Lock shared texture intensity to the native Vulkan receiver path."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_TEXTURE = (ROOT / "src/rend_gl/texture.c").read_text(encoding="utf-8")
GL_SHADER = (ROOT / "src/rend_gl/shader.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
VK_WORLD_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_world_shadow.frag"
).read_text(encoding="utf-8")
VK_ENTITY_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_entity.frag"
).read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads(
    (ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8")
)
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(
    encoding="utf-8"
)
INTENSITY_CONFIG = (
    ROOT / "assets/renderer_parity/fr01_world_texture_intensity.cfg"
).read_text(encoding="utf-8")
INTENSITY_MANIFEST = json.loads(
    (
        ROOT
        / "assets/renderer_parity/fr01_world_texture_intensity_manifest.json"
    ).read_text(encoding="utf-8")
)


class SharedTextureIntensityControlSourceTests(unittest.TestCase):
    def test_video_routes_bind_the_canonical_shared_cvar(self) -> None:
        self.assertIn('pairs "texture intensity" r_intensity', LEGACY_MENU)
        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        intensity = next(
            item
            for item in video_menu["items"]
            if item.get("label") == "texture intensity"
        )
        self.assertEqual(intensity["cvar"], "r_intensity")
        self.assertIn('data-cvar="r_intensity"', RML_VIDEO)

    def test_opengl_keeps_intensity_as_a_synchronized_compatibility_alias(self) -> None:
        self.assertIn('gl_intensity = Cvar_Get("intensity", "1", 0);', GL_TEXTURE)
        self.assertIn('r_intensity = Cvar_Get("r_intensity",', GL_TEXTURE)
        self.assertIn("gl_intensity->flags | CVAR_ARCHIVE", GL_TEXTURE)
        self.assertIn("gl_sync_intensity_defaults", GL_TEXTURE)
        self.assertIn("gl_intensity_sync", GL_TEXTURE)
        self.assertIn("Cvar_ClampValue(gl_intensity, 1.0f, 5.0f)", GL_SHADER)

    def test_vulkan_applies_shared_intensity_natively_to_world_and_entities(self) -> None:
        self.assertIn("VK_World_RegisterIntensityCvars();", VK_WORLD)
        self.assertIn("VK_World_UnregisterIntensityCvars();", VK_WORLD)
        self.assertIn(
            'Cvar_Get("r_intensity", vk_intensity_legacy->string,\n'
            '                              CVAR_ARCHIVE)',
            VK_WORLD,
        )
        self.assertIn(
            "float value = Cvar_ClampValue(vk_r_intensity, 1.0f, 5.0f);",
            VK_WORLD,
        )
        self.assertIn("return value;", VK_WORLD)
        self.assertIn(
            "vk_shadow.uniform.moment_tuning[2] = VK_World_Intensity();",
            VK_SHADOW,
        )
        self.assertIn("VK_WORLD_VERTEX_INTENSITY", VK_WORLD_SHADER)
        self.assertIn("VK_ENTITY_VERTEX_INTENSITY", VK_ENTITY_SHADER)
        self.assertIn("texture_intensity = max(shadow_moment_tuning.z, 1.0)", VK_ENTITY_SHADER)

    def test_headless_fixture_uses_a_nondefault_canonical_value(self) -> None:
        self.assertIn("set r_intensity 2", INTENSITY_CONFIG)
        scene = INTENSITY_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "wall_material_texture_intensity")
        self.assertEqual(
            scene["config"],
            "renderer_parity/fr01_world_texture_intensity.cfg",
        )
        self.assertEqual(scene["capture"], "fr01_world_texture_intensity.tga")
        self.assertEqual(scene["probes"][0]["min_color"], [75, 75, 75])
        self.assertEqual(scene["probes"][0]["max_color"], [77, 77, 77])


if __name__ == "__main__":
    unittest.main()
