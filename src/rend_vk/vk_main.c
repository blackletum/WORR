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

#include "vk_local.h"
#include "vk_debug.h"
#include "vk_entity.h"
#include "vk_postprocess.h"
#include "vk_shadow.h"
#include "vk_ui.h"
#include "vk_world.h"
#include "common/utils.h"
#include "format/pcx.h"
#include "renderer/shadow_frontend.h"
#include "refresh/debug.h"
#include "refresh/refresh.h"

#include "stb_image_write.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if USE_SDL
#include <SDL3/SDL_vulkan.h>
#endif

#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif

#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR ((VkInstanceCreateFlags)0x00000001)
#endif

vk_state_t vk_state;
refcfg_t r_config;
uint32_t d_8to24table[256];
static cvar_t *vk_draw_world;
static cvar_t *vk_draw_entities;
static cvar_t *vk_draw_ui;
static cvar_t *vk_damageblend_frac;
static cvar_t *vk_screenshot_dir;
static shadow_frontend_state_t vk_shadow_frontend;
static shadow_frontend_cvars_t vk_shadow_frontend_cvars;
static bool vk_shadow_frontend_cvars_registered;

// Swapchain readback for the screenshot commands. A request latches in
// vk_screenshot_pending; the next presented frame records a copy of its
// swapchain image into a host-visible buffer and writes the requested format after the
// frame fence signals.
static char vk_screenshot_path[MAX_OSPATH];
static bool vk_screenshot_tga;
static bool vk_screenshot_pending;
static bool vk_screenshot_armed;
static bool vk_screenshot_supported;
static VkBuffer vk_screenshot_buffer;
static VkDeviceMemory vk_screenshot_memory;
static VkDeviceSize vk_screenshot_capacity;
static bool vk_dof_supported;
static uint64_t vk_frame_begin_us;

// Keep CPU submission timing at the same sub-millisecond precision used by
// the OpenGL profiler. Sys_Milliseconds() is QPC-backed on Windows but its
// integer result makes ordinary Vulkan submit work look like 0 ms.
static uint64_t VK_HighResMicroseconds(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart) {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000ULL +
           (uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000ULL /
           (uint64_t)frequency.QuadPart;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000ULL +
           (uint64_t)now.tv_nsec / 1000ULL;
#endif
}

static bool VK_ShadowResolveCasterBounds(void *userdata, const entity_t *ent,
                                         const bsp_t *world_bsp,
                                         vec3_t local_mins,
                                         vec3_t local_maxs)
{
    (void)userdata;
    if (!ent) {
        return false;
    }

    if (ent->model & BIT(31)) {
        int model_index = ~ent->model;
        if (!world_bsp || !world_bsp->models || model_index < 1 ||
            model_index >= world_bsp->nummodels) {
            return false;
        }

        const mmodel_t *model = &world_bsp->models[model_index];
        VectorCopy(model->mins, local_mins);
        VectorCopy(model->maxs, local_maxs);
        return true;
    }

    return VK_Entity_ModelBounds(ent->model, ent, local_mins, local_maxs);
}

static const char *VK_ShadowModelName(void *userdata, qhandle_t model,
                                      const bsp_t *world_bsp)
{
    (void)userdata;
    (void)world_bsp;
    if (!model || (model & BIT(31))) {
        return "";
    }
    return VK_Entity_ModelName(model);
}

static const shadow_backend_ops_t vk_shadow_backend_ops = {
    .backend_name = "vulkan",
    .supports_depth_compare_pages = true,
    .supports_moment_pages = true,
    .supports_cube_array_pages = false,
    .supports_array_2d_pages = true,
    .max_pages = VK_SHADOW_MAX_PAGES,
    .max_resolution = VK_SHADOW_MAX_RESOLUTION,
    .userdata = NULL,
    .begin_frame = VK_Shadow_BeginFrame,
    .resolve_caster_bounds = VK_ShadowResolveCasterBounds,
    .model_name = VK_ShadowModelName,
    .ensure_page = VK_Shadow_EnsurePage,
    .render_view = VK_Shadow_RenderView,
    .end_frame = VK_Shadow_EndFrame,
    .describe_materialization = VK_Shadow_DescribeMaterialization,
};

static bool VK_LoadPaletteFromColormap(void)
{
    byte *data = NULL;
    int len = FS_LoadFile("pics/colormap.pcx", (void **)&data);
    if (len < (int)sizeof(dpcx_t) || !data) {
        if (data) {
            FS_FreeFile(data);
        }
        return false;
    }

    const dpcx_t *pcx = (const dpcx_t *)data;
    bool valid = (pcx->manufacturer == 10 &&
                  pcx->version == 5 &&
                  pcx->encoding == 1 &&
                  pcx->bits_per_pixel == 8 &&
                  pcx->color_planes == 1 &&
                  len >= 768);
    if (!valid) {
        FS_FreeFile(data);
        return false;
    }

    const byte *src = data + len - 768;
    for (int i = 0; i < 255; i++, src += 3) {
        d_8to24table[i] = COLOR_U32_RGBA(src[0], src[1], src[2], 255);
    }
    d_8to24table[255] = COLOR_U32_RGBA(src[0], src[1], src[2], 0);

    FS_FreeFile(data);
    return true;
}

static void VK_InitPalette(void)
{
    if (VK_LoadPaletteFromColormap()) {
        return;
    }

    for (int i = 0; i < 255; i++) {
        d_8to24table[i] = COLOR_RGB(i, i, i).u32;
    }
    d_8to24table[255] = COLOR_RGBA(0, 0, 0, 0).u32;
    Com_WPrintf("Vulkan: using grayscale fallback palette; colormap.pcx not available\n");
}

static const char *VK_ResultString(VkResult result)
{
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "VK_UNKNOWN_ERROR";
    }
}

static bool VK_Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }

    Com_SetLastError(va("Vulkan %s failed: %s", what, VK_ResultString(result)));
    return false;
}

static bool VK_ArrayBytes(size_t count, size_t item_size, size_t *out_size,
                          const char *what)
{
    if (!out_size) {
        Com_SetLastError("Vulkan: invalid allocation size output");
        return false;
    }

    if (item_size && count > SIZE_MAX / item_size) {
        Com_SetLastError(va("Vulkan: %s allocation size overflow", what));
        return false;
    }

    *out_size = count * item_size;
    return true;
}

static bool VK_ImageBytes(uint32_t width, uint32_t height, size_t bytes_per_pixel,
                          size_t *out_size, const char *what)
{
    if (!width || !height || !bytes_per_pixel) {
        Com_SetLastError(va("Vulkan: %s has zero dimensions", what));
        return false;
    }

    size_t pixel_count = 0;
    if (!VK_ArrayBytes((size_t)width, (size_t)height, &pixel_count, what)) {
        return false;
    }

    return VK_ArrayBytes(pixel_count, bytes_per_pixel, out_size, what);
}

static void *VK_AllocArray(size_t count, size_t item_size, const char *what)
{
    size_t bytes = 0;
    if (!VK_ArrayBytes(count, item_size, &bytes, what)) {
        return NULL;
    }

    void *memory = Z_TagMallocz(bytes, TAG_RENDERER);
    if (!memory) {
        Com_SetLastError(va("Vulkan: out of memory for %s", what));
        return NULL;
    }

    return memory;
}

static uint32_t VK_ClampU32(uint32_t value, uint32_t min_value, uint32_t max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static uint32_t VK_FindMemoryType(VkPhysicalDevice physical_device, uint32_t type_filter,
                                  VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & BIT(i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

static bool VK_IsDepthFormatSupported(VkPhysicalDevice device, VkFormat format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device, format, &props);
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
}

static bool VK_DepthFormatSupportsSampling(VkPhysicalDevice device, VkFormat format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device, format, &props);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (props.optimalTilingFeatures & required) == required;
}

static VkFormat VK_ChooseDepthFormat(VkPhysicalDevice device)
{
    // Prefer a combined depth/stencil attachment. Native alias-model outlines
    // use stencil masking just like the OpenGL renderer, while the depth-only
    // format remains a compatibility fallback for unusually limited devices.
    static const VkFormat candidates[] = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
    };

    for (size_t i = 0; i < q_countof(candidates); ++i) {
        if (VK_IsDepthFormatSupported(device, candidates[i])) {
            return candidates[i];
        }
    }

    return VK_FORMAT_UNDEFINED;
}

static bool VK_DepthFormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static const char *VK_SurfaceExtensionForPlatform(vid_native_platform_t platform)
{
    switch (platform) {
#if defined(_WIN32)
    case VID_NATIVE_WIN32:
        return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif
#if USE_X11
    case VID_NATIVE_X11:
        return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif
#if USE_WAYLAND
    case VID_NATIVE_WAYLAND:
        return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
#endif
    case VID_NATIVE_SDL:
    default:
        return NULL;
    }
}

static bool VK_InstanceHasExtension(const VkExtensionProperties *exts, uint32_t ext_count,
                                    const char *name)
{
    for (uint32_t i = 0; i < ext_count; ++i) {
        if (!strcmp(exts[i].extensionName, name))
            return true;
    }

    return false;
}

static bool VK_AddInstanceExtension(const char **extensions, uint32_t capacity, uint32_t *count,
                                    const char *name)
{
    for (uint32_t i = 0; i < *count; ++i) {
        if (!strcmp(extensions[i], name))
            return true;
    }

    if (*count >= capacity) {
        Com_SetLastError("Vulkan: instance extension list overflow");
        return false;
    }

    extensions[*count] = name;
    (*count)++;
    return true;
}

