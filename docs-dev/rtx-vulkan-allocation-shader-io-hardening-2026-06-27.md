# RTX Vulkan Allocation and Shader I/O Hardening - 2026-06-27

Task: `DV-04-T03`

## Summary

This pass hardens RTX/Vulkan startup, swapchain setup, stretch-pic framebuffer setup, and shader module loading. The goal is to convert assert-only or unchecked allocation/file I/O assumptions into explicit runtime failures that preserve useful diagnostics and avoid continuing with partially initialized renderer state.

## Implemented Improvements

1. Added `vkpt_array_allocation_size()` so Vulkan renderer arrays can share overflow-checked byte-size calculation before allocation.
2. Converted Vulkan instance extension enumeration from a void helper into a boolean helper with explicit first-query failure reporting.
3. Added extension-list allocation overflow and out-of-memory checks before reading the returned extension properties.
4. Added cleanup and diagnostic handling when the second extension-list read fails.
5. Converted Vulkan layer enumeration from a void helper into a boolean helper with explicit first-query failure reporting.
6. Added layer-list allocation overflow and out-of-memory checks before reading layer properties.
7. Added cleanup and diagnostic handling when the second layer-list read fails.
8. Replaced swapchain creation's generic error return with the native `VkResult` and a `Com_SetLastError()` message.
9. Replaced the swapchain image-count `assert()` with runtime handling for query failures and zero-image swapchains.
10. Added swapchain image-array overflow, out-of-memory, and second-read failure checks.
11. Added swapchain image-view array overflow/out-of-memory checks and partial image-view cleanup on creation failure.
12. Made initial and recreated swapchain setup stop before later RTX initialization if swapchain creation fails in a release build.
13. Made swapchain destruction tolerate partially initialized image-view arrays and null handles.
14. Hardened shader-file loading by checking the `len + 1` allocation bound, checking `fclose()`, freeing the shader buffer on short read or close failure, and writing the terminator through a `size_t` index.
15. Added stretch-pic framebuffer array overflow/out-of-memory checks and partial framebuffer cleanup on creation failure.

## Files Touched

- `src/rend_rtx/vkpt/vkpt.h`
- `src/rend_rtx/vkpt/main.c`
- `src/rend_rtx/vkpt/draw.c`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Validation

- `meson compile -C builddir-win worr_rtx_x86_64` passed after the implementation changes.
- `rg` confirmed the old unchecked swapchain/stretcher allocation and assert patterns are gone from the touched RTX files.
- `git diff --check -- src/rend_rtx/vkpt/vkpt.h src/rend_rtx/vkpt/main.c src/rend_rtx/vkpt/draw.c docs-dev/rtx-vulkan-allocation-shader-io-hardening-2026-06-27.md docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md` passed.
- `meson test -C builddir-win --list` reported `No tests defined.`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` completed and refreshed `.install/`.
