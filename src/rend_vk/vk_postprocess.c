/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "vk_postprocess.h"

#include "vk_bloom_spv.h"
#include "vk_crt_spv.h"
#include "vk_dof_spv.h"
#include "vk_postprocess_spv.h"
#include "vk_ui.h"
#include "vk_world.h"

#include "renderer/view_setup.h"

#include <math.h>
#include <string.h>

typedef struct {
    float time;
    float waterwarp;
    float color_enabled;
    float brightness;
    float contrast;
    float saturation;
    float output_size[2];
    float tint[4];
    float split_params[4];
    float split_shadow[4];
    float split_highlight[4];
    float lut_params[4];
    float bloom_final[4];
} vk_postprocess_push_t;

typedef struct {
    float output_size[4];
    float params[4];
    float aux[4];
} vk_postprocess_bloom_push_t;

typedef struct {
    float params[4];
    float params2[4];
    float texel[4];
} vk_postprocess_crt_push_t;

typedef struct {
    float params[4];
    float projection[4];
    float rect[4];
} vk_postprocess_dof_push_t;

_Static_assert(sizeof(vk_postprocess_push_t) == 128,
               "final post-process push constants must match vk_postprocess.frag");
_Static_assert(sizeof(vk_postprocess_bloom_push_t) == 48,
               "bloom push constants must match vk_bloom.frag");
_Static_assert(sizeof(vk_postprocess_crt_push_t) == 48,
               "CRT push constants must match vk_crt.frag");
_Static_assert(sizeof(vk_postprocess_dof_push_t) == 48,
               "DOF push constants must match vk_dof.frag");

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFramebuffer framebuffer;
    bool initialized;
} vk_postprocess_bloom_image_t;

typedef struct {
    vk_postprocess_bloom_image_t bloom_ping;
    vk_postprocess_bloom_image_t bloom_pong;
    vk_postprocess_bloom_image_t dof_ping;
    vk_postprocess_bloom_image_t dof_pong;
    vk_postprocess_bloom_image_t dof_scene;
    VkDescriptorSet final_scene_descriptor_set;
    VkDescriptorSet bloom_scene_descriptor_set;
    VkDescriptorSet bloom_ping_descriptor_set;
    VkDescriptorSet bloom_pong_descriptor_set;
    VkDescriptorSet dof_scene_descriptor_set;
    VkDescriptorSet dof_ping_descriptor_set;
    VkDescriptorSet dof_pong_descriptor_set;
    VkDescriptorSet dof_composite_descriptor_set;
    bool descriptor_lut_active;
    bool descriptor_bloom_active;
    bool descriptor_dof_active;
    uint32_t descriptor_generation;
} vk_postprocess_frame_resources_t;

enum {
    VK_BLOOM_MODE_COPY,
    VK_BLOOM_MODE_PREFILTER,
    VK_BLOOM_MODE_BLUR_X,
    VK_BLOOM_MODE_BLUR_Y,
};

typedef struct {
    vk_context_t *ctx;
    bool initialized;
    bool swapchain_ready;
    VkPipelineLayout pipeline_layout;
    VkPipeline final_pipeline;
    VkPipeline bloom_pipeline;
    VkPipeline dof_pipeline;
    VkPipeline crt_pipeline;
    VkRenderPass bloom_render_pass;
    cvar_t *waterwarp;
    cvar_t *color_correction;
    cvar_t *color_brightness;
    cvar_t *color_contrast;
    cvar_t *color_saturation;
    cvar_t *color_tint;
    cvar_t *color_split_shadows;
    cvar_t *color_split_highlights;
    cvar_t *color_split_strength;
    cvar_t *color_split_balance;
    cvar_t *color_lut;
    cvar_t *color_lut_intensity;
    cvar_t *bloom;
    cvar_t *bloom_iterations;
    cvar_t *bloom_downscale;
    cvar_t *bloom_firefly;
    cvar_t *bloom_sigma;
    cvar_t *bloom_threshold;
    cvar_t *bloom_knee;
    cvar_t *bloom_intensity;
    cvar_t *bloom_saturation;
    cvar_t *bloom_scene_saturation;
    cvar_t *crt_mode;
    cvar_t *crt_bright_boost;
    cvar_t *crt_hard_pix;
    cvar_t *crt_hard_scan;
    cvar_t *crt_mask_dark;
    cvar_t *crt_mask_light;
    cvar_t *crt_scale_in_linear_gamma;
    cvar_t *crt_shadow_mask;
    cvar_t *dof;
    cvar_t *dof_focus_distance;
    cvar_t *dof_blur_range;
    bool waterwarp_active;
    bool color_active;
    bool split_active;
    bool lut_active;
    bool bloom_requested;
    bool bloom_active;
    bool dof_requested;
    bool dof_active;
    bool crt_active;
    bool bloom_resources_dirty;
    bool bloom_supported;
    bool dof_resources_dirty;
    bool dof_supported;
    bool lut_valid;
    uint32_t descriptor_generation;
    qhandle_t lut_image;
    int lut_width;
    int lut_height;
    float lut_size;
    uint32_t bloom_width;
    uint32_t bloom_height;
    uint32_t dof_width;
    uint32_t dof_height;
    vk_postprocess_frame_resources_t frame_resources[VK_MAX_FRAMES_IN_FLIGHT];
    float tint[4];
    float split_shadow[4];
    float split_highlight[4];
    vk_postprocess_push_t push;
    vk_postprocess_bloom_push_t bloom_push;
    vk_postprocess_crt_push_t crt_push;
    vk_postprocess_dof_push_t dof_push;
} vk_postprocess_state_t;

static vk_postprocess_state_t vk_postprocess;

static vk_postprocess_frame_resources_t *VK_PostProcess_CurrentFrameResources(void)
{
    if (!vk_postprocess.ctx || !vk_postprocess.ctx->frame_count ||
        vk_postprocess.ctx->current_frame >= vk_postprocess.ctx->frame_count) {
        return NULL;
    }
    return &vk_postprocess.frame_resources[vk_postprocess.ctx->current_frame];
}

static VkImageView VK_PostProcess_CurrentSceneView(void)
{
    if (!vk_postprocess.ctx || !vk_postprocess.ctx->frame_count ||
        vk_postprocess.ctx->current_frame >= vk_postprocess.ctx->frame_count) {
        return VK_NULL_HANDLE;
    }
    return vk_postprocess.ctx->frames[vk_postprocess.ctx->current_frame]
        .liquid_scene_view;
}

static VkImageView VK_PostProcess_CurrentDepthView(void)
{
    if (!vk_postprocess.ctx || !vk_postprocess.ctx->frame_count ||
        vk_postprocess.ctx->current_frame >= vk_postprocess.ctx->frame_count) {
        return VK_NULL_HANDLE;
    }
    return vk_postprocess.ctx->frames[vk_postprocess.ctx->current_frame]
        .depth_sample_view;
}

static bool VK_PostProcess_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }
    Com_SetLastError(va("Vulkan post-process %s failed: %d", what, (int)result));
    return false;
}