static bool VK_CollectInstanceExtensions(const VkExtensionProperties *exts, uint32_t ext_count,
                                         const char **extensions, uint32_t capacity,
                                         uint32_t *out_count,
                                         VkInstanceCreateFlags *out_flags)
{
    uint32_t enabled_count = 0;
    VkInstanceCreateFlags create_flags = 0;

#if USE_SDL
    if (vk_state.native_window.platform == VID_NATIVE_SDL) {
        Uint32 sdl_ext_count = 0;
        const char *const *sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
        if (!sdl_exts || !sdl_ext_count) {
            const char *error = SDL_GetError();
            Com_SetLastError(error && *error
                                 ? va("Vulkan: SDL failed to report instance extensions: %s", error)
                                 : "Vulkan: SDL failed to report instance extensions");
            return false;
        }

        for (Uint32 i = 0; i < sdl_ext_count; ++i) {
            const char *ext = sdl_exts[i];
            if (!VK_InstanceHasExtension(exts, ext_count, ext)) {
                Com_SetLastError(va("Vulkan: required SDL instance extension missing: %s", ext));
                return false;
            }
            if (!VK_AddInstanceExtension(extensions, capacity, &enabled_count, ext))
                return false;
        }
    } else
#endif
    {
        const char *surface_ext = VK_SurfaceExtensionForPlatform(vk_state.native_window.platform);
        if (!surface_ext) {
            Com_SetLastError("Vulkan: unsupported video driver for surface creation");
            return false;
        }

        if (!VK_InstanceHasExtension(exts, ext_count, VK_KHR_SURFACE_EXTENSION_NAME) ||
            !VK_InstanceHasExtension(exts, ext_count, surface_ext)) {
            Com_SetLastError("Vulkan: required instance extensions missing");
            return false;
        }

        if (!VK_AddInstanceExtension(extensions, capacity, &enabled_count,
                                     VK_KHR_SURFACE_EXTENSION_NAME) ||
            !VK_AddInstanceExtension(extensions, capacity, &enabled_count, surface_ext)) {
            return false;
        }
    }

    if (VK_InstanceHasExtension(exts, ext_count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        if (!VK_AddInstanceExtension(extensions, capacity, &enabled_count,
                                     VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            return false;
        }
        create_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    *out_count = enabled_count;
    *out_flags = create_flags;
    return true;
}

static void VK_DestroySurfaceHandle(vk_context_t *ctx)
{
    if (!ctx->instance || !ctx->surface)
        return;

#if USE_SDL
    if (vk_state.native_window.platform == VID_NATIVE_SDL)
        SDL_Vulkan_DestroySurface(ctx->instance, ctx->surface, NULL);
    else
#endif
        vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);

    ctx->surface = VK_NULL_HANDLE;
}

static bool VK_QueryNativeWindow(vid_native_window_t *out)
{
    if (!out) {
        Com_SetLastError("Vulkan: native window buffer missing");
        return false;
    }

    if (!vid || !vid->get_native_window) {
        Com_SetLastError("Vulkan: video driver does not expose native window handles");
        return false;
    }

    memset(out, 0, sizeof(*out));
    if (!vid->get_native_window(out)) {
        Com_SetLastError("Vulkan: video driver failed to provide native window handles");
        return false;
    }

    return true;
}

static bool VK_CreateInstance(void)
{
    uint32_t ext_count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkEnumerateInstanceExtensionProperties");
    }
    if (ext_count == 0) {
        Com_SetLastError("Vulkan: no instance extensions reported");
        return false;
    }

    VkExtensionProperties *exts = VK_AllocArray(ext_count, sizeof(*exts),
                                                "instance extensions");
    if (!exts) {
        return false;
    }

    result = vkEnumerateInstanceExtensionProperties(NULL, &ext_count, exts);
    if (result != VK_SUCCESS) {
        Z_Free(exts);
        return VK_Check(result, "vkEnumerateInstanceExtensionProperties");
    }

    const char *extensions[8];
    uint32_t enabled_ext_count = 0;
    VkInstanceCreateFlags create_flags = 0;
    bool have_extensions = VK_CollectInstanceExtensions(exts, ext_count, extensions,
                                                        q_countof(extensions), &enabled_ext_count,
                                                        &create_flags);
    Z_Free(exts);
    if (!have_extensions)
        return false;

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = PRODUCT,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "WORR",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = create_flags,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = enabled_ext_count,
        .ppEnabledExtensionNames = extensions,
    };

    return VK_Check(vkCreateInstance(&create_info, NULL, &vk_state.ctx.instance),
                    "vkCreateInstance");
}

static bool VK_CreateSurface(void)
{
    const vid_native_window_t *native = &vk_state.native_window;

    switch (native->platform) {
#if defined(_WIN32)
    case VID_NATIVE_WIN32: {
        VkWin32SurfaceCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = (HINSTANCE)native->handle.win32.hinstance,
            .hwnd = (HWND)native->handle.win32.hwnd,
        };
        return VK_Check(vkCreateWin32SurfaceKHR(vk_state.ctx.instance, &create_info, NULL,
                                                &vk_state.ctx.surface),
                        "vkCreateWin32SurfaceKHR");
    }
#endif
#if USE_X11
    case VID_NATIVE_X11: {
        VkXlibSurfaceCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = (Display *)native->handle.x11.display,
            .window = (Window)native->handle.x11.window,
        };
        return VK_Check(vkCreateXlibSurfaceKHR(vk_state.ctx.instance, &create_info, NULL,
                                               &vk_state.ctx.surface),
                        "vkCreateXlibSurfaceKHR");
    }
#endif
#if USE_WAYLAND
    case VID_NATIVE_WAYLAND: {
        VkWaylandSurfaceCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .display = (struct wl_display *)native->handle.wayland.display,
            .surface = (struct wl_surface *)native->handle.wayland.surface,
        };
        return VK_Check(vkCreateWaylandSurfaceKHR(vk_state.ctx.instance, &create_info, NULL,
                                                  &vk_state.ctx.surface),
                        "vkCreateWaylandSurfaceKHR");
    }
#endif
    case VID_NATIVE_SDL:
#if USE_SDL
        if (!SDL_Vulkan_CreateSurface((SDL_Window *)native->handle.sdl.window,
                                      vk_state.ctx.instance, NULL, &vk_state.ctx.surface)) {
            const char *error = SDL_GetError();
            Com_SetLastError(error && *error
                                 ? va("Vulkan: SDL surface creation failed: %s", error)
                                 : "Vulkan: SDL surface creation failed");
            return false;
        }
        return true;
#else
        Com_SetLastError("Vulkan: SDL native window handles are not supported in this build");
        return false;
#endif
    default:
        Com_SetLastError("Vulkan: unsupported video driver for surface creation");
        return false;
    }
}

static bool VK_DeviceHasExtension(VkPhysicalDevice device, const char *ext)
{
    uint32_t count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    if (result != VK_SUCCESS || count == 0) {
        return false;
    }

    VkExtensionProperties *props = VK_AllocArray(count, sizeof(*props),
                                                 "device extensions");
    if (!props) {
        return false;
    }

    result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, props);
    if (result != VK_SUCCESS) {
        Z_Free(props);
        return false;
    }

    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!strcmp(props[i].extensionName, ext)) {
            found = true;
            break;
        }
    }

    Z_Free(props);
    return found;
}

static bool VK_FindQueueFamily(VkPhysicalDevice device, uint32_t *out_index)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    if (!count)
        return false;

    VkQueueFamilyProperties *props = VK_AllocArray(count, sizeof(*props),
                                                   "queue families");
    if (!props) {
        return false;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;

        VkBool32 present_supported = VK_FALSE;
        VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, i,
                                                               vk_state.ctx.surface,
                                                               &present_supported);
        if (result != VK_SUCCESS) {
            Z_Free(props);
            return VK_Check(result, "vkGetPhysicalDeviceSurfaceSupportKHR");
        }
        if (present_supported) {
            *out_index = i;
            found = true;
            break;
        }
    }

    Z_Free(props);
    return found;
}

static bool VK_SelectPhysicalDevice(void)
{
    uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(vk_state.ctx.instance, &count, NULL);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkEnumeratePhysicalDevices");
    }
    if (count == 0) {
        Com_SetLastError("Vulkan: no physical devices reported");
        return false;
    }

    VkPhysicalDevice *devices = VK_AllocArray(count, sizeof(*devices),
                                              "physical devices");
    if (!devices) {
        return false;
    }

    result = vkEnumeratePhysicalDevices(vk_state.ctx.instance, &count, devices);
    if (result != VK_SUCCESS) {
        Z_Free(devices);
        return VK_Check(result, "vkEnumeratePhysicalDevices");
    }

    VkPhysicalDevice picked = VK_NULL_HANDLE;
    uint32_t queue_family = 0;

    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDevice device = devices[i];
        if (!VK_DeviceHasExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;

        uint32_t family = 0;
        if (!VK_FindQueueFamily(device, &family))
            continue;

        picked = device;
        queue_family = family;
        break;
    }

    if (picked == VK_NULL_HANDLE) {
        Z_Free(devices);
        Com_SetLastError("Vulkan: no compatible physical device found");
        return false;
    }

    vk_state.ctx.physical_device = picked;
    vk_state.ctx.graphics_queue_family = queue_family;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(picked, &props);
    Com_Printf("Vulkan device: %s\n", props.deviceName);

    Z_Free(devices);
    return true;
}

static bool VK_CreateDevice(void)
{
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk_state.ctx.graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char *extensions[2] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    uint32_t enabled_ext_count = 1;

    if (VK_DeviceHasExtension(vk_state.ctx.physical_device,
                              VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        extensions[enabled_ext_count++] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
    }

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = enabled_ext_count,
        .ppEnabledExtensionNames = extensions,
    };

    if (!VK_Check(vkCreateDevice(vk_state.ctx.physical_device, &create_info, NULL,
                                 &vk_state.ctx.device),
                  "vkCreateDevice")) {
        return false;
    }

    vkGetDeviceQueue(vk_state.ctx.device, vk_state.ctx.graphics_queue_family, 0,
                     &vk_state.ctx.graphics_queue);

    return true;
}

static bool VK_CreateCommandPool(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        Com_SetLastError("Vulkan: device unavailable for command pool creation");
        return false;
    }

    if (ctx->command_pool) {
        return true;
    }

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_queue_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };

    VkResult result = vkCreateCommandPool(ctx->device, &pool_info, NULL, &ctx->command_pool);
    return VK_Check(result, "vkCreateCommandPool");
}

static VkSurfaceFormatKHR VK_ChooseSurfaceFormat(const VkSurfaceFormatKHR *formats,
                                                 uint32_t count)
{
    if (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        VkSurfaceFormatKHR chosen = {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        };
        return chosen;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }

    // The native raster shaders currently produce legacy display-referred
    // code values. Prefer an UNORM target even on unusual surfaces; selecting
    // an SRGB image format here would apply a second transfer function.
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
            formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            return formats[i];
        }
    }

    Com_WPrintf("Vulkan: no compatible UNORM swapchain format; using format %d\n",
                (int)formats[0].format);

    return formats[0];
}

static VkPresentModeKHR VK_ChoosePresentMode(const VkPresentModeKHR *modes, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_FIFO_KHR)
            return modes[i];
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D VK_ChooseSwapExtent(const VkSurfaceCapabilitiesKHR *caps,
                                      uint32_t width, uint32_t height)
{
    if (caps->currentExtent.width != UINT32_MAX) {
        return caps->currentExtent;
    }

    VkExtent2D extent = {
        .width = VK_ClampU32(width, caps->minImageExtent.width, caps->maxImageExtent.width),
        .height = VK_ClampU32(height, caps->minImageExtent.height, caps->maxImageExtent.height),
    };

    return extent;
}

