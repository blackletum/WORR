#!/usr/bin/env python3
"""Headless structural regression checks for FR-01-T11 glowmap parity."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
GL_IMAGES = (ROOT / "src/rend_gl/images.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
WORLD_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_world_shadow.frag"
).read_text(encoding="utf-8")
ENTITY_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_entity.frag"
).read_text(encoding="utf-8")


class VulkanGlowmapSourceTests(unittest.TestCase):
    def test_paired_glow_file_lookup_is_native_and_preserves_replacement_policy(self) -> None:
        self.assertIn('Cvar_Get("r_glowmaps", "1", CVAR_FILES)', VK_UI)
        self.assertIn('VK_UI_ReplaceExtension(glow_name, sizeof(glow_name), "_glow.pcx")', VK_UI)
        self.assertIn("static bool VK_UI_LoadGlowmapData", VK_UI)
        self.assertIn("VK_UI_LoadGlowmapData(glow_name", VK_UI)
        self.assertIn("return VK_UI_LoadRgbaFromFile(canonical_name", VK_UI)
        self.assertNotIn("VK_UI_LoadImageData(glow_name", VK_UI)
        self.assertIn("if (base->type == IT_SKIN)", VK_UI)
        self.assertIn("pixel[0] = (byte)(pixel[0] * alpha)", VK_UI)
        self.assertIn("base->glow_image = glow_handle", VK_UI)
        self.assertIn("vkDeviceWaitIdle(vk_ui.ctx->device);", VK_UI)
        self.assertIn("The image view and sampler bindings did not change", VK_UI)
        self.assertIn("if (image->internal_glowmap)", VK_UI)
        self.assertNotIn('#include "rend_gl', VK_UI)

    def test_opengl_companion_never_falls_through_to_a_base_wall(self) -> None:
        self.assertIn("static int load_glow_image_data", GL_IMAGES)
        self.assertIn("return try_image_format(IM_PCX, image, pic);", GL_IMAGES)
        self.assertIn("try_replace_ext(img_search[i], image, pic)", GL_IMAGES)
        self.assertNotIn("ret = load_image_data(&temporary, IM_PCX", GL_IMAGES)

    def test_world_material_descriptor_contains_the_native_glow_sampler(self) -> None:
        self.assertIn("VK_WORLD_VERTEX_GLOWMAP = BIT(6)", VK_WORLD)
        self.assertIn("VK_UI_HasGlowmap(texture_handle)", VK_WORLD)
        self.assertIn("VkDescriptorSetLayout set_layouts[4]", VK_WORLD)
        self.assertIn("VkDescriptorSetLayoutBinding bindings[3]", VK_UI)
        self.assertIn(".dstBinding = 1", VK_UI)
        self.assertIn(".dstBinding = 2", VK_UI)
        self.assertNotIn("set_layouts[5]", VK_WORLD)

    def test_entity_models_and_inline_bsp_faces_keep_their_own_glow_flag(self) -> None:
        self.assertIn("VK_ENTITY_VERTEX_GLOWMAP = BIT(10)", VK_ENTITY)
        self.assertIn("VK_UI_HasGlowmap(skin)", VK_ENTITY)
        self.assertIn("VK_UI_HasGlowmap(handle)", VK_ENTITY)
        self.assertIn("VkDescriptorSetLayout set_layouts[]", VK_ENTITY)
        self.assertIn("vk_entity.gpu_md5_set_layout", VK_ENTITY)
        self.assertNotIn("set_layouts[5]", VK_ENTITY)

    def test_shaders_preserve_wall_and_skin_contracts(self) -> None:
        self.assertIn("layout(set = 0, binding = 1) uniform sampler2D glow_sampler", WORLD_SHADER)
        self.assertIn("texture(glow_sampler, uv).a", WORLD_SHADER)
        self.assertIn("lm.rgb = mix(lm.rgb, vec3(1.0)", WORLD_SHADER)
        self.assertIn("layout(set = 0, binding = 1) uniform sampler2D glow_sampler", ENTITY_SHADER)
        self.assertIn("glow_emission = texture(glow_sampler, in_uv).rgb", ENTITY_SHADER)
        self.assertIn("out_color.rgb += glow_emission", ENTITY_SHADER)
        self.assertIn("lighting = mix(lighting, vec3(1.0)", ENTITY_SHADER)

    def test_intensity_is_runtime_uniform_data_not_a_texture_reload(self) -> None:
        self.assertIn('Cvar_Get("r_glowmap_intensity", "1", 0)', VK_WORLD)
        self.assertIn("Cvar_ClampValue(vk_r_glowmap_intensity, 0.0f, 10.0f)", VK_WORLD)
        self.assertIn("float glowmap_tuning[4]", VK_SHADOW)
        self.assertIn("VK_World_GlowmapIntensity()", VK_SHADOW)
        self.assertIn("shadow_glowmap_tuning.x", WORLD_SHADER)
        self.assertIn("shadow_glowmap_tuning.x", ENTITY_SHADER)


if __name__ == "__main__":
    unittest.main()
