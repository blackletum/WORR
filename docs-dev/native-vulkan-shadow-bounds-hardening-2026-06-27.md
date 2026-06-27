# Native Vulkan Shadow Bounds Hardening - 2026-06-27

Task: `DV-04-T03`

## Summary

This pass hardens the native Vulkan shadow backend around transient CPU vertex storage, world face-bounds caching, page/view validation, and mapped GPU vertex uploads. The rendering path remains native Vulkan and preserves the existing shadow frontend contract.

## Implemented Improvements

1. Added a shared checked byte-size helper for Vulkan shadow allocations and uploads.
2. Added checked `uint32_t` count addition for triangle vertex emission.
3. Added checked CPU vertex-capacity growth with explicit overflow diagnostics.
4. Replaced the literal initial shadow vertex capacity with a named constant.
5. Checked CPU shadow vertex `realloc()` byte sizing before growing transient vertex storage.
6. Checked triangle vertex count growth before reserving three more shadow vertices.
7. Replaced the triangle zero-fill byte expression with checked size computation.
8. Checked world face-bounds cache allocation size before allocating per-face bounds.
9. Added a direct BSP face-array guard and out-of-memory diagnostic to the face-bounds cache builder.
10. Added shadow storage-family validation for resource creation and command recording.
11. Added shared view validation for page index, resolution, and storage family at Vulkan backend entry points.
12. Added a context/device guard before creating or reusing shadow vertex buffers.
13. Reworked shadow render job reservation so failed or empty views restore vertex state and do not consume a job slot.
14. Added negative caster-count rejection at the render-view boundary.
15. Checked final mapped GPU vertex upload byte sizing before copying shadow vertices.
16. Added resolution/storage guards before recording shadow draw commands.

## Files Changed

- `src/rend_vk/vk_shadow.c`

## Verification

- `meson compile -C builddir-win worr_vulkan_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed and refreshed `.install/`.
- `git diff --check` passed.
- `meson test -C builddir-win --list` reported `No tests defined.`