static void VK_DestroyLiquidSceneResources(vk_context_t *ctx)
{
    if (!ctx || !ctx->device) {
        return;
    }

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        VK_UI_DestroyExternalImageDescriptor(&frame->liquid_scene_descriptor_set);
        // VK_UI may already be shut down during final renderer teardown, in
        // which case its descriptor pool has destroyed the set on our behalf.
        frame->liquid_scene_descriptor_set = VK_NULL_HANDLE;
        if (frame->liquid_scene_view) {
            vkDestroyImageView(ctx->device, frame->liquid_scene_view, NULL);
            frame->liquid_scene_view = VK_NULL_HANDLE;
        }
        if (frame->liquid_scene_image) {
            vkDestroyImage(ctx->device, frame->liquid_scene_image, NULL);
            frame->liquid_scene_image = VK_NULL_HANDLE;
        }
        if (frame->liquid_scene_memory) {
            vkFreeMemory(ctx->device, frame->liquid_scene_memory, NULL);
            frame->liquid_scene_memory = VK_NULL_HANDLE;
        }
        frame->liquid_scene_initialized = false;
    }
}

static void VK_CreateLiquidSceneResources(vk_context_t *ctx,
                                          bool swapchain_supports_transfer_src,
                                          VkFormatFeatureFlags format_features)
{
    if (!ctx || !ctx->device || !ctx->swapchain.handle ||
        !swapchain_supports_transfer_src ||
        !(format_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        return;
    }

    VkResult result = VK_SUCCESS;
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = ctx->swapchain.format,
        .extent = {
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = ctx->swapchain.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        result = vkCreateImage(ctx->device, &image_info, NULL,
                               &frame->liquid_scene_image);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(ctx->device, frame->liquid_scene_image,
                                     &requirements);
        uint32_t memory_type = VK_FindMemoryType(
            ctx->physical_device, requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memory_type == UINT32_MAX) {
            result = VK_ERROR_FEATURE_NOT_PRESENT;
            goto unavailable;
        }
        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type,
        };
        result = vkAllocateMemory(ctx->device, &alloc_info, NULL,
                                  &frame->liquid_scene_memory);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        result = vkBindImageMemory(ctx->device, frame->liquid_scene_image,
                                   frame->liquid_scene_memory, 0);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }

        view_info.image = frame->liquid_scene_image;
        result = vkCreateImageView(ctx->device, &view_info, NULL,
                                   &frame->liquid_scene_view);
        if (result != VK_SUCCESS) {
            goto unavailable;
        }
        frame->liquid_scene_descriptor_set =
            VK_UI_CreateExternalImageDescriptor(
                frame->liquid_scene_view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!frame->liquid_scene_descriptor_set) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto unavailable;
        }
        frame->liquid_scene_initialized = false;
    }
    return;

unavailable:
    Com_WPrintf("Vulkan: native liquid scene copy unavailable (%s); refraction disabled.\n",
                VK_ResultString(result));
    VK_DestroyLiquidSceneResources(ctx);
}

static void VK_DestroySwapchain(vk_context_t *ctx)
{
    if (!ctx->device)
        return;

    VK_PostProcess_DestroySwapchainResources(ctx);
    VK_World_DestroySwapchainResources(ctx);
    VK_Entity_DestroySwapchainResources(ctx);
    VK_Debug_DestroySwapchainResources(ctx);
    VK_UI_DestroySwapchainResources(ctx);

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        if (frame->framebuffers) {
            for (uint32_t image = 0; image < ctx->swapchain.image_count; ++image) {
                if (frame->framebuffers[image]) {
                    vkDestroyFramebuffer(ctx->device, frame->framebuffers[image], NULL);
                }
            }
            Z_Free(frame->framebuffers);
            frame->framebuffers = NULL;
        }
        if (frame->depth_sample_view) {
            vkDestroyImageView(ctx->device, frame->depth_sample_view, NULL);
            frame->depth_sample_view = VK_NULL_HANDLE;
        }
        if (frame->depth_view) {
            vkDestroyImageView(ctx->device, frame->depth_view, NULL);
            frame->depth_view = VK_NULL_HANDLE;
        }
        if (frame->depth_image) {
            vkDestroyImage(ctx->device, frame->depth_image, NULL);
            frame->depth_image = VK_NULL_HANDLE;
        }
        if (frame->depth_memory) {
            vkFreeMemory(ctx->device, frame->depth_memory, NULL);
            frame->depth_memory = VK_NULL_HANDLE;
        }
        if (frame->in_flight_fence) {
            vkDestroyFence(ctx->device, frame->in_flight_fence, NULL);
            frame->in_flight_fence = VK_NULL_HANDLE;
        }
        if (frame->image_available) {
            vkDestroySemaphore(ctx->device, frame->image_available, NULL);
            frame->image_available = VK_NULL_HANDLE;
        }
        if (ctx->command_pool && frame->command_buffer) {
            vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1,
                                 &frame->command_buffer);
            frame->command_buffer = VK_NULL_HANDLE;
        }
        frame->submitted = false;
    }

    if (ctx->swapchain.render_finished) {
        for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
            if (ctx->swapchain.render_finished[i]) {
                vkDestroySemaphore(ctx->device,
                                   ctx->swapchain.render_finished[i], NULL);
            }
        }
        Z_Free(ctx->swapchain.render_finished);
        ctx->swapchain.render_finished = NULL;
    }

    vk_dof_supported = false;

    VK_DestroyLiquidSceneResources(ctx);

    if (ctx->render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);
        ctx->render_pass = VK_NULL_HANDLE;
    }
    if (ctx->overlay_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->overlay_render_pass, NULL);
        ctx->overlay_render_pass = VK_NULL_HANDLE;
    }
    if (ctx->liquid_render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->liquid_render_pass, NULL);
        ctx->liquid_render_pass = VK_NULL_HANDLE;
    }

    if (ctx->swapchain.views) {
        for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
            if (ctx->swapchain.views[i]) {
                vkDestroyImageView(ctx->device, ctx->swapchain.views[i], NULL);
            }
        }
        Z_Free(ctx->swapchain.views);
        ctx->swapchain.views = NULL;
    }

    if (ctx->swapchain.images) {
        Z_Free(ctx->swapchain.images);
        ctx->swapchain.images = NULL;
    }

    if (ctx->swapchain.handle) {
        vkDestroySwapchainKHR(ctx->device, ctx->swapchain.handle, NULL);
        ctx->swapchain.handle = VK_NULL_HANDLE;
    }

    if (ctx->swapchain.image_frame_slots) {
        Z_Free(ctx->swapchain.image_frame_slots);
        ctx->swapchain.image_frame_slots = NULL;
    }

    ctx->frame_count = 0;
    ctx->current_frame = 0;
    ctx->swapchain.image_count = 0;
    ctx->swapchain.format = VK_FORMAT_UNDEFINED;
    ctx->swapchain.depth_format = VK_FORMAT_UNDEFINED;
    memset(&ctx->swapchain.extent, 0, sizeof(ctx->swapchain.extent));
}

