#!/usr/bin/env python3
"""Headless structural checks for native Vulkan shadow-stream allocation."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")


class VulkanShadowStreamGrowthSourceTests(unittest.TestCase):
    def test_persistent_stream_uses_geometric_capacity(self) -> None:
        self.assertIn("VK_SHADOW_STREAM_BUFFER_MIN_BYTES", VK_SHADOW)
        self.assertIn("static bool VK_Shadow_GrowStreamBuffer", VK_SHADOW)
        self.assertIn(
            "size_t capacity = current ? current : VK_SHADOW_STREAM_BUFFER_MIN_BYTES;",
            VK_SHADOW,
        )
        self.assertIn("while (capacity < needed)", VK_SHADOW)
        self.assertIn("capacity > SIZE_MAX / 2", VK_SHADOW)
        self.assertIn("capacity *= 2;", VK_SHADOW)

    def test_gpu_buffer_uses_device_local_capacity_and_live_staging_uploads(self) -> None:
        self.assertIn(
            "vk_shadow_frame_resources_t frame_resources[VK_MAX_FRAMES_IN_FLIGHT]",
            VK_SHADOW,
        )
        self.assertIn("VK_Shadow_CurrentFrameResources", VK_SHADOW)
        self.assertIn(
            "VK_Shadow_GrowStreamBuffer(frame->vertex_buffer_bytes, bytes,",
            VK_SHADOW,
        )
        self.assertIn(".size = capacity,", VK_SHADOW)
        self.assertIn("frame->vertex_buffer_bytes = capacity;", VK_SHADOW)
        self.assertIn("VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |", VK_SHADOW)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_SHADOW)
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", VK_SHADOW)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_SRC_BIT", VK_SHADOW)
        self.assertIn("frame->vertex_staging_mapped", VK_SHADOW)
        self.assertIn(
            "memcpy(frame->vertex_staging_mapped, vk_shadow.vertices, bytes);",
            VK_SHADOW,
        )
        self.assertIn("void VK_Shadow_RecordUploads(VkCommandBuffer cmd)", VK_SHADOW)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->vertex_staging_buffer, frame->vertex_buffer,", VK_SHADOW)
        self.assertIn("VK_PIPELINE_STAGE_VERTEX_INPUT_BIT", VK_SHADOW)
        self.assertIn("VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_SHADOW, bytes);", VK_SHADOW)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_SHADOW)


if __name__ == "__main__":
    unittest.main()
