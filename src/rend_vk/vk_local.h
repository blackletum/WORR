/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "client/client.h"
#include "client/video.h"
#include "renderer/renderer.h"

#include <stdint.h>

#if defined(RENDERER_DLL)
#include "renderer/renderer_api.h"
#endif

#if defined(RENDERER_VULKAN)
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif

#if USE_X11
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#endif

#if USE_WAYLAND
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#endif

#include <vulkan/vulkan.h>
#endif

#define VK_MAX_FRAMES_IN_FLIGHT 2
#define VK_INVALID_FRAME_SLOT UINT32_MAX

typedef struct vk_frame_context_s {
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkFence in_flight_fence;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    // A depth-only view is kept separate from the framebuffer's combined
    // depth/stencil view so native post-process passes can sample depth
    // without exposing stencil data.
    VkImageView depth_sample_view;
    VkFramebuffer *framebuffers;
    VkImage liquid_scene_image;
    VkDeviceMemory liquid_scene_memory;
    VkImageView liquid_scene_view;
    VkDescriptorSet liquid_scene_descriptor_set;
    bool liquid_scene_initialized;
    bool submitted;
} vk_frame_context_t;

typedef struct vk_swapchain_s {
    VkSwapchainKHR handle;
    VkFormat format;
    VkFormat depth_format;
    VkExtent2D extent;
    VkImage *images;
    VkImageView *views;
    VkSemaphore *render_finished;
    uint32_t *image_frame_slots;
    uint32_t image_count;
} vk_swapchain_t;

typedef struct vk_context_s {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;
    VkSurfaceKHR surface;
    VkRenderPass render_pass;
    VkRenderPass overlay_render_pass;
    VkRenderPass liquid_render_pass;
    VkCommandPool command_pool;
    vk_frame_context_t frames[VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t frame_count;
    uint32_t current_frame;
    vk_swapchain_t swapchain;
} vk_context_t;

typedef struct vk_state_s {
    bool initialized;
    bool swapchain_dirty;
    vid_native_window_t native_window;
    vk_context_t ctx;
} vk_state_t;

extern vk_state_t vk_state;