static bool VK_CreateSwapchain(uint32_t width, uint32_t height)
{
    vk_context_t *ctx = &vk_state.ctx;

    VkSurfaceCapabilitiesKHR caps;
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device,
                                                                ctx->surface, &caps);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    }

    uint32_t format_count = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface,
                                                  &format_count, NULL);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    }
    if (!format_count) {
        Com_SetLastError("Vulkan: no surface formats reported");
        return false;
    }

    VkSurfaceFormatKHR *formats = VK_AllocArray(format_count, sizeof(*formats),
                                                "surface formats");
    if (!formats) {
        return false;
    }

    result = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface,
                                                  &format_count, formats);
    if (result != VK_SUCCESS) {
        Z_Free(formats);
        return VK_Check(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    }
    if (!format_count) {
        Z_Free(formats);
        Com_SetLastError("Vulkan: no surface formats reported");
        return false;
    }

    VkSurfaceFormatKHR chosen_format = VK_ChooseSurfaceFormat(formats, format_count);
    Z_Free(formats);

    uint32_t present_count = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface,
                                                       &present_count, NULL);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
    if (!present_count) {
        Com_SetLastError("Vulkan: no present modes reported");
        return false;
    }

    VkPresentModeKHR *present_modes = VK_AllocArray(present_count, sizeof(*present_modes),
                                                    "present modes");
    if (!present_modes) {
        return false;
    }

    result = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface,
                                                       &present_count, present_modes);
    if (result != VK_SUCCESS) {
        Z_Free(present_modes);
        return VK_Check(result, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
    if (!present_count) {
        Z_Free(present_modes);
        Com_SetLastError("Vulkan: no present modes reported");
        return false;
    }

    VkPresentModeKHR present_mode = VK_ChoosePresentMode(present_modes, present_count);
    Z_Free(present_modes);

    VkExtent2D extent = VK_ChooseSwapExtent(&caps, width, height);
    if (!extent.width || !extent.height) {
        Com_SetLastError("Vulkan: swapchain extent is zero");
        return false;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    vk_screenshot_supported =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (vk_screenshot_supported) {
        image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkFormatProperties scene_format_properties;
    vkGetPhysicalDeviceFormatProperties(ctx->physical_device, chosen_format.format,
                                        &scene_format_properties);
    const bool swapchain_supports_transfer_src =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx->surface,
        .minImageCount = image_count,
        .imageFormat = chosen_format.format,
        .imageColorSpace = chosen_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = image_usage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    result = vkCreateSwapchainKHR(ctx->device, &create_info, NULL, &ctx->swapchain.handle);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkCreateSwapchainKHR");
    }

    result = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain.handle, &image_count, NULL);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkGetSwapchainImagesKHR");
    }
    if (!image_count) {
        Com_SetLastError("Vulkan: no swapchain images reported");
        VK_DestroySwapchain(ctx);
        return false;
    }

    ctx->swapchain.images = VK_AllocArray(image_count, sizeof(*ctx->swapchain.images),
                                          "swapchain images");
    if (!ctx->swapchain.images) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    result = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain.handle, &image_count,
                                     ctx->swapchain.images);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkGetSwapchainImagesKHR");
    }
    if (!image_count) {
        Com_SetLastError("Vulkan: no swapchain images reported");
        VK_DestroySwapchain(ctx);
        return false;
    }

    ctx->swapchain.image_count = image_count;
    ctx->swapchain.format = chosen_format.format;
    ctx->swapchain.extent = extent;
    ctx->frame_count = min((uint32_t)VK_MAX_FRAMES_IN_FLIGHT, image_count);
    if (!ctx->frame_count) {
        Com_SetLastError("Vulkan: swapchain has no usable frame contexts");
        VK_DestroySwapchain(ctx);
        return false;
    }

    VK_CreateLiquidSceneResources(
        ctx, swapchain_supports_transfer_src,
        scene_format_properties.optimalTilingFeatures);

    ctx->swapchain.views = VK_AllocArray(image_count, sizeof(*ctx->swapchain.views),
                                         "swapchain image views");
    if (!ctx->swapchain.views) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx->swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx->swapchain.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        result = vkCreateImageView(ctx->device, &view_info, NULL, &ctx->swapchain.views[i]);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateImageView");
        }
    }

    ctx->swapchain.depth_format = VK_ChooseDepthFormat(ctx->physical_device);
    if (ctx->swapchain.depth_format == VK_FORMAT_UNDEFINED) {
        Com_SetLastError("Vulkan: no supported depth format found");
        VK_DestroySwapchain(ctx);
        return false;
    }
    vk_dof_supported = VK_DepthFormatSupportsSampling(
        ctx->physical_device, ctx->swapchain.depth_format);
    if (!vk_dof_supported) {
        Com_WPrintf("Vulkan: depth format is not sampleable; depth-aware DOF disabled.\n");
    }

    VkImageCreateInfo depth_image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = ctx->swapchain.depth_format,
        .extent = {
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 (vk_dof_supported ? VK_IMAGE_USAGE_SAMPLED_BIT : 0),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (ctx->swapchain.depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        ctx->swapchain.depth_format == VK_FORMAT_D24_UNORM_S8_UINT) {
        depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = ctx->swapchain.depth_format,
        .subresourceRange = {
            .aspectMask = depth_aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        result = vkCreateImage(ctx->device, &depth_image_info, NULL,
                               &frame->depth_image);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateImage(depth)");
        }

        VkMemoryRequirements depth_requirements;
        vkGetImageMemoryRequirements(ctx->device, frame->depth_image,
                                     &depth_requirements);
        uint32_t depth_memory_type = VK_FindMemoryType(
            ctx->physical_device, depth_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (depth_memory_type == UINT32_MAX) {
            Com_SetLastError("Vulkan: no suitable depth memory type found");
            VK_DestroySwapchain(ctx);
            return false;
        }

        VkMemoryAllocateInfo depth_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = depth_requirements.size,
            .memoryTypeIndex = depth_memory_type,
        };
        result = vkAllocateMemory(ctx->device, &depth_alloc_info, NULL,
                                  &frame->depth_memory);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkAllocateMemory(depth)");
        }
        result = vkBindImageMemory(ctx->device, frame->depth_image,
                                   frame->depth_memory, 0);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkBindImageMemory(depth)");
        }

        depth_view_info.image = frame->depth_image;
        result = vkCreateImageView(ctx->device, &depth_view_info, NULL,
                                   &frame->depth_view);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateImageView(depth)");
        }
        if (vk_dof_supported) {
            VkImageViewCreateInfo sample_view_info = depth_view_info;
            sample_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            result = vkCreateImageView(ctx->device, &sample_view_info, NULL,
                                       &frame->depth_sample_view);
            if (result != VK_SUCCESS) {
                Com_WPrintf("Vulkan: depth sample view unavailable (%s); depth-aware DOF disabled.\n",
                            VK_ResultString(result));
                vk_dof_supported = false;
            }
        }
    }

    VkAttachmentDescription attachments[2] = {
        {
            .format = ctx->swapchain.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        {
            .format = ctx->swapchain.depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            // The native liquid phase reuses opaque depth after its scene
            // copy, so preserve it even on frames without transparent water.
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_DepthFormatHasStencil(ctx->swapchain.depth_format)
                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depth_ref = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };

    VkSubpassDependency dependencies[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
        },
    };

    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = q_countof(attachments),
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = q_countof(dependencies),
        .pDependencies = dependencies,
    };

    result = vkCreateRenderPass(ctx->device, &render_pass_info, NULL, &ctx->render_pass);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkCreateRenderPass");
    }

    // A compatible load pass lets native no-world entity previews overlay
    // the completed RmlUi shell without clearing its color attachment. Depth
    // is cleared because the menu preview is an independent scene.
    VkAttachmentDescription overlay_attachments[2] = {
        attachments[0],
        attachments[1],
    };
    overlay_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    overlay_attachments[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    overlay_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    overlay_attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkRenderPassCreateInfo overlay_render_pass_info = render_pass_info;
    overlay_render_pass_info.pAttachments = overlay_attachments;
    result = vkCreateRenderPass(ctx->device, &overlay_render_pass_info, NULL,
                                &ctx->overlay_render_pass);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkCreateRenderPass(preview overlay)");
    }

    // Transparent warp surfaces run after the opaque scene has been copied to
    // a sampled image. This pass loads both color and depth, retaining correct
    // depth rejection without feedback-looping the swapchain image.
    VkAttachmentDescription liquid_attachments[2] = {
        attachments[0],
        attachments[1],
    };
    liquid_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    liquid_attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    liquid_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    liquid_attachments[1].initialLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkRenderPassCreateInfo liquid_render_pass_info = render_pass_info;
    liquid_render_pass_info.pAttachments = liquid_attachments;
    // Keep this pass compatible with the base scene pipelines. Explicit image
    // barriers bracket the scene copy and the preserved depth attachment.
    liquid_render_pass_info.pDependencies = dependencies;
    result = vkCreateRenderPass(ctx->device, &liquid_render_pass_info, NULL,
                                &ctx->liquid_render_pass);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkCreateRenderPass(liquid overlay)");
    }

    for (uint32_t frame_index = 0; frame_index < ctx->frame_count; ++frame_index) {
        vk_frame_context_t *frame = &ctx->frames[frame_index];
        frame->framebuffers = VK_AllocArray(image_count,
                                            sizeof(*frame->framebuffers),
                                            "frame-context swapchain framebuffers");
        if (!frame->framebuffers) {
            VK_DestroySwapchain(ctx);
            return false;
        }
        for (uint32_t image_index = 0; image_index < image_count; ++image_index) {
            VkImageView framebuffer_attachments[] = {
                ctx->swapchain.views[image_index],
                frame->depth_view,
            };
            VkFramebufferCreateInfo framebuffer_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = ctx->render_pass,
                .attachmentCount = q_countof(framebuffer_attachments),
                .pAttachments = framebuffer_attachments,
                .width = ctx->swapchain.extent.width,
                .height = ctx->swapchain.extent.height,
                .layers = 1,
            };
            result = vkCreateFramebuffer(ctx->device, &framebuffer_info, NULL,
                                         &frame->framebuffers[image_index]);
            if (result != VK_SUCCESS) {
                VK_DestroySwapchain(ctx);
                return VK_Check(result, "vkCreateFramebuffer");
            }

        }
    }

    if (!ctx->command_pool) {
        Com_SetLastError("Vulkan: command pool unavailable during swapchain creation");
        VK_DestroySwapchain(ctx);
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = ctx->frame_count,
    };

    VkCommandBuffer command_buffers[VK_MAX_FRAMES_IN_FLIGHT] = { VK_NULL_HANDLE };
    result = vkAllocateCommandBuffers(ctx->device, &alloc_info, command_buffers);
    if (result != VK_SUCCESS) {
        VK_DestroySwapchain(ctx);
        return VK_Check(result, "vkAllocateCommandBuffers");
    }
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        ctx->frames[i].command_buffer = command_buffers[i];
    }

    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    // A presentation wait may outlive the queue-submission fence. Associate
    // the render-finished semaphore with the acquired swapchain image so it is
    // not reused until presentation has released that image.
    ctx->swapchain.render_finished = VK_AllocArray(
        image_count, sizeof(*ctx->swapchain.render_finished),
        "render-finished semaphores");
    if (!ctx->swapchain.render_finished) {
        VK_DestroySwapchain(ctx);
        return false;
    }
    for (uint32_t i = 0; i < image_count; ++i) {
        result = vkCreateSemaphore(ctx->device, &sem_info, NULL,
                                   &ctx->swapchain.render_finished[i]);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateSemaphore(render finished)");
        }
    }

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        vk_frame_context_t *frame = &ctx->frames[i];
        result = vkCreateSemaphore(ctx->device, &sem_info, NULL,
                                   &frame->image_available);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateSemaphore(image available)");
        }
        result = vkCreateFence(ctx->device, &fence_info, NULL,
                               &frame->in_flight_fence);
        if (result != VK_SUCCESS) {
            VK_DestroySwapchain(ctx);
            return VK_Check(result, "vkCreateFence");
        }
    }

    ctx->swapchain.image_frame_slots = VK_AllocArray(
        image_count, sizeof(*ctx->swapchain.image_frame_slots),
        "swapchain image frame ownership");
    if (!ctx->swapchain.image_frame_slots) {
        VK_DestroySwapchain(ctx);
        return false;
    }
    for (uint32_t i = 0; i < image_count; ++i) {
        ctx->swapchain.image_frame_slots[i] = VK_INVALID_FRAME_SLOT;
    }
    ctx->current_frame = 0;
    return true;
}

static bool VK_RecreateSwapchain(uint32_t width, uint32_t height)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (!ctx->device)
        return false;

    vkDeviceWaitIdle(ctx->device);
    VK_PostProcess_DestroySwapchainResources(ctx);
    VK_DestroySwapchain(ctx);

    if (!VK_CreateSwapchain(width, height)) {
        return false;
    }

    if (!VK_World_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    if (!VK_Entity_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    if (!VK_Debug_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    if (!VK_UI_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    if (!VK_PostProcess_CreateSwapchainResources(ctx)) {
        VK_DestroySwapchain(ctx);
        return false;
    }

    VK_Debug_UpdateCapabilities(
        vk_screenshot_supported,
        VK_DepthFormatHasStencil(ctx->swapchain.depth_format),
        vk_dof_supported);

    vk_state.swapchain_dirty = false;
    return true;
}

static bool VK_WaitForSubmittedFrames(vk_context_t *ctx, const char *what);

static void VK_Screenshot_DestroyBuffer(void)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (vk_screenshot_buffer) {
        vkDestroyBuffer(ctx->device, vk_screenshot_buffer, NULL);
        vk_screenshot_buffer = VK_NULL_HANDLE;
    }
    if (vk_screenshot_memory) {
        vkFreeMemory(ctx->device, vk_screenshot_memory, NULL);
        vk_screenshot_memory = VK_NULL_HANDLE;
    }
    vk_screenshot_capacity = 0;
}

static bool VK_Screenshot_EnsureBuffer(VkDeviceSize size)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (!size) {
        Com_SetLastError("Vulkan: zero-sized screenshot readback buffer");
        return false;
    }

    if (vk_screenshot_buffer && vk_screenshot_capacity >= size) {
        return true;
    }

    VK_Screenshot_DestroyBuffer();

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VK_Check(vkCreateBuffer(ctx->device, &buffer_info, NULL,
                                 &vk_screenshot_buffer),
                  "vkCreateBuffer(screenshot)")) {
        return false;
    }

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(ctx->device, vk_screenshot_buffer, &reqs);

    uint32_t memory_type = VK_FindMemoryType(ctx->physical_device,
                                             reqs.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        Com_EPrintf("Vulkan screenshot: no host-visible memory type\n");
        VK_Screenshot_DestroyBuffer();
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = reqs.size,
        .memoryTypeIndex = memory_type,
    };
    if (!VK_Check(vkAllocateMemory(ctx->device, &alloc_info, NULL,
                                   &vk_screenshot_memory),
                  "vkAllocateMemory(screenshot)")) {
        VK_Screenshot_DestroyBuffer();
        return false;
    }
    if (!VK_Check(vkBindBufferMemory(ctx->device, vk_screenshot_buffer,
                                     vk_screenshot_memory, 0),
                  "vkBindBufferMemory(screenshot)")) {
        VK_Screenshot_DestroyBuffer();
        return false;
    }

    vk_screenshot_capacity = size;
    return true;
}