static void VK_PostProcess_ParseColor(cvar_t *self, float output[4])
{
    color_t color;

    if (!SCR_ParseColor(self->string, &color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        color.u32 = COLOR_U32_WHITE;
    }

    output[0] = color.u8[0] / 255.0f;
    output[1] = color.u8[1] / 255.0f;
    output[2] = color.u8[2] / 255.0f;
    output[3] = 1.0f;
}

static void VK_PostProcess_ColorTintChanged(cvar_t *self)
{
    VK_PostProcess_ParseColor(self, vk_postprocess.tint);
}

static void VK_PostProcess_ColorSplitShadowsChanged(cvar_t *self)
{
    VK_PostProcess_ParseColor(self, vk_postprocess.split_shadow);
}

static void VK_PostProcess_ColorSplitHighlightsChanged(cvar_t *self)
{
    VK_PostProcess_ParseColor(self, vk_postprocess.split_highlight);
}

static void VK_PostProcess_DestroyExternalDescriptors(
    vk_postprocess_frame_resources_t *frame)
{
    if (!frame) {
        return;
    }
    VK_UI_DestroyExternalImageDescriptor(&frame->final_scene_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->bloom_scene_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->bloom_ping_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->bloom_pong_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->dof_scene_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->dof_ping_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->dof_pong_descriptor_set);
    VK_UI_DestroyExternalImageDescriptor(&frame->dof_composite_descriptor_set);
    frame->descriptor_lut_active = false;
    frame->descriptor_bloom_active = false;
    frame->descriptor_dof_active = false;
    frame->descriptor_generation = 0;
}

static void VK_PostProcess_DestroyAllExternalDescriptors(void)
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_PostProcess_DestroyExternalDescriptors(&vk_postprocess.frame_resources[i]);
    }
}

static void VK_PostProcess_ResetColorLut(void)
{
    vk_postprocess.lut_image = 0;
    vk_postprocess.lut_width = 0;
    vk_postprocess.lut_height = 0;
    vk_postprocess.lut_size = 0.0f;
    vk_postprocess.lut_valid = false;
    vk_postprocess.descriptor_generation++;
    if (!vk_postprocess.descriptor_generation) {
        vk_postprocess.descriptor_generation = 1;
    }
}

static void VK_PostProcess_ColorLutChanged(cvar_t *self)
{
    VK_PostProcess_ResetColorLut();
    if (!self || !self->string[0]) {
        return;
    }

    qhandle_t image = VK_UI_RegisterImage(
        self->string, IT_PIC, IF_PERMANENT | IF_EXACT | IF_NO_COLOR_ADJUST);
    if (!image) {
        Com_WPrintf("Vulkan color LUT '%s' could not be loaded\n", self->string);
        return;
    }

    int width = 0;
    int height = 0;
    if (!VK_UI_GetPicSize(&width, &height, image) || width <= 0 || height <= 0) {
        Com_WPrintf("Vulkan color LUT '%s' has invalid dimensions\n", self->string);
        return;
    }

    int size = 0;
    if (width == height * height) {
        size = height;
    } else if (height == width * width) {
        size = width;
    }
    if (size <= 0) {
        Com_WPrintf("Vulkan color LUT '%s' expects NxN strip (got %dx%d)\n",
                    self->string, width, height);
        return;
    }

    vk_postprocess.lut_image = image;
    vk_postprocess.lut_width = width;
    vk_postprocess.lut_height = height;
    vk_postprocess.lut_size = (float)size;
    vk_postprocess.lut_valid = true;
}

