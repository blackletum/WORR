# Native Vulkan Entity Loader Bounds Hardening

Date: 2026-06-27

Task IDs: `DV-04-T03`

## Summary

This pass hardens `src/rend_vk/vk_entity.c` around native Vulkan entity-model
loading, scratch-buffer growth, BSP model texture caches, and per-frame MD2
vertex uploads. It remains fully native Vulkan work and does not redirect any
renderer path to OpenGL.

## Implemented Improvements

1. Added shared checked byte-size helpers for entity array allocations.
2. Added checked `calloc` and `realloc` wrappers for entity-owned buffers.
3. Added range validation for file-backed model lumps with negative-offset and
   length-overflow diagnostics.
4. Added checked MD2 frame/vector offset calculation for render-time positions,
   normals, bounds, and shadow emission.
5. Hardened MD5 mesh array allocation for meshes, vertices, texture
   coordinates, indices, weights, and joint indices.
6. Hardened MD5 animation skeleton frame count sizing and initialization.
7. Hardened temporary MD5 skeleton growth with checked realloc sizing.
8. Moved MD5 replacement skin allocation before model ownership transfer so a
   failed allocation cannot leave a partially promoted replacement model.
9. Hardened dynamic entity vertex and batch capacity growth against unchecked
   doubling and realloc byte products.
10. Hardened BSP inline-model texture cache allocations for handles,
    descriptor sets, inverse sizes, and transparency flags.
11. Hardened SP2 sprite frame table sizing and allocation.
12. Hardened MD2 lump range checks, triangle index sizing, scratch arrays,
    positions, normals, UVs, indices, skins, and skin-name allocations.
13. Added cleanup for failed MD2 skin-name allocation paths.
14. Hardened final per-frame entity vertex upload byte sizing before copying to
    the persistent Vulkan buffer.

## Verification

- `meson compile -C builddir-win worr_vulkan_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `git diff --check`
- `meson test -C builddir-win --list` reported `No tests defined.`