static void VK_Screenshot_RecordCopy(VkCommandBuffer cmd, uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    VkImage image = ctx->swapchain.images[image_index];

    VkImageMemoryBarrier to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_transfer);

    VkBufferImageCopy region = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = {
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .depth = 1,
        },
    };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vk_screenshot_buffer, 1, &region);

    VkBufferMemoryBarrier host_read = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = vk_screenshot_buffer,
        .size = VK_WHOLE_SIZE,
    };
    VkImageMemoryBarrier to_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT |
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, NULL, 1, &host_read, 1, &to_present);
}

static void VK_Screenshot_Complete(void)
{
    vk_context_t *ctx = &vk_state.ctx;

    vk_screenshot_armed = false;
    vk_screenshot_pending = false;

    if (!VK_WaitForSubmittedFrames(ctx, "vkWaitForFences(screenshot)")) {
        return;
    }

    uint32_t width = ctx->swapchain.extent.width;
    uint32_t height = ctx->swapchain.extent.height;
    size_t rgba_bytes = 0;
    size_t rgb_bytes = 0;
    size_t row_stride = 0;
    if (!VK_ImageBytes(width, height, 4, &rgba_bytes, "screenshot readback") ||
        !VK_ImageBytes(width, height, 3, &rgb_bytes, "screenshot RGB conversion") ||
        !VK_ArrayBytes((size_t)width, 3, &row_stride, "screenshot row stride")) {
        Com_EPrintf("Vulkan screenshot: image dimensions are too large\n");
        return;
    }
    if (width > (uint32_t)INT_MAX || height > (uint32_t)INT_MAX ||
        row_stride > (size_t)INT_MAX) {
        Com_EPrintf("Vulkan screenshot: image dimensions exceed PNG writer limits\n");
        return;
    }

    size_t pixel_count = rgba_bytes / 4;

    bool swap_red_blue;
    switch (ctx->swapchain.format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        swap_red_blue = true;
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        swap_red_blue = false;
        break;
    default:
        Com_EPrintf("Vulkan screenshot: unsupported swapchain format %d\n",
                    ctx->swapchain.format);
        return;
    }

    byte *rgb = Z_TagMalloc(rgb_bytes, TAG_RENDERER);
    if (!rgb) {
        Com_EPrintf("Vulkan screenshot: out of memory for RGB conversion\n");
        return;
    }

    void *mapped = NULL;
    if (!VK_Check(vkMapMemory(ctx->device, vk_screenshot_memory, 0,
                              VK_WHOLE_SIZE, 0, &mapped),
                  "vkMapMemory(screenshot)")) {
        Z_Free(rgb);
        return;
    }

    const byte *src = mapped;
    byte *dst = rgb;
    for (size_t i = 0; i < pixel_count; i++, src += 4, dst += 3) {
        if (swap_red_blue) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
        } else {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }
    vkUnmapMemory(ctx->device, vk_screenshot_memory);

    int ok;
    if (vk_screenshot_tga) {
        const int previous_tga_rle = stbi_write_tga_with_rle;
        stbi_write_tga_with_rle = 0;
        ok = stbi_write_tga(vk_screenshot_path, (int)width, (int)height, 3, rgb);
        stbi_write_tga_with_rle = previous_tga_rle;
    } else {
        ok = stbi_write_png(vk_screenshot_path, (int)width, (int)height, 3,
                            rgb, (int)row_stride);
    }
    Z_Free(rgb);

    if (!ok) {
        Com_EPrintf("Couldn't write %s\n", vk_screenshot_path);
        return;
    }

    Com_Printf("Wrote %s\n", vk_screenshot_path);
}

static void VK_RequestScreenshot(const char *extension, bool tga)
{
    if (!vk_state.initialized || !vk_state.ctx.swapchain.handle) {
        Com_EPrintf("No Vulkan swapchain available for screenshot.\n");
        return;
    }
    if (!vk_screenshot_supported) {
        Com_EPrintf("Vulkan swapchain does not support readback.\n");
        return;
    }
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [name]\n", Cmd_Argv(0));
        return;
    }

    char name[MAX_OSPATH];
    if (Cmd_Argc() > 1) {
        if (FS_NormalizePathBuffer(name, Cmd_Argv(1), sizeof(name)) >= sizeof(name)) {
            Com_EPrintf("Screenshot name too long.\n");
            return;
        }
        FS_CleanupPath(name);
    } else {
        Q_snprintf(name, sizeof(name), "worr_vk_%llu",
                   (unsigned long long)time(NULL));
    }

    if (vk_screenshot_dir && vk_screenshot_dir->string[0]) {
        char directory[MAX_OSPATH];
        if (Q_strlcpy(directory, vk_screenshot_dir->string,
                      sizeof(directory)) >= sizeof(directory)) {
            Com_EPrintf("Screenshot directory too long.\n");
            return;
        }
        for (char *cursor = directory; *cursor; ++cursor) {
            if (*cursor == '\\') {
                *cursor = '/';
            }
        }
        if (Q_snprintf(vk_screenshot_path, sizeof(vk_screenshot_path),
                       "%s/%s%s", directory, name, extension) >=
            sizeof(vk_screenshot_path)) {
            Com_EPrintf("Screenshot path too long.\n");
            return;
        }
    } else if (Q_snprintf(vk_screenshot_path, sizeof(vk_screenshot_path),
                          "%s/screenshots/%s%s", fs_gamedir, name,
                          extension) >= sizeof(vk_screenshot_path)) {
        Com_EPrintf("Screenshot path too long.\n");
        return;
    }

    int ret = FS_CreatePath(vk_screenshot_path);
    if (ret < 0) {
        Com_EPrintf("Couldn't create path for %s: %s\n", vk_screenshot_path,
                    Q_ErrorString(ret));
        return;
    }

    vk_screenshot_tga = tga;
    vk_screenshot_pending = true;
}

static void VK_ScreenShotPNG_f(void)
{
    VK_RequestScreenshot(".png", false);
}

static void VK_ScreenShotTGA_f(void)
{
    VK_RequestScreenshot(".tga", true);
}