static void VK_PostProcess_UpdateExternalDescriptors(void)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!frame) {
        return;
    }
    if (frame->descriptor_generation == vk_postprocess.descriptor_generation &&
        frame->descriptor_lut_active == vk_postprocess.lut_active &&
        frame->descriptor_bloom_active == vk_postprocess.bloom_active &&
        frame->descriptor_dof_active == vk_postprocess.dof_active) {
        return;
    }

    VK_PostProcess_DestroyExternalDescriptors(frame);
    VkImageView scene_view = VK_PostProcess_CurrentSceneView();
    if (!scene_view) {
        return;
    }

    if (vk_postprocess.dof_active) {
        VkImageView depth_view = VK_PostProcess_CurrentDepthView();
        if (!depth_view || !frame->dof_ping.view || !frame->dof_pong.view ||
            !frame->dof_scene.view) {
            Com_WPrintf("Vulkan depth-aware DOF resources are unavailable\n");
            vk_postprocess.dof_active = false;
        } else {
            frame->dof_scene_descriptor_set =
                VK_UI_CreateExternalImageDescriptor(
                    scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            frame->dof_ping_descriptor_set =
                VK_UI_CreateExternalImageDescriptor(
                    frame->dof_ping.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            frame->dof_pong_descriptor_set =
                VK_UI_CreateExternalImageDescriptor(
                    frame->dof_pong.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            frame->dof_composite_descriptor_set =
                VK_UI_CreateExternalImageTripleDescriptor(
                    scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    frame->dof_ping.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    depth_view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            if (!frame->dof_scene_descriptor_set || !frame->dof_ping_descriptor_set ||
                !frame->dof_pong_descriptor_set || !frame->dof_composite_descriptor_set) {
                Com_WPrintf("Vulkan depth-aware DOF descriptors could not be allocated\n");
                VK_PostProcess_DestroyExternalDescriptors(frame);
                vk_postprocess.dof_active = false;
            }
        }
    }

    VkImageView source_scene_view = vk_postprocess.dof_active
        ? frame->dof_scene.view : scene_view;
    VkImageView lut_view = source_scene_view;
    if (vk_postprocess.lut_active) {
        lut_view = VK_UI_GetImageView(vk_postprocess.lut_image);
        if (!lut_view) {
            Com_WPrintf("Vulkan color LUT image is unavailable\n");
            vk_postprocess.lut_valid = false;
            vk_postprocess.lut_active = false;
            lut_view = source_scene_view;
        }
    }

    VkImageView bloom_view = vk_postprocess.bloom_active && frame->bloom_ping.view
        ? frame->bloom_ping.view : source_scene_view;
    if (vk_postprocess.lut_active || vk_postprocess.bloom_active ||
        vk_postprocess.dof_active) {
        frame->final_scene_descriptor_set =
            VK_UI_CreateExternalImageTripleDescriptor(
                source_scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                lut_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                bloom_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->final_scene_descriptor_set) {
            Com_WPrintf("Vulkan post-process final descriptor could not be allocated\n");
            vk_postprocess.lut_active = false;
            vk_postprocess.bloom_active = false;
            vk_postprocess.dof_active = false;
            vk_postprocess.push.lut_params[0] = 0.0f;
            vk_postprocess.push.bloom_final[0] = 0.0f;
            vk_postprocess.push.bloom_final[1] = 1.0f;
            vk_postprocess.push.bloom_final[2] = 1.0f;
            vk_postprocess.push.bloom_final[3] = 0.0f;
            return;
        }
    }

    if (vk_postprocess.bloom_active) {
        frame->bloom_scene_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frame->bloom_ping_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                frame->bloom_ping.view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frame->bloom_pong_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                frame->bloom_pong.view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->bloom_scene_descriptor_set ||
            !frame->bloom_ping_descriptor_set ||
            !frame->bloom_pong_descriptor_set) {
            Com_WPrintf("Vulkan bloom descriptors could not be allocated\n");
            VK_PostProcess_DestroyExternalDescriptors(frame);
            vk_postprocess.bloom_active = false;
            vk_postprocess.lut_active = false;
            vk_postprocess.push.lut_params[0] = 0.0f;
            vk_postprocess.push.bloom_final[0] = 0.0f;
            vk_postprocess.push.bloom_final[1] = 1.0f;
            vk_postprocess.push.bloom_final[2] = 1.0f;
            vk_postprocess.push.bloom_final[3] = 0.0f;
        }
    }

    frame->descriptor_lut_active = vk_postprocess.lut_active;
    frame->descriptor_bloom_active = vk_postprocess.bloom_active;
    frame->descriptor_dof_active = vk_postprocess.dof_active;
    frame->descriptor_generation = vk_postprocess.descriptor_generation;
}

static uint32_t VK_PostProcess_FindMemoryType(VkPhysicalDevice physical_device,
                                               uint32_t type_filter,
                                               VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((type_filter & BIT(i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static void VK_PostProcess_DestroyBloomImage(vk_postprocess_bloom_image_t *image)
{
    if (!image || !vk_postprocess.ctx || !vk_postprocess.ctx->device) {
        return;
    }

    VkDevice device = vk_postprocess.ctx->device;
    if (image->framebuffer) {
        vkDestroyFramebuffer(device, image->framebuffer, NULL);
        image->framebuffer = VK_NULL_HANDLE;
    }
    if (image->view) {
        vkDestroyImageView(device, image->view, NULL);
        image->view = VK_NULL_HANDLE;
    }
    if (image->image) {
        vkDestroyImage(device, image->image, NULL);
        image->image = VK_NULL_HANDLE;
    }
    if (image->memory) {
        vkFreeMemory(device, image->memory, NULL);
        image->memory = VK_NULL_HANDLE;
    }
    image->initialized = false;
}

static void VK_PostProcess_DestroyBloomImages(void)
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        VK_PostProcess_DestroyBloomImage(&frame->bloom_ping);
        VK_PostProcess_DestroyBloomImage(&frame->bloom_pong);
    }
    vk_postprocess.bloom_width = 0;
    vk_postprocess.bloom_height = 0;
    vk_postprocess.bloom_supported = false;
}

static void VK_PostProcess_DestroyDofImages(void)
{
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        VK_PostProcess_DestroyBloomImage(&frame->dof_ping);
        VK_PostProcess_DestroyBloomImage(&frame->dof_pong);
        VK_PostProcess_DestroyBloomImage(&frame->dof_scene);
    }
    vk_postprocess.dof_width = 0;
    vk_postprocess.dof_height = 0;
    vk_postprocess.dof_supported = false;
}

static bool VK_PostProcess_CreateBloomImage(uint32_t width, uint32_t height,
                                            vk_postprocess_bloom_image_t *image)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device || !image || !vk_postprocess.bloom_render_pass ||
        !width || !height) {
        return false;
    }

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = ctx->swapchain.format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!VK_PostProcess_Check(vkCreateImage(ctx->device, &image_info, NULL,
                                            &image->image),
                              "vkCreateImage(bloom)")) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(ctx->device, image->image, &requirements);
    uint32_t memory_type = VK_PostProcess_FindMemoryType(
        ctx->physical_device, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("Vulkan bloom: suitable image memory type not found");
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (!VK_PostProcess_Check(vkAllocateMemory(ctx->device, &alloc_info, NULL,
                                                &image->memory),
                              "vkAllocateMemory(bloom)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }
    if (!VK_PostProcess_Check(vkBindImageMemory(ctx->device, image->image,
                                                 image->memory, 0),
                              "vkBindImageMemory(bloom)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = ctx->swapchain.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (!VK_PostProcess_Check(vkCreateImageView(ctx->device, &view_info, NULL,
                                                &image->view),
                              "vkCreateImageView(bloom)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }

    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = vk_postprocess.bloom_render_pass,
        .attachmentCount = 1,
        .pAttachments = &image->view,
        .width = width,
        .height = height,
        .layers = 1,
    };
    if (!VK_PostProcess_Check(vkCreateFramebuffer(ctx->device,
                                                   &framebuffer_info, NULL,
                                                   &image->framebuffer),
                              "vkCreateFramebuffer(bloom)")) {
        VK_PostProcess_DestroyBloomImage(image);
        return false;
    }
    return true;
}

static bool VK_PostProcess_CreateBloomImages(uint32_t width, uint32_t height)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device || !width || !height) {
        return false;
    }

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(ctx->physical_device,
                                        ctx->swapchain.format,
                                        &format_properties);
    const VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((format_properties.optimalTilingFeatures & required) != required) {
        Com_WPrintf("Vulkan bloom: swapchain format lacks sampled colour attachment support\n");
        return false;
    }

    VK_PostProcess_DestroyBloomImages();
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        if (!VK_PostProcess_CreateBloomImage(width, height, &frame->bloom_ping) ||
            !VK_PostProcess_CreateBloomImage(width, height, &frame->bloom_pong)) {
            VK_PostProcess_DestroyBloomImages();
            return false;
        }
    }
    if (!ctx->frame_count) {
        VK_PostProcess_DestroyBloomImages();
        return false;
    }

    vk_postprocess.bloom_width = width;
    vk_postprocess.bloom_height = height;
    vk_postprocess.bloom_supported = true;
    return true;
}

static bool VK_PostProcess_CreateDofImages(uint32_t width, uint32_t height)
{
    vk_context_t *ctx = vk_postprocess.ctx;
    if (!ctx || !ctx->device || !width || !height ||
        !ctx->swapchain.extent.width || !ctx->swapchain.extent.height) {
        return false;
    }

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(ctx->physical_device,
                                        ctx->swapchain.format,
                                        &format_properties);
    const VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((format_properties.optimalTilingFeatures & required) != required) {
        Com_WPrintf("Vulkan depth-aware DOF: swapchain format lacks sampled colour attachment support\n");
        return false;
    }

    VK_PostProcess_DestroyDofImages();
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_postprocess_frame_resources_t *frame =
            &vk_postprocess.frame_resources[i];
        if (!VK_PostProcess_CreateBloomImage(width, height, &frame->dof_ping) ||
            !VK_PostProcess_CreateBloomImage(width, height, &frame->dof_pong) ||
            !VK_PostProcess_CreateBloomImage(ctx->swapchain.extent.width,
                                              ctx->swapchain.extent.height,
                                              &frame->dof_scene)) {
            VK_PostProcess_DestroyDofImages();
            return false;
        }
    }

    vk_postprocess.dof_width = width;
    vk_postprocess.dof_height = height;
    vk_postprocess.dof_supported = true;
    return true;
}

static bool VK_PostProcess_CreateBloomRenderPass(vk_context_t *ctx)
{
    VkAttachmentDescription color_attachment = {
        .format = ctx->swapchain.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference color_reference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_reference,
    };
    VkSubpassDependency dependencies[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        },
    };
    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = q_countof(dependencies),
        .pDependencies = dependencies,
    };
    return VK_PostProcess_Check(vkCreateRenderPass(ctx->device,
                                                    &render_pass_info, NULL,
                                                    &vk_postprocess.bloom_render_pass),
                                "vkCreateRenderPass(bloom)");
}

