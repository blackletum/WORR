#!/usr/bin/env python3
"""Headless structural checks for FR-01-T14 entity-stream allocation."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")


class VulkanEntityStreamGrowthSourceTests(unittest.TestCase):
    def test_persistent_stream_uses_geometric_capacity(self) -> None:
        self.assertIn("VK_ENTITY_STREAM_BUFFER_MIN_BYTES", VK_ENTITY)
        self.assertIn("static bool VK_Entity_GrowStreamBuffer", VK_ENTITY)
        self.assertIn("size_t capacity = current ? current : VK_ENTITY_STREAM_BUFFER_MIN_BYTES;", VK_ENTITY)
        self.assertIn("while (capacity < needed)", VK_ENTITY)
        self.assertIn("capacity > SIZE_MAX / 2", VK_ENTITY)
        self.assertIn("capacity *= 2;", VK_ENTITY)

    def test_device_local_gpu_stream_uses_capacity_and_live_staging_ranges(self) -> None:
        self.assertIn("vk_entity_frame_buffer_t frame_buffers[VK_MAX_FRAMES_IN_FLIGHT]", VK_ENTITY)
        self.assertIn("VK_Entity_CurrentFrameBuffer", VK_ENTITY)
        self.assertIn("VK_Entity_GrowStreamBuffer(frame->vertex_buffer_bytes, bytes,", VK_ENTITY)
        self.assertIn("VkBuffer vertex_staging_buffer;", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_ENTITY)
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_SRC_BIT", VK_ENTITY)
        self.assertIn("frame->vertex_buffer_bytes = capacity;", VK_ENTITY)
        self.assertIn("memcpy(frame->vertex_mapped, vk_entity.vertices, bytes);", VK_ENTITY)
        self.assertIn("frame->vertex_upload_bytes = bytes;", VK_ENTITY)
        self.assertIn("VK_Debug_RecordUpload(VK_DEBUG_DOMAIN_ENTITY, bytes);", VK_ENTITY)
        self.assertIn("void VK_Entity_RecordUploads(VkCommandBuffer cmd)", VK_ENTITY)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->vertex_staging_buffer, frame->vertex_buffer,", VK_ENTITY)
        self.assertIn("VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT", VK_ENTITY)
        self.assertIn("VK_ACCESS_INDEX_READ_BIT", VK_ENTITY)
        self.assertIn("VK_PIPELINE_STAGE_TRANSFER_BIT", VK_ENTITY)
        self.assertIn("VK_Entity_RecordUploads(cmd);", VK_MAIN)

    def test_standard_md2_and_md5_instances_use_a_compact_native_index_stream(self) -> None:
        self.assertIn("VkBuffer index_buffer;", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_INDEX_BUFFER_BIT", VK_ENTITY)
        self.assertIn("static bool VK_Entity_EnsureIndexBuffer", VK_ENTITY)
        self.assertIn("static bool VK_Entity_AppendIndexedBatch", VK_ENTITY)
        self.assertIn("uint16_t *indices;", VK_ENTITY)
        self.assertIn("vk_entity.indices[vk_entity.index_count++] = (uint16_t)i0;", VK_ENTITY)
        self.assertIn("vkCmdBindIndexBuffer(cmd, frame->index_buffer, 0, VK_INDEX_TYPE_UINT16);", VK_ENTITY)
        self.assertIn("(int32_t)batch->first_vertex, 0);", VK_ENTITY)
        self.assertIn("!(ent->flags & (RF_ITEM_COLORIZE | RF_OUTLINE))", VK_ENTITY)
        self.assertIn("Vulkan entity: indexed MD5 stream count overflow", VK_ENTITY)
        self.assertIn("memcpy(&vk_entity.vertices[first_vertex], vk_entity.temp_md5_vertices,", VK_ENTITY)
        self.assertIn("uint16_t *index = &vk_entity.indices[first_index + i];", VK_ENTITY)
        self.assertIn("Vulkan entity: 16-bit indexed batch overflow", VK_ENTITY)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_ENTITY)


if __name__ == "__main__":
    unittest.main()