static void VK_ImageBarrier(VkCommandBuffer cmd, VkImage image,
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
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_DepthToShaderRead(VkCommandBuffer cmd,
                                 const vk_frame_context_t *frame)
{
    if (!cmd || !frame || !frame->depth_image || !frame->depth_sample_view) {
        return;
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame->depth_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format)
                    ? VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_DepthToAttachment(VkCommandBuffer cmd,
                                 const vk_frame_context_t *frame)
{
    if (!cmd || !frame || !frame->depth_image || !frame->depth_sample_view) {
        return;
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame->depth_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format)
                    ? VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_DepthAttachmentBarrier(VkCommandBuffer cmd,
                                      const vk_frame_context_t *frame)
{
    if (!cmd || !frame || !frame->depth_image) {
        return;
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame->depth_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format)
                    ? VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         0, NULL, 0, NULL, 1, &barrier);
}

static void VK_SceneCopy_Record(VkCommandBuffer cmd, uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    if (!frame->liquid_scene_image ||
        image_index >= ctx->swapchain.image_count) {
        return;
    }

    VkImage swap_image = ctx->swapchain.images[image_index];
    VkImage scene_image = frame->liquid_scene_image;
    VK_ImageBarrier(
        cmd, swap_image,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VK_ImageBarrier(
        cmd, scene_image,
        frame->liquid_scene_initialized
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        frame->liquid_scene_initialized
            ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        frame->liquid_scene_initialized
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy copy = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .extent = {
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .depth = 1,
        },
    };
    vkCmdCopyImage(cmd, swap_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   scene_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VK_ImageBarrier(
        cmd, scene_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VK_ImageBarrier(
        cmd, swap_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    frame->liquid_scene_initialized = true;
}

static bool VK_RecordCommandBuffer(VkCommandBuffer cmd, uint32_t image_index)
{
    vk_context_t *ctx = &vk_state.ctx;
    vk_frame_context_t *frame = &ctx->frames[ctx->current_frame];
    if (!frame->framebuffers || image_index >= ctx->swapchain.image_count ||
        !frame->framebuffers[image_index]) {
        Com_SetLastError("Vulkan: frame-context framebuffer unavailable");
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    VkResult result = vkBeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        return VK_Check(result, "vkBeginCommandBuffer");
    }
    VK_Debug_BeginGpuFrame(cmd, ctx->current_frame);

    VkClearValue clear_values[2] = {
        {
            .color = { { 0.0f, 0.0f, 0.0f, 1.0f } },
        },
        {
            .depthStencil = { 1.0f, 0 },
        },
    };

    VkRenderPassBeginInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = ctx->render_pass,
        .framebuffer = frame->framebuffers[image_index],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = ctx->swapchain.extent,
        },
        .clearValueCount = q_countof(clear_values),
        .pClearValues = clear_values,
    };

    // Current-frame dynamic streams are copied into device-local vertex/index
    // storage before any pass can bind them.
    VK_Entity_RecordUploads(cmd);
    VK_Shadow_RecordUploads(cmd);
    VK_UI_RecordUploads(cmd);
    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_UPLOAD);
    VK_Shadow_Record(cmd);
    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_SHADOW);
    VK_Entity_ResetFlareQueries(cmd);

    // Native menu previews are submitted after RmlUi has queued its draw
    // list. Overlay only the no-world subview entity pass after the UI so the
    // opaque shell background cannot hide it; the preserved entity scissor
    // keeps the model inside the authored preview surface.
    const bool entity_overlay = VK_Entity_IsNoWorldSubview();

    const bool depth_dof = !entity_overlay && VK_PostProcess_UsesDof();
    const bool draw_world = !vk_draw_world || vk_draw_world->integer;
    const bool liquid_refraction = !entity_overlay && draw_world &&
        ctx->liquid_render_pass && frame->liquid_scene_image &&
        frame->liquid_scene_descriptor_set &&
        VK_World_UsesRefraction();
    const bool postprocess_composite = VK_PostProcess_UsesCompositePass();
    const bool crt_postprocess = VK_PostProcess_UsesCrtPass();
    const bool final_postprocess = !entity_overlay && ctx->liquid_render_pass &&
        frame->liquid_scene_image && frame->liquid_scene_descriptor_set &&
        VK_PostProcess_UsesFinalPass();

    vkCmdBeginRenderPass(cmd, &render_info, VK_SUBPASS_CONTENTS_INLINE);
    if (draw_world) {
        if (liquid_refraction) {
            VK_World_RecordOpaque(cmd, &ctx->swapchain.extent);
        } else {
            VK_World_Record(cmd, &ctx->swapchain.extent);
        }
    }
    if (!entity_overlay && (!vk_draw_entities || vk_draw_entities->integer)) {
        if (liquid_refraction) {
            VK_Entity_RecordBeforeLiquid(cmd, &ctx->swapchain.extent);
        } else {
            VK_Entity_Record(cmd, &ctx->swapchain.extent);
        }
    }
    if (!liquid_refraction) {
        VK_Debug_Record(cmd, &ctx->swapchain.extent);
        if (!final_postprocess && (!vk_draw_ui || vk_draw_ui->integer)) {
            VK_UI_Record(cmd, &ctx->swapchain.extent);
        }
    }
    vkCmdEndRenderPass(cmd);

    if (liquid_refraction) {
        VK_SceneCopy_Record(cmd, image_index);
        VK_DepthAttachmentBarrier(cmd, frame);

        VkRenderPassBeginInfo liquid_info = render_info;
        liquid_info.renderPass = ctx->liquid_render_pass;
        vkCmdBeginRenderPass(cmd, &liquid_info, VK_SUBPASS_CONTENTS_INLINE);
        VK_World_RecordAlpha(cmd, &ctx->swapchain.extent,
                             frame->liquid_scene_descriptor_set);
        if (!vk_draw_entities || vk_draw_entities->integer) {
            VK_Entity_RecordAfterLiquid(cmd, &ctx->swapchain.extent);
        }
        VK_Debug_Record(cmd, &ctx->swapchain.extent);
        if (!final_postprocess && (!vk_draw_ui || vk_draw_ui->integer)) {
            VK_UI_Record(cmd, &ctx->swapchain.extent);
        }
        vkCmdEndRenderPass(cmd);
    }

    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_SCENE);

    if (final_postprocess) {
        // Copy the completed 3D scene (including any native liquid pass) and
        // overwrite the swapchain through native Vulkan composition. UI is
        // recorded only after the scene passes, so HUD/menu text remains sharp.
        VK_SceneCopy_Record(cmd, image_index);
        if (depth_dof) {
            VK_DepthToShaderRead(cmd, frame);
            VK_PostProcess_RecordDof(cmd);
            VK_DepthToAttachment(cmd, frame);
        }
        VK_PostProcess_RecordBloom(cmd);

        if (postprocess_composite) {
            VkRenderPassBeginInfo final_info = render_info;
            final_info.renderPass = ctx->liquid_render_pass;
            vkCmdBeginRenderPass(cmd, &final_info, VK_SUBPASS_CONTENTS_INLINE);
            VK_PostProcess_RecordFinal(
                cmd, &ctx->swapchain.extent,
                frame->liquid_scene_descriptor_set);
            vkCmdEndRenderPass(cmd);
        }

        if (crt_postprocess) {
            // When a base composite ran, copy its result before the CRT pass;
            // for CRT-only frames the original scene copy is already the input.
            if (postprocess_composite) {
                VK_SceneCopy_Record(cmd, image_index);
            }
            VkRenderPassBeginInfo crt_info = render_info;
            crt_info.renderPass = ctx->liquid_render_pass;
            vkCmdBeginRenderPass(cmd, &crt_info, VK_SUBPASS_CONTENTS_INLINE);
            VK_PostProcess_RecordCrt(
                cmd, &ctx->swapchain.extent,
                frame->liquid_scene_descriptor_set);
            vkCmdEndRenderPass(cmd);
        }
    }

    if (final_postprocess) {
        VkRenderPassBeginInfo ui_overlay_info = render_info;
        ui_overlay_info.renderPass = ctx->overlay_render_pass;
        vkCmdBeginRenderPass(cmd, &ui_overlay_info, VK_SUBPASS_CONTENTS_INLINE);
        if (!vk_draw_ui || vk_draw_ui->integer) {
            VK_UI_Record(cmd, &ctx->swapchain.extent);
        }
        vkCmdEndRenderPass(cmd);
    }

    if (entity_overlay && (!vk_draw_entities || vk_draw_entities->integer)) {
        VkRenderPassBeginInfo overlay_info = render_info;
        overlay_info.renderPass = ctx->overlay_render_pass;
        vkCmdBeginRenderPass(cmd, &overlay_info, VK_SUBPASS_CONTENTS_INLINE);
        VK_Entity_Record(cmd, &ctx->swapchain.extent);
        vkCmdEndRenderPass(cmd);
    }

    VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_POSTPROCESS);

    if (vk_screenshot_armed) {
        VK_Screenshot_RecordCopy(cmd, image_index);
    }

    VK_Debug_EndGpuFrame(cmd);

    result = vkEndCommandBuffer(cmd);
    return VK_Check(result, "vkEndCommandBuffer");
}

static bool VK_WaitForFrame(vk_context_t *ctx, uint32_t frame_index,
                            const char *what)
{
    if (!ctx || frame_index >= ctx->frame_count) {
        Com_SetLastError("Vulkan: invalid frame context");
        return false;
    }

    vk_frame_context_t *frame = &ctx->frames[frame_index];
    if (!frame->submitted) {
        return true;
    }

    VkResult result = vkWaitForFences(ctx->device, 1, &frame->in_flight_fence, VK_TRUE,
                                      UINT64_MAX);
    if (result == VK_SUCCESS) {
        VK_Debug_ResolveGpuFrame(frame_index);
        frame->submitted = false;
        return true;
    }

    frame->submitted = false;
    return VK_Check(result, what);
}

static bool VK_WaitForSubmittedFrames(vk_context_t *ctx, const char *what)
{
    if (!ctx) {
        return false;
    }
    for (uint32_t i = 0; i < ctx->frame_count; ++i) {
        if (!VK_WaitForFrame(ctx, i, what)) {
            return false;
        }
    }
    return true;
}

static bool VK_DrawFrame(void)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (!ctx->swapchain.handle || !ctx->frame_count ||
        ctx->current_frame >= ctx->frame_count) {
        return false;
    }

    const uint32_t frame_index = ctx->current_frame;
    vk_frame_context_t *frame = &ctx->frames[frame_index];
    VkResult result = VK_SUCCESS;
    if (VK_PostProcess_NeedsSafeResourceUpdate() &&
        !VK_WaitForSubmittedFrames(ctx,
                                   "vkWaitForFences(bloom resource rebuild)")) {
        return false;
    }
    VK_PostProcess_PrepareFrame();

    uint32_t image_index = 0;
    result = vkAcquireNextImageKHR(ctx->device, ctx->swapchain.handle, UINT64_MAX,
                                   frame->image_available, VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vk_state.swapchain_dirty = true;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return VK_Check(result, "vkAcquireNextImageKHR");
    }
    if (image_index >= ctx->swapchain.image_count ||
        !ctx->swapchain.image_frame_slots) {
        Com_SetLastError("Vulkan: acquired image has no frame ownership state");
        return false;
    }
    uint32_t image_frame = ctx->swapchain.image_frame_slots[image_index];
    if (image_frame != VK_INVALID_FRAME_SLOT && image_frame != frame_index &&
        !VK_WaitForFrame(ctx, image_frame,
                         "vkWaitForFences(acquired swapchain image)")) {
        return false;
    }

    vk_screenshot_armed = false;
    if (vk_screenshot_pending) {
        size_t needed_bytes = 0;
        if (vk_screenshot_supported &&
            VK_ImageBytes(ctx->swapchain.extent.width, ctx->swapchain.extent.height,
                          4, &needed_bytes, "screenshot readback") &&
            VK_Screenshot_EnsureBuffer((VkDeviceSize)needed_bytes)) {
            vk_screenshot_armed = true;
        } else {
            Com_EPrintf("Vulkan screenshot: readback unavailable\n");
            vk_screenshot_pending = false;
        }
    }

    VkCommandBuffer cmd = frame->command_buffer;
    VkSemaphore render_finished = ctx->swapchain.render_finished[image_index];
    vkResetCommandBuffer(cmd, 0);
    if (!VK_RecordCommandBuffer(cmd, image_index))
        return false;

    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->image_available,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished,
    };

    result = vkResetFences(ctx->device, 1, &frame->in_flight_fence);
    if (result != VK_SUCCESS) {
        frame->submitted = false;
        return VK_Check(result, "vkResetFences");
    }

    result = vkQueueSubmit(ctx->graphics_queue, 1, &submit_info,
                           frame->in_flight_fence);
    if (result != VK_SUCCESS) {
        frame->submitted = false;
        return VK_Check(result, "vkQueueSubmit");
    }
    frame->submitted = true;
    ctx->swapchain.image_frame_slots[image_index] = frame_index;
    VK_Debug_MarkGpuFrameSubmitted(frame_index);
    ctx->current_frame = (frame_index + 1) % ctx->frame_count;

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_finished,
        .swapchainCount = 1,
        .pSwapchains = &ctx->swapchain.handle,
        .pImageIndices = &image_index,
    };

    result = vkQueuePresentKHR(ctx->graphics_queue, &present_info);

    if (vk_screenshot_armed) {
        // The submitted command buffer already executed the readback copy;
        // finish the file write even if the present was suboptimal.
        VK_Screenshot_Complete();
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        vk_state.swapchain_dirty = true;
        return false;
    }

    return VK_Check(result, "vkQueuePresentKHR");
}

static void VK_DestroyContext(void)
{
    vk_context_t *ctx = &vk_state.ctx;

    if (ctx->device) {
        vkDeviceWaitIdle(ctx->device);
    }

    VK_Debug_Shutdown(ctx);
    VK_Entity_Shutdown(ctx);
    VK_PostProcess_Shutdown(ctx);
    VK_World_Shutdown(ctx);
    VK_UI_Shutdown(ctx);
    VK_Screenshot_DestroyBuffer();
    vk_screenshot_armed = false;
    vk_screenshot_pending = false;
    VK_DestroySwapchain(ctx);

    if (ctx->command_pool) {
        vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);
        ctx->command_pool = VK_NULL_HANDLE;
    }

    if (ctx->device) {
        vkDestroyDevice(ctx->device, NULL);
        ctx->device = VK_NULL_HANDLE;
    }

    VK_DestroySurfaceHandle(ctx);

    if (ctx->instance) {
        vkDestroyInstance(ctx->instance, NULL);
        ctx->instance = VK_NULL_HANDLE;
    }

    ctx->physical_device = VK_NULL_HANDLE;
    ctx->graphics_queue = VK_NULL_HANDLE;
    ctx->graphics_queue_family = 0;
}

