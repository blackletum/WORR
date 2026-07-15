/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "vk_local.h"

bool VK_PostProcess_Init(vk_context_t *ctx);
void VK_PostProcess_Shutdown(vk_context_t *ctx);
bool VK_PostProcess_CreateSwapchainResources(vk_context_t *ctx);
void VK_PostProcess_DestroySwapchainResources(vk_context_t *ctx);

void VK_PostProcess_RenderFrame(const refdef_t *fd);
// Returns true only when preparing the next frame would replace all frame
// slots' bloom targets. Callers must retire submitted frames first.
bool VK_PostProcess_NeedsSafeResourceUpdate(void);
void VK_PostProcess_PrepareFrame(void);
bool VK_PostProcess_UsesFinalPass(void);
bool VK_PostProcess_UsesCompositePass(void);
bool VK_PostProcess_UsesCrtPass(void);
bool VK_PostProcess_UsesDof(void);
void VK_PostProcess_RecordBloom(VkCommandBuffer cmd);
void VK_PostProcess_RecordDof(VkCommandBuffer cmd);
void VK_PostProcess_RecordFinal(VkCommandBuffer cmd, const VkExtent2D *extent,
                                VkDescriptorSet scene_descriptor_set);
void VK_PostProcess_RecordCrt(VkCommandBuffer cmd, const VkExtent2D *extent,
                              VkDescriptorSet scene_descriptor_set);