bool VK_PostProcess_Init(vk_context_t *ctx)
{
    memset(&vk_postprocess, 0, sizeof(vk_postprocess));
    if (!ctx) {
        Com_SetLastError("Vulkan post-process: context is missing");
        return false;
    }

    vk_postprocess.ctx = ctx;
    vk_postprocess.waterwarp = Cvar_Get("vk_waterwarp", "1", 0);
    vk_postprocess.color_correction = Cvar_Get("vk_color_correction", "1", CVAR_ARCHIVE);
    vk_postprocess.color_brightness = Cvar_Get("vk_color_brightness", "0.0", CVAR_ARCHIVE);
    vk_postprocess.color_contrast = Cvar_Get("vk_color_contrast", "1.0", CVAR_ARCHIVE);
    vk_postprocess.color_saturation = Cvar_Get("vk_color_saturation", "1.0", CVAR_ARCHIVE);
    vk_postprocess.color_tint = Cvar_Get("vk_color_tint", "white", CVAR_ARCHIVE);
    vk_postprocess.color_tint->changed = VK_PostProcess_ColorTintChanged;
    vk_postprocess.color_tint->generator = Com_Color_g;
    VK_PostProcess_ColorTintChanged(vk_postprocess.color_tint);
    vk_postprocess.color_split_shadows =
        Cvar_Get("vk_color_split_shadows", "white", CVAR_ARCHIVE);
    vk_postprocess.color_split_shadows->changed =
        VK_PostProcess_ColorSplitShadowsChanged;
    vk_postprocess.color_split_shadows->generator = Com_Color_g;
    VK_PostProcess_ColorSplitShadowsChanged(
        vk_postprocess.color_split_shadows);
    vk_postprocess.color_split_highlights =
        Cvar_Get("vk_color_split_highlights", "white", CVAR_ARCHIVE);
    vk_postprocess.color_split_highlights->changed =
        VK_PostProcess_ColorSplitHighlightsChanged;
    vk_postprocess.color_split_highlights->generator = Com_Color_g;
    VK_PostProcess_ColorSplitHighlightsChanged(
        vk_postprocess.color_split_highlights);
    vk_postprocess.color_split_strength =
        Cvar_Get("vk_color_split_strength", "0.0", CVAR_ARCHIVE);
    vk_postprocess.color_split_balance =
        Cvar_Get("vk_color_split_balance", "0.0", CVAR_ARCHIVE);
    vk_postprocess.color_lut = Cvar_Get("vk_color_lut", "", CVAR_ARCHIVE);
    vk_postprocess.color_lut->changed = VK_PostProcess_ColorLutChanged;
    vk_postprocess.color_lut_intensity =
        Cvar_Get("vk_color_lut_intensity", "1.0", CVAR_ARCHIVE);
    VK_PostProcess_ColorLutChanged(vk_postprocess.color_lut);
    vk_postprocess.bloom = Cvar_Get("vk_bloom", "1", CVAR_ARCHIVE);
    vk_postprocess.bloom_iterations =
        Cvar_Get("vk_bloom_iterations", "1", CVAR_ARCHIVE);
    vk_postprocess.bloom_downscale =
        Cvar_Get("vk_bloom_downscale", "4", CVAR_ARCHIVE);
    vk_postprocess.bloom_firefly =
        Cvar_Get("vk_bloom_firefly", "10.0", CVAR_ARCHIVE);
    vk_postprocess.bloom_sigma =
        Cvar_Get("vk_bloom_sigma", "8.0", CVAR_ARCHIVE);
    vk_postprocess.bloom_threshold =
        Cvar_Get("vk_bloom_threshold", "1.0", CVAR_ARCHIVE);
    vk_postprocess.bloom_knee =
        Cvar_Get("vk_bloom_knee", "0.5", CVAR_ARCHIVE);
    vk_postprocess.bloom_intensity =
        Cvar_Get("vk_bloom_intensity", "0.8", CVAR_ARCHIVE);
    vk_postprocess.bloom_saturation =
        Cvar_Get("vk_bloom_saturation", "1.0", CVAR_ARCHIVE);
    vk_postprocess.bloom_scene_saturation =
        Cvar_Get("vk_bloom_scene_saturation", "1.0", CVAR_ARCHIVE);
    vk_postprocess.dof = Cvar_Get("r_dof", "1", CVAR_ARCHIVE | CVAR_LATCH);
    vk_postprocess.dof_focus_distance =
        Cvar_Get("r_dof_focus_distance", "16.0", CVAR_SERVERINFO);
    vk_postprocess.dof_blur_range =
        Cvar_Get("r_dof_blur_range", "0.0", CVAR_SERVERINFO);
    vk_postprocess.crt_mode = Cvar_Get("r_crtmode", "0", CVAR_ARCHIVE);
    vk_postprocess.crt_bright_boost =
        Cvar_Get("r_crt_brightboost", "1.5", CVAR_SERVERINFO);
    vk_postprocess.crt_hard_pix =
        Cvar_Get("r_crt_hard_pix", "-8.0", CVAR_SERVERINFO);
    vk_postprocess.crt_hard_scan =
        Cvar_Get("r_crt_hard_scan", "-8.0", CVAR_SERVERINFO);
    vk_postprocess.crt_mask_dark =
        Cvar_Get("r_crt_mask_dark", "0.5", CVAR_SERVERINFO);
    vk_postprocess.crt_mask_light =
        Cvar_Get("r_crt_mask_light", "1.5", CVAR_SERVERINFO);
    vk_postprocess.crt_scale_in_linear_gamma =
        Cvar_Get("r_crt_scale_in_linear_gamma", "1.0", CVAR_ARCHIVE);
    vk_postprocess.crt_shadow_mask =
        Cvar_Get("r_crt_shadow_mask", "0.0", CVAR_ARCHIVE);
    VkDescriptorSetLayout scene_set_layout = VK_UI_GetDescriptorSetLayout();
    if (!scene_set_layout) {
        Com_SetLastError("Vulkan post-process: UI descriptor layout is unavailable");
        return false;
    }

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(vk_postprocess_push_t),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &scene_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    if (!VK_PostProcess_Check(vkCreatePipelineLayout(ctx->device, &layout_info, NULL,
                                                     &vk_postprocess.pipeline_layout),
                              "vkCreatePipelineLayout")) {
        return false;
    }
    vk_postprocess.initialized = true;
    return true;
}

void VK_PostProcess_DestroySwapchainResources(vk_context_t *ctx)
{
    (void)ctx;
    if (!vk_postprocess.initialized || !vk_postprocess.ctx ||
        !vk_postprocess.ctx->device) {
        return;
    }
    if (vk_postprocess.final_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.final_pipeline, NULL);
        vk_postprocess.final_pipeline = VK_NULL_HANDLE;
    }
    if (vk_postprocess.bloom_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.bloom_pipeline, NULL);
        vk_postprocess.bloom_pipeline = VK_NULL_HANDLE;
    }
    if (vk_postprocess.dof_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.dof_pipeline, NULL);
        vk_postprocess.dof_pipeline = VK_NULL_HANDLE;
    }
    if (vk_postprocess.crt_pipeline) {
        vkDestroyPipeline(vk_postprocess.ctx->device,
                          vk_postprocess.crt_pipeline, NULL);
        vk_postprocess.crt_pipeline = VK_NULL_HANDLE;
    }
    VK_PostProcess_DestroyAllExternalDescriptors();
    VK_PostProcess_DestroyBloomImages();
    VK_PostProcess_DestroyDofImages();
    if (vk_postprocess.bloom_render_pass) {
        vkDestroyRenderPass(vk_postprocess.ctx->device,
                            vk_postprocess.bloom_render_pass, NULL);
        vk_postprocess.bloom_render_pass = VK_NULL_HANDLE;
    }
    vk_postprocess.descriptor_generation++;
    if (!vk_postprocess.descriptor_generation) {
        vk_postprocess.descriptor_generation = 1;
    }
    vk_postprocess.bloom_resources_dirty = true;
    vk_postprocess.bloom_active = false;
    vk_postprocess.dof_resources_dirty = true;
    vk_postprocess.dof_active = false;
    vk_postprocess.swapchain_ready = false;
}