static void VK_ShadowDump_f(void)
{
    shadow_frontend_policy_t policy;
    ShadowFrontend_PolicyFromCvars(&vk_shadow_frontend_cvars, &policy);
    int focus = Cmd_Argc() > 1 ? Q_atoi(Cmd_Argv(1)) : -1;
    ShadowFrontend_Dump(&vk_shadow_frontend, &policy, &vk_shadow_backend_ops,
                        focus);
}

bool R_Init(bool total)
{
    if (!vk_draw_world) {
        vk_draw_world = Cvar_Get("vk_draw_world", "1", 0);
    }
    if (!vk_draw_entities) {
        vk_draw_entities = Cvar_Get("vk_draw_entities", "1", 0);
    }
    if (!vk_draw_ui) {
        vk_draw_ui = Cvar_Get("vk_draw_ui", "1", 0);
    }
    if (!vk_damageblend_frac) {
        vk_damageblend_frac = Cvar_Get("gl_damageblend_frac", "0.2", 0);
    }
    if (!vk_screenshot_dir) {
        vk_screenshot_dir = Cvar_Get("r_screenshot_dir", "", CVAR_NOARCHIVE);
    }
    if (!vk_shadow_frontend_cvars_registered) {
        ShadowFrontend_RegisterCvars(&vk_shadow_frontend_cvars, "vk");
        Cmd_AddCommand("r_shadow_dump", VK_ShadowDump_f);
        Cmd_AddCommand("vk_shadow_dump", VK_ShadowDump_f);
        Cmd_AddCommand("screenshot", VK_ScreenShotPNG_f);
        Cmd_AddCommand("screenshotpng", VK_ScreenShotPNG_f);
        Cmd_AddCommand("screenshottga", VK_ScreenShotTGA_f);
        vk_shadow_frontend_cvars_registered = true;
    }

    if (!total) {
        vk_state.swapchain_dirty = true;
        return vk_state.initialized;
    }

    if (total && !vk_state.initialized) {
        VK_InitPalette();
        ShadowFrontend_Init(&vk_shadow_frontend);
    }

    Com_Printf("------- VK_Init -------\n");
    Com_Printf("Using video driver: %s\n", vid->name);

    if (!vid->init()) {
        return false;
    }

    vid_native_window_t native;
    if (!VK_QueryNativeWindow(&native)) {
        vid->shutdown();
        return false;
    }

    vk_state.native_window = native;

    if (!VK_CreateInstance()) {
        vid->shutdown();
        return false;
    }

    if (!VK_CreateSurface()) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_SelectPhysicalDevice()) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_CreateDevice()) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_CreateCommandPool(&vk_state.ctx)) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_Shadow_Init(&vk_state.ctx)) {
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_UI_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_PostProcess_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_World_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_Entity_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    if (!VK_Debug_Init(&vk_state.ctx)) {
        VK_Shadow_Shutdown(&vk_state.ctx);
        VK_DestroyContext();
        vid->shutdown();
        return false;
    }

    vk_state.initialized = true;
    vk_state.swapchain_dirty = true;

    if (!VK_RecreateSwapchain(r_config.width, r_config.height)) {
        vk_state.swapchain_dirty = true;
    }

    Com_Printf("----------------------\n");

    return true;
}

void R_Shutdown(bool total)
{
    if (!vk_state.initialized) {
        return;
    }

    if (!total) {
        vk_state.swapchain_dirty = true;
        return;
    }

    // Shadow descriptors can still be referenced by the final submitted
    // command buffer. Drain the device before tearing that subsystem down;
    // VK_DestroyContext's wait occurs after this call and is therefore too
    // late for descriptor lifetime validation.
    if (vk_state.ctx.device) {
        vkDeviceWaitIdle(vk_state.ctx.device);
    }
    VK_Shadow_Shutdown(&vk_state.ctx);
    VK_DestroyContext();
    ShadowFrontend_Shutdown(&vk_shadow_frontend);
    Cmd_RemoveCommand("r_shadow_dump");
    Cmd_RemoveCommand("vk_shadow_dump");
    Cmd_RemoveCommand("screenshot");
    Cmd_RemoveCommand("screenshotpng");
    Cmd_RemoveCommand("screenshottga");
    vk_shadow_frontend_cvars_registered = false;
    vid->shutdown();

    memset(&vk_state, 0, sizeof(vk_state));
}

void R_BeginRegistration(const char *map)
{
    VK_Entity_BeginRegistration();
    VK_World_BeginRegistration(map);
}

qhandle_t R_RegisterModel(const char *name)
{
    return VK_Entity_RegisterModel(name);
}

qhandle_t R_RegisterImage(const char *name, imagetype_t type, imageflags_t flags)
{
    return VK_UI_RegisterImage(name, type, flags);
}

qhandle_t R_RegisterRawImage(const char *name, int width, int height, byte *pic,
                             imagetype_t type, imageflags_t flags)
{
    return VK_UI_RegisterRawImage(name, width, height, pic, type, flags);
}

void R_UnregisterImage(qhandle_t handle)
{
    if (vk_state.initialized && vk_state.ctx.device &&
        !VK_WaitForSubmittedFrames(&vk_state.ctx,
                                   "vkWaitForFences(unregister image)")) {
        return;
    }

    VK_UI_UnregisterImage(handle);
}

bool R_RmlUiDrawGeometry(const renderer_rmlui_vertex_t *vertices,
                         size_t vertex_count,
                         const uint32_t *indices,
                         size_t index_count,
                         float translation_x,
                         float translation_y,
                         qhandle_t texture)
{
    return VK_UI_DrawRmlGeometry(vertices, vertex_count, indices, index_count,
                                 translation_x, translation_y, texture);
}

void R_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis)
{
    Com_DPrintf("VK R_SetSky: name=%s rotate=%.2f autorotate=%d axis=(%.2f %.2f %.2f)\n",
                (name && *name) ? name : "<empty>",
                rotate, autorotate ? 1 : 0,
                axis ? axis[0] : 0.0f,
                axis ? axis[1] : 0.0f,
                axis ? axis[2] : 0.0f);
    VK_World_SetSky(name, rotate, autorotate, axis);
}

void R_EndRegistration(void)
{
    VK_World_EndRegistration();
    VK_Entity_EndRegistration();
}

void R_RenderFrame(const refdef_t *fd)
{
    VK_Debug_SetRefdef(fd);
    VK_Debug_SetSceneCounts((uint32_t)max(fd->num_entities, 0),
                            (uint32_t)max(fd->num_dlights, 0),
                            (uint32_t)max(fd->num_particles, 0));

    shadow_frontend_policy_t shadow_policy;
    ShadowFrontend_PolicyFromCvars(&vk_shadow_frontend_cvars, &shadow_policy);
    shadow_policy.max_resolution =
        min(shadow_policy.max_resolution, VK_SHADOW_MAX_RESOLUTION);
    shadow_policy.default_resolution =
        min(shadow_policy.default_resolution, VK_SHADOW_MAX_RESOLUTION);
    shadow_policy.sun_resolution =
        min(shadow_policy.sun_resolution, VK_SHADOW_MAX_RESOLUTION);
    int shadow_sun_pages = shadow_policy.sun_enabled
        ? shadow_policy.sun_cascades
        : 0;
    int shadow_local_pages = max(0, VK_SHADOW_MAX_PAGES - shadow_sun_pages);
    shadow_policy.max_lights =
        min(shadow_policy.max_lights,
            shadow_local_pages / SHADOW_FRONTEND_POINT_FACES);
    ShadowFrontend_BuildFrame(&vk_shadow_frontend, fd, VK_World_GetBsp(),
                              &shadow_policy, &vk_shadow_backend_ops);
    VK_Shadow_UpdateDlights(fd);

    VK_World_RenderFrame(fd);
    VK_PostProcess_RenderFrame(fd);
    VK_Entity_RenderFrame(fd);

    // Full-screen liquid/powerup and damage blends layer between the 3D view
    // and the HUD, mirroring the GL renderer's GL_Blend pass.
    VK_UI_DrawScreenBlend(fd, vk_damageblend_frac
                          ? Cvar_ClampValue(vk_damageblend_frac, 0.0f, 0.5f)
                          : 0.2f);
}

void R_LightPoint(const vec3_t origin, vec3_t light)
{
    VK_World_LightPoint(origin, light);
}

void R_SetClipRect(const clipRect_t *clip)
{
    VK_UI_SetClipRect(clip);
}

float R_ClampScale(cvar_t *var)
{
    return VK_UI_ClampScale(var);
}

void R_SetScale(float scale)
{
    VK_UI_SetScale(scale);
}

static inline color_t vk_draw_resolve_color(int flags, color_t color)
{
    if (flags & (UI_ALTCOLOR | UI_XORCOLOR)) {
        color_t alt = COLOR_RGB(255, 255, 0);
        alt.a = color.a;
        return alt;
    }
    return color;
}

static inline void vk_draw_char(int x, int y, int w, int h, int flags, int c,
                                color_t color, qhandle_t font)
{
    if ((c & 127) == 32)
        return;

    if (flags & UI_ALTCOLOR)
        c |= 0x80;
    if (flags & UI_XORCOLOR)
        c ^= 0x80;

    if (c >> 7)
        color = COLOR_SETA_U8(COLOR_WHITE, color.a);

    float s = (c & 15) * 0.0625f;
    float t = (c >> 4) * 0.0625f;
    float eps = 1e-5f;

    if ((flags & UI_DROPSHADOW) && c != 0x83) {
        color_t shadow = COLOR_A(color.a);
        VK_UI_DrawStretchSubPic(x + 1, y + 1, w, h,
                                s + eps, t + eps,
                                s + 0.0625f - eps, t + 0.0625f - eps,
                                shadow, font);
    }

    VK_UI_DrawStretchSubPic(x, y, w, h,
                            s + eps, t + eps,
                            s + 0.0625f - eps, t + 0.0625f - eps,
                            color, font);
}

void R_DrawChar(int x, int y, int flags, int ch, color_t color, qhandle_t font)
{
    vk_draw_char(x, y, CONCHAR_WIDTH, CONCHAR_HEIGHT, flags, ch & 255, color, font);
}

void R_DrawStretchChar(int x, int y, int w, int h, int flags, int ch, color_t color, qhandle_t font)
{
    vk_draw_char(x, y, w, h, flags, ch & 255, color, font);
}

