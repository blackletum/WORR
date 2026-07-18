#!/usr/bin/env python3
"""Lock native Vulkan material desaturation to the shared Video control."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_TEXTURE = (ROOT / "src/rend_gl/texture.c").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads(
    (ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8")
)
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(
    encoding="utf-8"
)
SATURATION_CONFIG = (
    ROOT / "assets/renderer_parity/fr01_world_texture_saturation.cfg"
).read_text(encoding="utf-8")
SATURATION_MANIFEST = json.loads(
    (
        ROOT
        / "assets/renderer_parity/fr01_world_texture_saturation_manifest.json"
    ).read_text(encoding="utf-8")
)


class SharedTextureSaturationControlSourceTests(unittest.TestCase):
    def test_video_routes_bind_shared_material_desaturation(self) -> None:
        self.assertIn(
            'range "texture saturation" r_texture_saturation', LEGACY_MENU
        )
        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        texture_saturation = next(
            item
            for item in video_menu["items"]
            if item.get("label") == "texture saturation"
        )
        self.assertEqual(texture_saturation["cvar"], "r_texture_saturation")
        self.assertIn(
            'data-cvar="r_texture_saturation"', RML_VIDEO
        )

    def test_opengl_keeps_legacy_alias_for_its_upload_transform(self) -> None:
        self.assertIn("static cvar_t *r_texture_saturation;", GL_TEXTURE)
        self.assertIn("gl_sync_texture_saturation_defaults", GL_TEXTURE)
        self.assertIn("gl_texture_saturation_changed", GL_TEXTURE)
        self.assertIn(
            'Cvar_Get("r_texture_saturation",', GL_TEXTURE
        )
        self.assertIn(
            "colorscale = Cvar_ClampValue(r_texture_saturation, 0, 1);",
            GL_TEXTURE,
        )

    def test_vulkan_applies_the_identical_wall_only_transform_before_picmip(self) -> None:
        self.assertIn("static bool VK_UI_ApplyTextureSaturation", VK_UI)
        self.assertIn("image->type != IT_WALL", VK_UI)
        self.assertIn("IF_TURBULENT | IF_NO_COLOR_ADJUST", VK_UI)
        self.assertIn("const float y = LUMINANCE(r, g, b);", VK_UI)
        self.assertIn("VK_UI_RegisterTextureSaturationCvars();", VK_UI)
        self.assertIn("VK_UI_UnregisterTextureSaturationCvars();", VK_UI)
        self.assertIn("VK_UI_ApplyTextureSaturation(image, width, height, rgba,", VK_UI)
        self.assertIn("VK_UI_ApplyPicmip(image, &width, &height, upload_rgba,", VK_UI)
        self.assertIn("base->flags | IF_TURBULENT", VK_UI)

    def test_headless_fixture_requests_full_material_desaturation(self) -> None:
        self.assertIn("set r_texture_saturation 0", SATURATION_CONFIG)
        scene = SATURATION_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "wall_material_texture_desaturation")
        self.assertEqual(
            scene["config"],
            "renderer_parity/fr01_world_texture_saturation.cfg",
        )
        self.assertEqual(scene["capture"], "fr01_world_texture_saturation.tga")
        self.assertEqual(scene["crop"], [200, 140, 560, 420])
        self.assertEqual(
            scene["probes"][0]["color"], [38, 38, 38]
        )
        self.assertEqual(
            scene["probes"][1]["color"], [174, 174, 174]
        )


if __name__ == "__main__":
    unittest.main()