bool VK_PostProcess_CreateSwapchainResources(vk_context_t *ctx)
{
    if (!vk_postprocess.initialized || !ctx || !ctx->liquid_render_pass) {
        return false;
    }
    VK_PostProcess_DestroySwapchainResources(ctx);

    VkShaderModule vert_shader = VK_NULL_HANDLE;
    VkShaderModule frag_shader = VK_NULL_HANDLE;
    VkShaderModule bloom_frag_shader = VK_NULL_HANDLE;
    VkShaderModule dof_frag_shader = VK_NULL_HANDLE;
    VkShaderModule crt_frag_shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vert_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_postprocess_vert_spv_size,
        .pCode = vk_postprocess_vert_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device, &vert_info, NULL,
                                                   &vert_shader),
                              "vkCreateShaderModule(final postprocess vert)")) {
        return false;
    }
    VkShaderModuleCreateInfo frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_postprocess_frag_spv_size,
        .pCode = vk_postprocess_frag_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device, &frag_info, NULL,
                                                   &frag_shader),
                              "vkCreateShaderModule(final postprocess frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        return false;
    }
    VkShaderModuleCreateInfo bloom_frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_bloom_frag_spv_size,
        .pCode = vk_bloom_frag_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device,
                                                   &bloom_frag_info, NULL,
                                                   &bloom_frag_shader),
                              "vkCreateShaderModule(bloom frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        vkDestroyShaderModule(ctx->device, frag_shader, NULL);
        return false;
    }
    if (!VK_PostProcess_CreateBloomRenderPass(ctx)) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        vkDestroyShaderModule(ctx->device, frag_shader, NULL);
        vkDestroyShaderModule(ctx->device, bloom_frag_shader, NULL);
        return false;
    }
    VkShaderModuleCreateInfo dof_frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_dof_frag_spv_size,
        .pCode = vk_dof_frag_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device, &dof_frag_info,
                                                   NULL, &dof_frag_shader),
                              "vkCreateShaderModule(DOF frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        vkDestroyShaderModule(ctx->device, frag_shader, NULL);
        vkDestroyShaderModule(ctx->device, bloom_frag_shader, NULL);
        VK_PostProcess_DestroySwapchainResources(ctx);
        return false;
    }
    VkShaderModuleCreateInfo crt_frag_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vk_crt_frag_spv_size,
        .pCode = vk_crt_frag_spv,
    };
    if (!VK_PostProcess_Check(vkCreateShaderModule(ctx->device, &crt_frag_info,
                                                   NULL, &crt_frag_shader),
                              "vkCreateShaderModule(CRT frag)")) {
        vkDestroyShaderModule(ctx->device, vert_shader, NULL);
        vkDestroyShaderModule(ctx->device, frag_shader, NULL);
        vkDestroyShaderModule(ctx->device, bloom_frag_shader, NULL);
        vkDestroyShaderModule(ctx->device, dof_frag_shader, NULL);
        VK_PostProcess_DestroySwapchainResources(ctx);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_shader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_shader,
            .pName = "main",
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = q_countof(dynamic_states),
        .pDynamicStates = dynamic_states,
    };
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk_postprocess.pipeline_layout,
        .renderPass = ctx->liquid_render_pass,
        .subpass = 0,
    };
    bool created = VK_PostProcess_Check(
        vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                  &pipeline_info, NULL,
                                  &vk_postprocess.final_pipeline),
        "vkCreateGraphicsPipelines(final postprocess)");
    if (created) {
        pipeline_info.renderPass = vk_postprocess.bloom_render_pass;
        stages[1].module = bloom_frag_shader;
        created = VK_PostProcess_Check(
            vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                      &pipeline_info, NULL,
                                      &vk_postprocess.bloom_pipeline),
            "vkCreateGraphicsPipelines(bloom)");
    }
    if (created) {
        pipeline_info.renderPass = vk_postprocess.bloom_render_pass;
        stages[1].module = dof_frag_shader;
        created = VK_PostProcess_Check(
            vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                      &pipeline_info, NULL,
                                      &vk_postprocess.dof_pipeline),
            "vkCreateGraphicsPipelines(DOF)");
    }
    if (created) {
        pipeline_info.renderPass = ctx->liquid_render_pass;
        stages[1].module = crt_frag_shader;
        created = VK_PostProcess_Check(
            vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                      &pipeline_info, NULL,
                                      &vk_postprocess.crt_pipeline),
            "vkCreateGraphicsPipelines(CRT)");
    }
    vkDestroyShaderModule(ctx->device, vert_shader, NULL);
    vkDestroyShaderModule(ctx->device, frag_shader, NULL);
    vkDestroyShaderModule(ctx->device, bloom_frag_shader, NULL);
    vkDestroyShaderModule(ctx->device, dof_frag_shader, NULL);
    vkDestroyShaderModule(ctx->device, crt_frag_shader, NULL);
    if (!created) {
        VK_PostProcess_DestroySwapchainResources(ctx);
        return false;
    }
    vk_postprocess.bloom_resources_dirty = true;
    vk_postprocess.descriptor_generation++;
    if (!vk_postprocess.descriptor_generation) {
        vk_postprocess.descriptor_generation = 1;
    }
    vk_postprocess.swapchain_ready = true;
    return true;
}

void VK_PostProcess_Shutdown(vk_context_t *ctx)
{
    if (!vk_postprocess.initialized) {
        return;
    }
    if (!ctx) {
        ctx = vk_postprocess.ctx;
    }
    VK_PostProcess_DestroySwapchainResources(ctx);
    if (ctx && ctx->device && vk_postprocess.pipeline_layout) {
        vkDestroyPipelineLayout(ctx->device, vk_postprocess.pipeline_layout, NULL);
    }
    memset(&vk_postprocess, 0, sizeof(vk_postprocess));
}