int R_DrawStringStretch(int x, int y, int scale, int flags, size_t maxChars,
                        const char *string, color_t color, qhandle_t font)
{
    if (!string)
        return x;

    int sx = x;
    size_t remaining = maxChars;
    bool use_color_codes = Com_HasColorEscape(string, maxChars);
    int draw_flags = flags;
    color_t base_color = color;
    if (use_color_codes) {
        base_color = vk_draw_resolve_color(flags, color);
        draw_flags &= ~(UI_ALTCOLOR | UI_XORCOLOR);
    }
    color_t draw_color = use_color_codes ? base_color : color;

    while (remaining && *string) {
        if (use_color_codes) {
            color_t parsed;
            if (Com_ParseColorEscape(&string, &remaining, base_color, &parsed)) {
                draw_color = parsed;
                continue;
            }
        }

        byte ch = *string++;
        remaining--;

        if ((flags & UI_MULTILINE) && ch == '\n') {
            y += CONCHAR_HEIGHT * max(scale, 1) + 1;
            x = sx;
            continue;
        }

        vk_draw_char(x, y, CONCHAR_WIDTH * max(scale, 1), CONCHAR_HEIGHT * max(scale, 1),
                     draw_flags, ch, draw_color, font);
        x += CONCHAR_WIDTH * max(scale, 1);
    }

    return x;
}

const kfont_char_t *SCR_KFontLookup(const kfont_t *kfont, uint32_t codepoint)
{
    if (!kfont)
        return NULL;

    if (codepoint < KFONT_ASCII_MIN || codepoint > KFONT_ASCII_MAX)
        return NULL;

    const kfont_char_t *ch = &kfont->chars[codepoint - KFONT_ASCII_MIN];

    if (!ch->w)
        return NULL;

    return ch;
}

void SCR_LoadKFont(kfont_t *font, const char *filename)
{
    static cvar_t *cl_debug_fonts;
    if (!cl_debug_fonts)
        cl_debug_fonts = Cvar_Get("cl_debug_fonts", "1", 0);
    const bool debug_fonts = cl_debug_fonts && cl_debug_fonts->integer;

    memset(font, 0, sizeof(*font));

    char *buffer;

    if (FS_LoadFile(filename, (void **) &buffer) < 0) {
        if (debug_fonts) {
            Com_LPrintf(PRINT_ALL, "Font: SCR_LoadKFont \"%s\" failed: %s\n",
                        filename ? filename : "<null>", Com_GetLastError());
        }
        return;
    }

    if (debug_fonts) {
        Com_LPrintf(PRINT_ALL, "Font: SCR_LoadKFont \"%s\"\n",
                    filename ? filename : "<null>");
    }

    const char *data = buffer;

    while (true) {
        const char *token = COM_Parse(&data);

        if (!*token)
            break;

        if (!strcmp(token, "texture")) {
            token = COM_Parse(&data);
            font->pic = R_RegisterFont(va("/%s", token));
            if (debug_fonts) {
                Com_LPrintf(PRINT_ALL, "Font: kfont texture \"%s\" handle=%d\n",
                            token ? token : "<null>", font->pic);
            }
        } else if (!strcmp(token, "unicode")) {
        } else if (!strcmp(token, "mapchar")) {
            token = COM_Parse(&data);

            while (true) {
                token = COM_Parse(&data);

                if (!*token || !strcmp(token, "}"))
                    break;

                uint32_t codepoint = strtoul(token, NULL, 10);
                uint32_t x = strtoul(COM_Parse(&data), NULL, 10);
                uint32_t y = strtoul(COM_Parse(&data), NULL, 10);
                uint32_t w = strtoul(COM_Parse(&data), NULL, 10);
                uint32_t h = strtoul(COM_Parse(&data), NULL, 10);
                COM_Parse(&data);

                if (codepoint >= KFONT_ASCII_MIN && codepoint <= KFONT_ASCII_MAX) {
                    size_t char_index = (size_t)(codepoint - KFONT_ASCII_MIN);
                    font->chars[char_index].x = x;
                    font->chars[char_index].y = y;
                    font->chars[char_index].w = w;
                    font->chars[char_index].h = h;

                    font->line_height = max(font->line_height, h);
                }
            }
        }
    }

    int pic_w = 0;
    int pic_h = 0;
    if (font->pic && R_GetPicSize(&pic_w, &pic_h, font->pic) && pic_w > 0 && pic_h > 0) {
        font->sw = 1.0f / pic_w;
        font->sh = 1.0f / pic_h;
    }

    FS_FreeFile(buffer);

    if (debug_fonts) {
        Com_LPrintf(PRINT_ALL, "Font: kfont \"%s\" loaded line_height=%d handle=%d\n",
                    filename ? filename : "<null>", font->line_height, font->pic);
    }
}

int R_DrawKFontChar(int x, int y, int scale, int flags, uint32_t codepoint, color_t color, const kfont_t *kfont)
{
    const kfont_char_t *ch = SCR_KFontLookup(kfont, codepoint);
    if (!ch || !kfont || !kfont->pic || kfont->sw <= 0.0f || kfont->sh <= 0.0f)
        return 0;

    int draw_scale = max(scale, 1);
    int w = ch->w * draw_scale;
    int h = ch->h * draw_scale;

    float s = ch->x * kfont->sw;
    float t = ch->y * kfont->sh;
    float sw = ch->w * kfont->sw;
    float sh = ch->h * kfont->sh;

    if (flags & UI_DROPSHADOW) {
        color_t shadow = COLOR_A(color.a);
        int shadow_offset = draw_scale;
        VK_UI_DrawStretchSubPic(x + shadow_offset, y + shadow_offset, w, h,
                                s, t, s + sw, t + sh,
                                shadow, kfont->pic);
    }

    VK_UI_DrawStretchSubPic(x, y, w, h, s, t, s + sw, t + sh, color, kfont->pic);
    return ch->w * draw_scale;
}

bool R_GetPicSize(int *w, int *h, qhandle_t pic)
{
    return VK_UI_GetPicSize(w, h, pic);
}

void R_DrawPic(int x, int y, color_t color, qhandle_t pic)
{
    VK_UI_DrawPic(x, y, color, pic);
}

void R_DrawStretchPic(int x, int y, int w, int h, color_t color, qhandle_t pic)
{
    VK_UI_DrawStretchPic(x, y, w, h, color, pic);
}

void R_DrawStretchSubPic(int x, int y, int w, int h,
                         float s1, float t1, float s2, float t2,
                         color_t color, qhandle_t pic)
{
    VK_UI_DrawStretchSubPic(x, y, w, h, s1, t1, s2, t2, color, pic);
}

void R_DrawStretchRotatePic(int x, int y, int w, int h, color_t color, float angle,
                            int pivot_x, int pivot_y, qhandle_t pic)
{
    VK_UI_DrawStretchRotatePic(x, y, w, h, color, angle, pivot_x, pivot_y, pic);
}

void R_DrawKeepAspectPic(int x, int y, int w, int h, color_t color, qhandle_t pic)
{
    VK_UI_DrawKeepAspectPic(x, y, w, h, color, pic);
}

void R_DrawStretchRaw(int x, int y, int w, int h)
{
    VK_UI_DrawStretchRaw(x, y, w, h);
}

void R_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic)
{
    VK_UI_UpdateRawPic(pic_w, pic_h, pic);
}

bool R_UpdateImageRGBA(qhandle_t handle, int width, int height, const byte *pic)
{
    return VK_UI_UpdateImageRGBA(handle, width, height, pic);
}

void R_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
    VK_UI_TileClear(x, y, w, h, pic);
}

void R_DrawFill8(int x, int y, int w, int h, int c)
{
    VK_UI_DrawFill8(x, y, w, h, c);
}

void R_DrawFill32(int x, int y, int w, int h, color_t color)
{
    VK_UI_DrawFill32(x, y, w, h, color);
}

void R_BeginFrame(void)
{
    if (!vk_state.initialized)
        return;

    if (vk_state.swapchain_dirty) {
        if (!VK_RecreateSwapchain(r_config.width, r_config.height))
            return;
    }

    if (!vk_state.ctx.frame_count ||
        !VK_WaitForFrame(&vk_state.ctx, vk_state.ctx.current_frame,
                         "vkWaitForFences(begin frame)")) {
        return;
    }

    vk_frame_begin_us = VK_HighResMicroseconds();
    VK_Debug_BeginFrame();
    VK_UI_BeginFrame();
}

void R_EndFrame(void)
{
    if (!vk_state.initialized)
        return;

    if (vk_state.swapchain_dirty) {
        if (!VK_RecreateSwapchain(r_config.width, r_config.height))
            return;
    }

    VK_UI_EndFrame();
    if (!VK_DrawFrame()) {
        const char *error = Com_GetLastError();
        if (error && *error) {
            Com_WPrintf("Vulkan: frame submission failed: %s\n", error);
        }
    }
    VK_Debug_EndFrame((float)(VK_HighResMicroseconds() - vk_frame_begin_us) /
                      1000.0f);
}

void R_ModeChanged(int width, int height, int flags)
{
    r_config.width = width;
    r_config.height = height;
    r_config.flags = flags;

    if (!vk_state.initialized)
        return;

    if (vk_state.ctx.device) {
        vkDeviceWaitIdle(vk_state.ctx.device);
    }

    VK_PostProcess_DestroySwapchainResources(&vk_state.ctx);
    VK_DestroySwapchain(&vk_state.ctx);

    VK_DestroySurfaceHandle(&vk_state.ctx);

    vid_native_window_t native;
    if (!VK_QueryNativeWindow(&native)) {
        vk_state.swapchain_dirty = true;
        return;
    }

    vk_state.native_window = native;

    if (!VK_CreateSurface()) {
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_CreateSwapchain(width, height)) {
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_World_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_Entity_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_Debug_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_UI_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    if (!VK_PostProcess_CreateSwapchainResources(&vk_state.ctx)) {
        VK_DestroySwapchain(&vk_state.ctx);
        vk_state.swapchain_dirty = true;
        return;
    }

    VK_Debug_UpdateCapabilities(
        vk_screenshot_supported,
        VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format),
        vk_dof_supported);

    vk_state.swapchain_dirty = false;
}

bool R_VideoSync(void)
{
    return true;
}

void GL_ExpireDebugObjects(void)
{
    R_ExpireDebugLines();
}

bool R_SupportsPerPixelLighting(void)
{
    return true;
}

r_opengl_config_t R_GetGLConfig(void)
{
    bool stencil_available = vk_state.initialized &&
        VK_DepthFormatHasStencil(vk_state.ctx.swapchain.depth_format);
    r_opengl_config_t cfg = {
        .colorbits = 24,
        .depthbits = 24,
        .stencilbits = stencil_available ? 8 : 0,
        .multisamples = 0,
        .debug = false,
        .profile = QGL_PROFILE_NONE,
        .major_ver = 0,
        .minor_ver = 0,
    };

    return cfg;
}

bool R_SupportsDebugLines(void)
{
    return VK_Debug_Supported();
}

void R_AddDebugText_(const vec3_t origin, const vec3_t angles, const char *text, float size,
                     color_t color, uint32_t time, bool depth_test)
{
    VK_Debug_AddText(origin, angles, text, size, color, time, depth_test);
}
