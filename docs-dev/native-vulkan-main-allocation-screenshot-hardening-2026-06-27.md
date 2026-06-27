# Native Vulkan Main Allocation and Screenshot Hardening

Date: 2026-06-27

Task IDs: `DV-04-T03`

## Summary

This pass hardens `src/rend_vk/vk_main.c` around count-driven Vulkan
enumeration, swapchain-owned arrays, and screenshot readback sizing. It keeps
the native Vulkan renderer path native; no OpenGL fallback or redirect behavior
was introduced.

## Implemented Improvements

1. Added shared checked byte-size helpers for array allocations and image byte
   counts.
2. Added a renderer-tagged array allocator wrapper that reports overflow and
   allocation failures through the existing Vulkan error path.
3. Checked instance extension enumeration allocation size before allocating.
4. Checked device extension enumeration allocation size before allocating.
5. Checked queue-family allocation size and propagated present-support query
   failures instead of ignoring the Vulkan result.
6. Checked physical-device list allocation size before allocating.
7. Split surface-format query errors from empty-result errors so zero formats
   fail with a clear diagnostic instead of flowing through `VK_SUCCESS`.
8. Split present-mode query errors from empty-result errors for the same reason.
9. Checked swapchain image-list allocation size and rejected zero reported
   swapchain images before and after the image query.
10. Checked swapchain image-view, framebuffer, and command-buffer array
    allocations before use.
11. Rejected zero-sized screenshot readback buffer requests.
12. Replaced screenshot readback and RGB conversion byte math with checked image
    sizing.
13. Checked screenshot RGB allocation failure and cleaned it up correctly if
    memory mapping fails.
14. Checked PNG writer width, height, and row-stride limits before casting to
    `int`.

## Verification

- `meson compile -C builddir-win worr_vulkan_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `git diff --check`
- `meson test -C builddir-win --list` reported `No tests defined.`