void VK_PostProcess_RenderFrame(const refdef_t *fd)
{
    vk_postprocess.waterwarp_active = fd &&
        (fd->rdflags & RDF_UNDERWATER) &&
        (!vk_postprocess.waterwarp || vk_postprocess.waterwarp->integer != 0);

    memset(&vk_postprocess.push, 0, sizeof(vk_postprocess.push));
    vk_postprocess.push.time = fd ? fd->time : 0.0f;
    vk_postprocess.push.waterwarp = vk_postprocess.waterwarp_active ? 1.0f : 0.0f;
    vk_postprocess.push.output_size[0] = vk_postprocess.ctx &&
        vk_postprocess.ctx->swapchain.extent.width
        ? (float)vk_postprocess.ctx->swapchain.extent.width
        : (fd && fd->width ? (float)fd->width : 1.0f);
    vk_postprocess.push.output_size[1] = vk_postprocess.ctx &&
        vk_postprocess.ctx->swapchain.extent.height
        ? (float)vk_postprocess.ctx->swapchain.extent.height
        : (fd && fd->height ? (float)fd->height : 1.0f);

    memset(&vk_postprocess.dof_push, 0, sizeof(vk_postprocess.dof_push));
    vk_postprocess.dof_push.rect[2] = 1.0f;
    vk_postprocess.dof_push.rect[3] = 1.0f;
    vk_postprocess.dof_requested = fd && vk_postprocess.dof &&
        vk_postprocess.dof->integer != 0 &&
        !(fd->rdflags & RDF_NOWORLDMODEL) && fd->dof_strength > 0.0f;
    if (vk_postprocess.dof_requested) {
        float zfar = 8192.0f;
        const bsp_t *bsp = VK_World_GetBsp();
        if (bsp && bsp->numnodes > 0) {
            vec3_t extents;
            VectorSubtract(bsp->nodes[0].maxs, bsp->nodes[0].mins, extents);
            zfar = max(2048.0f, VectorLength(extents) * 4.0f);
        }
        renderer_view_push_t view_push;
        R_BuildViewPush(fd, 4.0f, zfar, &view_push);
        vk_postprocess.dof_push.params[0] =
            vk_postprocess.dof_focus_distance
                ? vk_postprocess.dof_focus_distance->value : 16.0f;
        vk_postprocess.dof_push.params[1] =
            vk_postprocess.dof_blur_range
                ? vk_postprocess.dof_blur_range->value : 0.0f;
        vk_postprocess.dof_push.params[2] = Q_clipf(fd->dof_strength, 0.0f, 1.0f);
        vk_postprocess.dof_push.params[3] = view_push.proj[10];
        vk_postprocess.dof_push.projection[0] = view_push.proj[14];

        if (fd->dof_rect_enabled) {
            const float output_width = max(vk_postprocess.push.output_size[0], 1.0f);
            const float output_height = max(vk_postprocess.push.output_size[1], 1.0f);
            float left = max((float)fd->dof_rect.left, 0.0f);
            float top = max((float)fd->dof_rect.top, 0.0f);
            float right = min((float)fd->dof_rect.right, output_width);
            float bottom = min((float)fd->dof_rect.bottom, output_height);
            if (right <= left || bottom <= top) {
                vk_postprocess.dof_push.rect[0] = 1.0f;
                vk_postprocess.dof_push.rect[1] = 1.0f;
                vk_postprocess.dof_push.rect[2] = 0.0f;
                vk_postprocess.dof_push.rect[3] = 0.0f;
            } else {
                vk_postprocess.dof_push.rect[0] = left / output_width;
                vk_postprocess.dof_push.rect[1] = top / output_height;
                vk_postprocess.dof_push.rect[2] = right / output_width;
                vk_postprocess.dof_push.rect[3] = bottom / output_height;
            }
        }
    }

    const bool correction_enabled = vk_postprocess.color_correction &&
        vk_postprocess.color_correction->integer != 0;
    vk_postprocess.push.brightness = correction_enabled
        ? Cvar_ClampValue(vk_postprocess.color_brightness, -1.0f, 1.0f) : 0.0f;
    vk_postprocess.push.contrast = correction_enabled
        ? Cvar_ClampValue(vk_postprocess.color_contrast, 0.0f, 4.0f) : 1.0f;
    vk_postprocess.push.saturation = correction_enabled
        ? Cvar_ClampValue(vk_postprocess.color_saturation, 0.0f, 4.0f) : 1.0f;
    memcpy(vk_postprocess.push.tint, vk_postprocess.tint,
           sizeof(vk_postprocess.push.tint));

    vk_postprocess.color_active = correction_enabled &&
        (fabsf(vk_postprocess.push.brightness) > 0.0001f ||
         fabsf(vk_postprocess.push.contrast - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.saturation - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.tint[0] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.tint[1] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.tint[2] - 1.0f) > 0.0001f);
    vk_postprocess.push.color_enabled = vk_postprocess.color_active ? 1.0f : 0.0f;

    vk_postprocess.push.split_params[0] = Cvar_ClampValue(
        vk_postprocess.color_split_strength, 0.0f, 1.0f);
    vk_postprocess.push.split_params[1] = Cvar_ClampValue(
        vk_postprocess.color_split_balance, -1.0f, 1.0f);
    memcpy(vk_postprocess.push.split_shadow, vk_postprocess.split_shadow,
           sizeof(vk_postprocess.push.split_shadow));
    memcpy(vk_postprocess.push.split_highlight, vk_postprocess.split_highlight,
           sizeof(vk_postprocess.push.split_highlight));

    vk_postprocess.split_active =
        vk_postprocess.push.split_params[0] > 0.0001f &&
        (fabsf(vk_postprocess.push.split_shadow[0] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_shadow[1] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_shadow[2] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_highlight[0] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_highlight[1] - 1.0f) > 0.0001f ||
         fabsf(vk_postprocess.push.split_highlight[2] - 1.0f) > 0.0001f);

    vk_postprocess.push.lut_params[0] = vk_postprocess.lut_valid
        ? Cvar_ClampValue(vk_postprocess.color_lut_intensity, 0.0f, 1.0f)
        : 0.0f;
    vk_postprocess.push.lut_params[1] = vk_postprocess.lut_size;
    vk_postprocess.push.lut_params[2] = vk_postprocess.lut_width > 0
        ? 1.0f / (float)vk_postprocess.lut_width : 0.0f;
    vk_postprocess.push.lut_params[3] = vk_postprocess.lut_height > 0
        ? 1.0f / (float)vk_postprocess.lut_height : 0.0f;
    vk_postprocess.lut_active = vk_postprocess.push.lut_params[0] > 0.0001f;

    const int bloom_downscale = Cvar_ClampInteger(
        vk_postprocess.bloom_downscale, 1, 8);
    const float base_height = vk_postprocess.ctx &&
        vk_postprocess.ctx->swapchain.extent.height
        ? (float)vk_postprocess.ctx->swapchain.extent.height
        : (fd && fd->height ? (float)fd->height : 1.0f);
    vk_postprocess.bloom_push.params[0] = Cvar_ClampValue(
        vk_postprocess.bloom_threshold, 0.0f, 10.0f);
    vk_postprocess.bloom_push.params[1] = Cvar_ClampValue(
        vk_postprocess.bloom_knee, 0.0f, 1.0f);
    vk_postprocess.bloom_push.params[2] = Cvar_ClampValue(
        vk_postprocess.bloom_firefly, 0.0f, 1000.0f);
    vk_postprocess.bloom_push.params[3] = Cvar_ClampValue(
        vk_postprocess.bloom_sigma, 1.0f, 25.0f) * base_height / 2160.0f *
        4.0f / (float)bloom_downscale;
    vk_postprocess.bloom_push.params[3] =
        max(vk_postprocess.bloom_push.params[3], 0.5f);
    vk_postprocess.bloom_requested = fd &&
        !(fd->rdflags & RDF_NOWORLDMODEL) && vk_postprocess.bloom &&
        vk_postprocess.bloom->integer != 0;

    vk_postprocess.crt_active = vk_postprocess.crt_mode &&
        vk_postprocess.crt_mode->integer != 0;
    vk_postprocess.crt_push.params[0] = vk_postprocess.crt_hard_pix
        ? min(vk_postprocess.crt_hard_pix->value, 0.0f) : -8.0f;
    vk_postprocess.crt_push.params[1] = vk_postprocess.crt_hard_scan
        ? min(vk_postprocess.crt_hard_scan->value, 0.0f) : -8.0f;
    vk_postprocess.crt_push.params[2] = vk_postprocess.crt_bright_boost
        ? max(vk_postprocess.crt_bright_boost->value, 0.0f) : 1.5f;
    vk_postprocess.crt_push.params[3] = vk_postprocess.crt_scale_in_linear_gamma
        ? Cvar_ClampValue(vk_postprocess.crt_scale_in_linear_gamma, 0.0f, 1.0f)
        : 1.0f;
    vk_postprocess.crt_push.params2[0] = vk_postprocess.crt_mask_dark
        ? max(vk_postprocess.crt_mask_dark->value, 0.0f) : 0.5f;
    vk_postprocess.crt_push.params2[1] = vk_postprocess.crt_mask_light
        ? max(vk_postprocess.crt_mask_light->value, 0.0f) : 1.5f;
    vk_postprocess.crt_push.params2[2] = vk_postprocess.crt_shadow_mask
        ? Cvar_ClampValue(vk_postprocess.crt_shadow_mask, 0.0f, 4.0f) : 0.0f;
    // Vulkan currently has no dynamic render scale, so the source and output
    // scanline grids share one pixel scale. Keep this explicit for the future
    // resolution-scaling path rather than baking it into the shader.
    vk_postprocess.crt_push.params2[3] = 1.0f;
    vk_postprocess.crt_push.texel[0] = 1.0f /
        max(vk_postprocess.push.output_size[0], 1.0f);
    vk_postprocess.crt_push.texel[1] = 1.0f /
        max(vk_postprocess.push.output_size[1], 1.0f);
    vk_postprocess.crt_push.texel[2] = vk_postprocess.push.output_size[0];
    vk_postprocess.crt_push.texel[3] = vk_postprocess.push.output_size[1];
}

