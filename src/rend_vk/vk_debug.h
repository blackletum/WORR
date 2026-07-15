/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "vk_local.h"

typedef enum {
    VK_DEBUG_DOMAIN_WORLD,
    VK_DEBUG_DOMAIN_ENTITY,
    VK_DEBUG_DOMAIN_UI,
    VK_DEBUG_DOMAIN_SHADOW,
    VK_DEBUG_DOMAIN_DEBUG,
    VK_DEBUG_DOMAIN_COUNT,
} vk_debug_domain_t;

typedef enum {
    VK_DEBUG_GPU_PHASE_UPLOAD,
    VK_DEBUG_GPU_PHASE_SHADOW,
    VK_DEBUG_GPU_PHASE_SCENE,
    VK_DEBUG_GPU_PHASE_POSTPROCESS,
    VK_DEBUG_GPU_PHASE_COUNT,
} vk_debug_gpu_phase_t;

bool VK_Debug_Init(vk_context_t *ctx);
void VK_Debug_Shutdown(vk_context_t *ctx);
bool VK_Debug_CreateSwapchainResources(vk_context_t *ctx);
void VK_Debug_DestroySwapchainResources(vk_context_t *ctx);

void VK_Debug_BeginFrame(void);
void VK_Debug_EndFrame(float cpu_frame_ms);
void VK_Debug_BeginGpuFrame(VkCommandBuffer cmd, uint32_t frame_index);
void VK_Debug_MarkGpuPhase(VkCommandBuffer cmd, vk_debug_gpu_phase_t phase);
void VK_Debug_EndGpuFrame(VkCommandBuffer cmd);
void VK_Debug_MarkGpuFrameSubmitted(uint32_t frame_index);
void VK_Debug_ResolveGpuFrame(uint32_t frame_index);
void VK_Debug_SetRefdef(const refdef_t *fd);
void VK_Debug_Record(VkCommandBuffer cmd, const VkExtent2D *extent);
bool VK_Debug_Supported(void);
void VK_Debug_AddText(const vec3_t origin, const vec3_t angles,
                      const char *text, float size, color_t color,
                      uint32_t time, bool depth_test);

void VK_Debug_RecordDraw(vk_debug_domain_t domain, uint32_t vertices,
                         uint32_t indices);
void VK_Debug_RecordUpload(vk_debug_domain_t domain, size_t bytes);
void VK_Debug_RecordQuery(uint32_t count);
void VK_Debug_SetSceneCounts(uint32_t entities, uint32_t dlights,
                             uint32_t particles);
void VK_Debug_UpdateCapabilities(bool screenshot, bool stencil,
                                 bool depth_dof);
