#!/usr/bin/env python3
"""Headless structural checks for the native Vulkan bloom baseline."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_POST = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
POST_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_postprocess.frag"
).read_text(encoding="utf-8")
BLOOM_SHADER = (ROOT / "src/rend_vk/shaders/vk_bloom.frag").read_text(
    encoding="utf-8"
)


class VulkanBloomSourceTests(unittest.TestCase):
    def test_native_controls_cover_opengl_baseline_parameters(self) -> None:
        for name in (
            "vk_bloom",
            "vk_bloom_iterations",
            "vk_bloom_downscale",
            "vk_bloom_firefly",
            "vk_bloom_sigma",
            "vk_bloom_threshold",
            "vk_bloom_knee",
            "vk_bloom_intensity",
            "vk_bloom_saturation",
            "vk_bloom_scene_saturation",
        ):
            self.assertIn(f'Cvar_Get("{name}"', VK_POST)

    def test_bloom_uses_native_downsampled_ping_pong_images(self) -> None:
        self.assertIn("VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |", VK_POST)
        self.assertIn("VK_IMAGE_USAGE_SAMPLED_BIT", VK_POST)
        self.assertIn("VK_PostProcess_CreateBloomImage", VK_POST)
        self.assertIn("VK_PostProcess_RecordBloomPass", VK_POST)
        self.assertIn("VK_BLOOM_MODE_PREFILTER", VK_POST)
        self.assertIn("VK_BLOOM_MODE_BLUR_X", VK_POST)
        self.assertIn("VK_BLOOM_MODE_BLUR_Y", VK_POST)
        self.assertNotIn('#include "rend_gl', VK_POST)

    def test_shaders_keep_prefilter_blur_and_composite_separate(self) -> None:
        self.assertIn("soft threshold/knee extraction", BLOOM_SHADER)
        self.assertIn("Gaussian kernel", BLOOM_SHADER)
        self.assertIn("for (int i = -50; i <= 50; i++)", BLOOM_SHADER)
        self.assertIn("binding = 2", POST_SHADER)
        self.assertIn("bloom_final", POST_SHADER)
        self.assertLess(
            POST_SHADER.index("vec4 color = texture(scene_sampler, tc)"),
            POST_SHADER.index("if (push_data.color_enabled > 0.5)"),
        )

    def test_bloom_records_between_scene_copy_and_final_composite(self) -> None:
        final_postprocess = VK_MAIN.index("if (final_postprocess) {")
        scene_copy = VK_MAIN.index("VK_SceneCopy_Record(cmd, image_index)",
                                   final_postprocess)
        bloom = VK_MAIN.index("VK_PostProcess_RecordBloom(cmd)",
                              final_postprocess)
        final = VK_MAIN.index("VK_PostProcess_RecordFinal(",
                              final_postprocess)
        self.assertLess(scene_copy, bloom)
        self.assertLess(bloom, final)

    def test_external_descriptors_supply_scene_lut_and_bloom_bindings(self) -> None:
        self.assertIn("VK_UI_CreateExternalImageTripleDescriptor", VK_UI)
        self.assertIn(".dstBinding = 2", VK_UI)
        self.assertIn("VkDescriptorSetLayoutBinding bindings[3]", VK_UI)
        self.assertIn("VK_UI_CreateExternalImageTripleDescriptor", VK_POST)


if __name__ == "__main__":
    unittest.main()