bool VK_PostProcess_NeedsSafeResourceUpdate(void)
{
    if (!vk_postprocess.initialized || !vk_postprocess.swapchain_ready ||
        !VK_PostProcess_CurrentSceneView()) {
        return false;
    }

    bool bloom_resources_need_rebuild = false;
    bool dof_resources_need_rebuild = false;
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (vk_postprocess.bloom_requested) {
        const int downscale = Cvar_ClampInteger(vk_postprocess.bloom_downscale,
                                                 1, 8);
        uint32_t width = max(1u, vk_postprocess.ctx->swapchain.extent.width /
                                  (uint32_t)downscale);
        uint32_t height = max(1u, vk_postprocess.ctx->swapchain.extent.height /
                                   (uint32_t)downscale);
        bloom_resources_need_rebuild = !frame || vk_postprocess.bloom_resources_dirty ||
            !frame->bloom_ping.image || !frame->bloom_pong.image ||
            vk_postprocess.bloom_width != width ||
            vk_postprocess.bloom_height != height;
    }
    if (vk_postprocess.dof_requested) {
        uint32_t width = max(1u, vk_postprocess.ctx->swapchain.extent.width / 4u);
        uint32_t height = max(1u, vk_postprocess.ctx->swapchain.extent.height / 4u);
        dof_resources_need_rebuild = !frame || vk_postprocess.dof_resources_dirty ||
            !frame->dof_ping.image || !frame->dof_pong.image ||
            !frame->dof_scene.image || vk_postprocess.dof_width != width ||
            vk_postprocess.dof_height != height;
    }
    return bloom_resources_need_rebuild || dof_resources_need_rebuild;
}

void VK_PostProcess_PrepareFrame(void)
{
    if (!vk_postprocess.initialized || !vk_postprocess.swapchain_ready ||
        !VK_PostProcess_CurrentSceneView()) {
        return;
    }

    bool previous_bloom_active = vk_postprocess.bloom_active;
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!frame) {
        return;
    }
    if (vk_postprocess.bloom_requested) {
        const int downscale = Cvar_ClampInteger(vk_postprocess.bloom_downscale,
                                                 1, 8);
        uint32_t width = max(1u, vk_postprocess.ctx->swapchain.extent.width /
                                  (uint32_t)downscale);
        uint32_t height = max(1u, vk_postprocess.ctx->swapchain.extent.height /
                                   (uint32_t)downscale);
        if (vk_postprocess.bloom_resources_dirty ||
            !frame->bloom_ping.image || !frame->bloom_pong.image ||
            vk_postprocess.bloom_width != width ||
            vk_postprocess.bloom_height != height) {
            VK_PostProcess_DestroyAllExternalDescriptors();
            VK_PostProcess_CreateBloomImages(width, height);
            vk_postprocess.descriptor_generation++;
            if (!vk_postprocess.descriptor_generation) {
                vk_postprocess.descriptor_generation = 1;
            }
            vk_postprocess.bloom_resources_dirty = false;
        }
    }

    if (vk_postprocess.dof_requested) {
        uint32_t width = max(1u, vk_postprocess.ctx->swapchain.extent.width / 4u);
        uint32_t height = max(1u, vk_postprocess.ctx->swapchain.extent.height / 4u);
        if (vk_postprocess.dof_resources_dirty || !frame->dof_ping.image ||
            !frame->dof_pong.image || !frame->dof_scene.image ||
            vk_postprocess.dof_width != width || vk_postprocess.dof_height != height) {
            VK_PostProcess_DestroyAllExternalDescriptors();
            vk_postprocess.dof_supported =
                VK_PostProcess_CreateDofImages(width, height);
            vk_postprocess.descriptor_generation++;
            if (!vk_postprocess.descriptor_generation) {
                vk_postprocess.descriptor_generation = 1;
            }
            vk_postprocess.dof_resources_dirty = false;
        }
    }

    vk_postprocess.bloom_active = vk_postprocess.bloom_requested &&
        vk_postprocess.bloom_supported && frame->bloom_ping.image &&
        frame->bloom_pong.image;
    vk_postprocess.push.bloom_final[0] = vk_postprocess.bloom_active
        ? Cvar_ClampValue(vk_postprocess.bloom_intensity, 0.0f, 10.0f)
        : 0.0f;
    vk_postprocess.push.bloom_final[1] = vk_postprocess.bloom_active
        ? Cvar_ClampValue(vk_postprocess.bloom_scene_saturation, 0.0f, 4.0f)
        : 1.0f;
    vk_postprocess.push.bloom_final[2] = vk_postprocess.bloom_active
        ? Cvar_ClampValue(vk_postprocess.bloom_saturation, 0.0f, 4.0f)
        : 1.0f;
    vk_postprocess.push.bloom_final[3] = vk_postprocess.bloom_active ? 1.0f : 0.0f;
    vk_postprocess.dof_active = vk_postprocess.dof_requested &&
        vk_postprocess.dof_supported && frame->dof_ping.image &&
        frame->dof_pong.image && frame->dof_scene.image &&
        VK_PostProcess_CurrentDepthView() != VK_NULL_HANDLE;
    (void)previous_bloom_active;
    VK_PostProcess_UpdateExternalDescriptors();
}

static void VK_PostProcess_ImageBarrier(VkCommandBuffer cmd, VkImage image,
                                        VkImageLayout old_layout,
                                        VkImageLayout new_layout,
                                        VkAccessFlags src_access,
                                        VkAccessFlags dst_access,
                                        VkPipelineStageFlags src_stage,
                                        VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL,
                         1, &barrier);
}

