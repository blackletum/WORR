#!/usr/bin/env python3
"""Lock shared r_gamma material-upload parity to native Vulkan."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_TEXTURE = (ROOT / "src/rend_gl/texture.c").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
GAMMA_CONFIG = (
    ROOT / "assets/renderer_parity/fr01_world_texture_gamma.cfg"
).read_text(encoding="utf-8")
GAMMA_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_world_texture_gamma_manifest.json").read_text(
        encoding="utf-8"
    )
)


class SharedTextureGammaControlSourceTests(unittest.TestCase):
    def test_opengl_contract_is_wall_skin_upload_gamma_after_saturation(self) -> None:
        self.assertIn("static void GL_LightScaleTexture", GL_TEXTURE)
        self.assertIn("if (type == IT_WALL || type == IT_SKIN)", GL_TEXTURE)
        self.assertIn("gammaintensitytable[p[0]]", GL_TEXTURE)
        upload = GL_TEXTURE[GL_TEXTURE.index("static void GL_Upload32"):]
        self.assertLess(upload.index("GL_GrayScaleTexture"), upload.index("GL_LightScaleTexture"))

    def test_vulkan_registers_shared_gamma_and_preserves_the_platform_ramp_path(self) -> None:
        self.assertIn("static cvar_t *vk_r_gamma;", VK_UI)
        self.assertIn("static cvar_t *vk_vid_gamma_legacy;", VK_UI)
        self.assertIn("static void VK_UI_RegisterGammaCvars(void)", VK_UI)
        self.assertIn('Cvar_Get("r_gamma", "1", CVAR_ARCHIVE)', VK_UI)
        self.assertIn("CVAR_ARCHIVE | CVAR_NOARCHIVE", VK_UI)
        self.assertIn("VK_UI_GammaChanged", VK_UI)
        self.assertIn("VK_UI_UpdateHardwareGamma", VK_UI)
        self.assertIn("r_config.flags & QVF_GAMMARAMP", VK_UI)
        self.assertIn("vk_r_gamma->flags |= CVAR_FILES", VK_UI)
        self.assertIn("VK_UI_UnregisterGammaCvars();", VK_UI)

    def test_vulkan_matches_the_material_transform_and_upload_order(self) -> None:
        self.assertIn("static bool VK_UI_ApplyTextureGamma", VK_UI)
        self.assertIn("image->type != IT_WALL && image->type != IT_SKIN", VK_UI)
        self.assertIn("IF_NO_COLOR_ADJUST", VK_UI)
        self.assertIn("pow(normalized, gamma)", VK_UI)
        create_image = VK_UI[VK_UI.index("static qhandle_t VK_UI_CreateImage"):]
        self.assertLess(
            create_image.index("VK_UI_ApplyTextureSaturation"),
            create_image.index("VK_UI_ApplyTextureGamma"),
        )
        self.assertLess(
            create_image.index("VK_UI_ApplyTextureGamma"),
            create_image.index("VK_UI_ApplyPicmip"),
        )

    def test_headless_fixture_uses_nonidentity_software_gamma(self) -> None:
        self.assertIn("set r_hwgamma 0", GAMMA_CONFIG)
        self.assertIn("set r_gamma 1.3", GAMMA_CONFIG)
        scene = GAMMA_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "wall_material_texture_gamma")
        self.assertEqual(scene["capture"], "fr01_world_texture_gamma.tga")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 0)
        self.assertEqual(scene["probes"][0]["color"], [22, 22, 22])
        self.assertEqual(scene["probes"][0]["tolerance"], 0)
        self.assertEqual(scene["probes"][1]["color"], [155, 155, 155])
        self.assertEqual(scene["probes"][1]["tolerance"], 0)


if __name__ == "__main__":
    unittest.main()
