# RTX Vulkan Enumeration and Init Hardening - 2026-06-27

Task: `DV-04-T03`

## Summary

This pass continues the RTX renderer durability work by hardening Vulkan enumeration and initialization paths that still trusted driver-reported counts, stack `alloca()` buffers, or debug-only `_VK()` checks. The implementation keeps failures native to Vulkan, records `Com_SetLastError()` diagnostics, and frees temporary lists before returning.

## Implemented Improvements

1. Added a checked helper for physical-device extension enumeration so device extension count/read failures are no longer ignored.
2. Replaced unchecked surface-capability queries during swapchain creation with explicit `VkResult` handling.
3. Replaced surface-format `alloca()` usage with overflow-checked heap allocation.
4. Added surface-format count-query, zero-count, read-query, and cleanup handling.
5. Replaced present-mode `alloca()` usage with overflow-checked heap allocation.
6. Added present-mode count-query, zero-count, read-query, and cleanup handling.
7. Stopped assuming mailbox present mode is always available; non-vsync now prefers immediate, then mailbox, then guaranteed FIFO.
8. Replaced debug-only physical-device enumeration checks with release-build `VkResult` diagnostics.
9. Replaced physical-device list `alloca()` usage with overflow-checked heap allocation and cleanup.
10. Reused the checked device-extension helper for per-GPU ray-tracing capability detection.
11. Reused the checked device-extension helper for optional extension detection on the selected GPU.
12. Replaced queue-family `alloca()` usage with overflow-checked heap allocation.
13. Added queue-family zero-count and readback-zero handling before queue selection.
14. Added checked queue-family present-support queries and cleanup on failure.
15. Added a checked frame-time surface-capability query before minimized/window-resize swapchain recreation.

## Files Touched

- `src/rend_rtx/vkpt/main.c`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Validation

- `meson compile -C builddir-win worr_rtx_x86_64` passed after the implementation changes.
- `rg` confirmed the old unchecked Vulkan `alloca()` enumeration patterns and debug-only physical-device enumeration wrapper are gone from `src/rend_rtx/vkpt/main.c`.
- `git diff --check -- src/rend_rtx/vkpt/main.c docs-dev/rtx-vulkan-enumeration-init-hardening-2026-06-27.md docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md` passed.
- `meson test -C builddir-win --list` reported `No tests defined.`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` completed and refreshed `.install/`.