static void VK_PostProcess_RecordBloomPass(
    VkCommandBuffer cmd, vk_postprocess_bloom_image_t *target,
    VkDescriptorSet descriptor_set, float mode, uint32_t width,
    uint32_t height, const vk_postprocess_bloom_push_t *source_push)
{
    if (!cmd || !target || !target->image || !target->framebuffer ||
        !descriptor_set || !vk_postprocess.bloom_pipeline || !source_push ||
        !width || !height) {
        return;
    }

    VK_PostProcess_ImageBarrier(
        cmd, target->image,
        target->initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        target->initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        target->initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk_postprocess.bloom_render_pass,
        .framebuffer = target->framebuffer,
        .renderArea = {
            .extent = {
                .width = width,
                .height = height,
            },
        },
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)height,
        .width = (float)width,
        .height = -(float)height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .extent = {
            .width = width,
            .height = height,
        },
    };
    vk_postprocess_bloom_push_t push = *source_push;
    push.output_size[0] = (float)width;
    push.output_size[1] = (float)height;
    push.aux[0] = mode;
    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.bloom_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0, 1,
                            &descriptor_set, 0, NULL);
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    target->initialized = true;
}

void VK_PostProcess_RecordBloom(VkCommandBuffer cmd)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!vk_postprocess.bloom_active || !cmd || !frame ||
        !frame->bloom_scene_descriptor_set ||
        !frame->bloom_ping_descriptor_set || !frame->bloom_pong_descriptor_set) {
        return;
    }

    VK_PostProcess_RecordBloomPass(
        cmd, &frame->bloom_ping,
        frame->bloom_scene_descriptor_set, VK_BLOOM_MODE_PREFILTER,
        vk_postprocess.bloom_width, vk_postprocess.bloom_height,
        &vk_postprocess.bloom_push);

    const int passes = Cvar_ClampInteger(vk_postprocess.bloom_iterations,
                                         1, 8) * 2;
    for (int i = 0; i < passes; i++) {
        if (i & 1) {
            VK_PostProcess_RecordBloomPass(
                cmd, &frame->bloom_ping,
                frame->bloom_pong_descriptor_set, VK_BLOOM_MODE_BLUR_Y,
                vk_postprocess.bloom_width, vk_postprocess.bloom_height,
                &vk_postprocess.bloom_push);
        } else {
            VK_PostProcess_RecordBloomPass(
                cmd, &frame->bloom_pong,
                frame->bloom_ping_descriptor_set, VK_BLOOM_MODE_BLUR_X,
                vk_postprocess.bloom_width, vk_postprocess.bloom_height,
                &vk_postprocess.bloom_push);
        }
    }
}

static void VK_PostProcess_RecordDofComposite(
    VkCommandBuffer cmd, vk_postprocess_bloom_image_t *target,
    VkDescriptorSet descriptor_set)
{
    if (!cmd || !target || !target->image || !target->framebuffer ||
        !descriptor_set || !vk_postprocess.dof_pipeline ||
        !vk_postprocess.ctx) {
        return;
    }

    VK_PostProcess_ImageBarrier(
        cmd, target->image,
        target->initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        target->initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        target->initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VkExtent2D extent = vk_postprocess.ctx->swapchain.extent;
    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk_postprocess.bloom_render_pass,
        .framebuffer = target->framebuffer,
        .renderArea = {
            .extent = extent,
        },
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)extent.height,
        .width = (float)extent.width,
        .height = -(float)extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .extent = extent,
    };
    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.dof_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0, 1,
                            &descriptor_set, 0, NULL);
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(vk_postprocess.dof_push),
                       &vk_postprocess.dof_push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    target->initialized = true;
}

void VK_PostProcess_RecordDof(VkCommandBuffer cmd)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    if (!vk_postprocess.dof_active || !cmd || !frame ||
        !frame->dof_scene_descriptor_set || !frame->dof_ping_descriptor_set ||
        !frame->dof_pong_descriptor_set || !frame->dof_composite_descriptor_set) {
        return;
    }

    // OpenGL's native path uses a quarter-resolution downsample followed by
    // four separable Gaussian iterations before its depth-aware composite.
    vk_postprocess_bloom_push_t blur_push = { 0 };
    blur_push.params[3] = 1.0f;
    VK_PostProcess_RecordBloomPass(
        cmd, &frame->dof_ping, frame->dof_scene_descriptor_set,
        VK_BLOOM_MODE_COPY, vk_postprocess.dof_width,
        vk_postprocess.dof_height, &blur_push);
    for (int i = 0; i < 8; i++) {
        if (i & 1) {
            VK_PostProcess_RecordBloomPass(
                cmd, &frame->dof_ping, frame->dof_pong_descriptor_set,
                VK_BLOOM_MODE_BLUR_Y, vk_postprocess.dof_width,
                vk_postprocess.dof_height, &blur_push);
        } else {
            VK_PostProcess_RecordBloomPass(
                cmd, &frame->dof_pong, frame->dof_ping_descriptor_set,
                VK_BLOOM_MODE_BLUR_X, vk_postprocess.dof_width,
                vk_postprocess.dof_height, &blur_push);
        }
    }
    VK_PostProcess_RecordDofComposite(cmd, &frame->dof_scene,
                                      frame->dof_composite_descriptor_set);
}

bool VK_PostProcess_UsesCompositePass(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        (vk_postprocess.waterwarp_active || vk_postprocess.color_active ||
         vk_postprocess.split_active || vk_postprocess.lut_active ||
         vk_postprocess.bloom_active || vk_postprocess.dof_active) &&
        vk_postprocess.final_pipeline;
}

bool VK_PostProcess_UsesCrtPass(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        vk_postprocess.crt_active && vk_postprocess.crt_pipeline;
}

bool VK_PostProcess_UsesFinalPass(void)
{
    return VK_PostProcess_UsesCompositePass() || VK_PostProcess_UsesCrtPass();
}

bool VK_PostProcess_UsesDof(void)
{
    return vk_postprocess.initialized && vk_postprocess.swapchain_ready &&
        vk_postprocess.dof_active && vk_postprocess.dof_pipeline;
}

void VK_PostProcess_RecordFinal(VkCommandBuffer cmd, const VkExtent2D *extent,
                                VkDescriptorSet scene_descriptor_set)
{
    vk_postprocess_frame_resources_t *frame =
        VK_PostProcess_CurrentFrameResources();
    VkDescriptorSet descriptor_set = frame && frame->final_scene_descriptor_set
        ? frame->final_scene_descriptor_set : scene_descriptor_set;
    if (!VK_PostProcess_UsesCompositePass() || !cmd || !extent ||
        !descriptor_set) {
        return;
    }
    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)extent->height,
        .width = (float)extent->width,
        .height = -(float)extent->height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = *extent,
    };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.final_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0, 1,
                            &descriptor_set, 0, NULL);
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(vk_postprocess.push),
                       &vk_postprocess.push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void VK_PostProcess_RecordCrt(VkCommandBuffer cmd, const VkExtent2D *extent,
                              VkDescriptorSet scene_descriptor_set)
{
    if (!VK_PostProcess_UsesCrtPass() || !cmd || !extent ||
        !scene_descriptor_set) {
        return;
    }
    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)extent->height,
        .width = (float)extent->width,
        .height = -(float)extent->height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = *extent,
    };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_postprocess.crt_pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_postprocess.pipeline_layout, 0, 1,
                            &scene_descriptor_set, 0, NULL);
    vkCmdPushConstants(cmd, vk_postprocess.pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(vk_postprocess.crt_push),
                       &vk_postprocess.crt_push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
