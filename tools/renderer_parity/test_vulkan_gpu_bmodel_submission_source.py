#!/usr/bin/env python3
"""Headless structural checks for immutable Vulkan inline-BSP submission."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
DEBUG = (ROOT / "src/rend_vk/vk_debug.c").read_text(encoding="utf-8")
SHADER = (
    ROOT / "src/rend_vk/shaders/vk_entity_gpu_bmodel.vert"
).read_text(encoding="utf-8")
SPV_GENERATOR = (ROOT / "tools/gen_vk_world_spv.py").read_text(encoding="utf-8")


class VulkanGpuBmodelSubmissionSourceTests(unittest.TestCase):
    def test_static_geometry_and_frame_instance_stream_are_native_vulkan(self) -> None:
        self.assertIn("vk_bmodel_gpu_vertex_t", ENTITY)
        self.assertIn("vk_bmodel_gpu_instance_t", ENTITY)
        self.assertIn("VK_Entity_EnsureBspGpuGeometry", ENTITY)
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", ENTITY)
        self.assertIn('"vkCreateBuffer(static BSP vertex)"', ENTITY)
        self.assertIn("VK_Entity_EnsureBmodelInstanceBuffer", ENTITY)
        self.assertIn("VK_Entity_BindGpuBmodelBatch", ENTITY)
        self.assertIn("VK_Entity_AppendGpuBmodelBatch", ENTITY)
        self.assertNotIn('#include "rend_gl', ENTITY)

    def test_static_mesh_keeps_per_entity_transform_and_normal_handling(self) -> None:
        self.assertIn("in_scaled_axis0", SHADER)
        self.assertIn("in_normal_axis0", SHADER)
        self.assertIn("world_pos = in_origin", SHADER)
        self.assertIn("world_normal = normalize", SHADER)
        self.assertIn("out_flags = in_face_flags | in_entity_flags", SHADER)
        self.assertIn("VK_Entity_TransformNormalWithTransform", ENTITY)
        self.assertIn("transform.inv_scale[axis]", ENTITY)

    def test_shader_is_embedded_and_gets_dedicated_pipelines(self) -> None:
        self.assertIn("vk_entity_gpu_bmodel.vert", SPV_GENERATOR)
        self.assertIn("vk_entity_gpu_bmodel_vert_spv", ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL", ENTITY)
        self.assertIn("pipeline_gpu_bmodel_opaque", ENTITY)
        self.assertIn("pipeline_gpu_bmodel_alpha", ENTITY)
        self.assertIn("gpu_bmodel_available", ENTITY)

    def test_special_bmodels_keep_cpu_expansion_fallback(self) -> None:
        self.assertIn("RF_SHELL_MASK | RF_OUTLINE | RF_RIMLIGHT | RF_ITEM_COLORIZE", ENTITY)
        self.assertIn("using CPU expansion", ENTITY)
        self.assertIn("VK_Entity_AddBspModel", ENTITY)

    def test_interval_stats_expose_the_entity_upload_domain(self) -> None:
        self.assertIn("entity_uploads=%llu", DEBUG)
        self.assertIn("VK_DEBUG_DOMAIN_ENTITY", DEBUG)

    def test_opaque_bmodel_instances_coalesce_without_reordering_blended_work(self) -> None:
        self.assertIn("VK_Entity_CoalesceGpuBmodelBatches", ENTITY)
        self.assertIn("VK_Entity_CanCoalesceGpuBmodelBatches", ENTITY)
        self.assertIn("Opaque, non-alpha-tested geometry is order-independent", ENTITY)
        self.assertIn("!(first->vertex_flags & VK_ENTITY_VERTEX_ALPHATEST)", ENTITY)
        self.assertIn("first->first_instance + first->instance_count == next->first_instance", ENTITY)
        self.assertIn("existing->instance_count++;", ENTITY)
        self.assertIn("vk_entity.bmodel_instance_count - batch->first_instance", ENTITY)


if __name__ == "__main__":
    unittest.main()
