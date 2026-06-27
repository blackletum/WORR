# First-Party Runtime and Vulkan Bounds Pass

Task: `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.

Date: 2026-06-27

## Summary

This pass implements a focused set of first-party reliability improvements across server helpers, sgame intermission flow, the shared Base85 codec, patched-PVS file handling, Steam path discovery, the Game3 bridge, and the native Vulkan UI/world paths. The work avoids `q2proto/`, imported code, and renderer-path redirection.

## Implemented Improvements

1. Server address-match listing now copies the `"never"` timestamp fallback through `Q_strlcpy()`.
2. Server address-match listing now copies the `"error"` timestamp fallback through `Q_strlcpy()`.
3. Sgame intermission queuing now accepts a null victor message and copies through `Q_strlcpy()` instead of open-coded `strncpy()` plus manual termination.
4. Game3 proxy old-client stat import now bounds the copy to the server destination array after zero-filling it.
5. Game3 proxy new-client stat import now zero-fills the server destination stats and bounds the copy to the destination array.
6. Game3 proxy Base85 save reads now check encoder context initialization, incremental encode, and finalization results.
7. Game3 proxy Base85 restore writes now check decoder context initialization, incremental decode, and finalization results.
8. The shared Base85 output-buffer growth path now records the actual fallback allocation size instead of the failed larger request size.
9. The shared Base85 output-buffer growth path now guards used-size plus requested-size overflow and negative remaining-space states.
10. BSP patched-PVS path construction now validates null inputs and every append/copy before returning a generated `pvs/*.bin` path.
11. BSP PVS matrix construction, patched-PVS loading, and patched-PVS saving now guard row/cluster/file-size multiplication before allocating, reading, or writing.
12. Steam Quake II discovery now rejects oversized `libraryfolders.vdf`, base Quake II, and rerelease paths before use.
13. Native Vulkan UI capacity growth now detects vertex, index, draw, and image handle count overflows before reallocating host arrays.
14. Native Vulkan UI image load/upload paths now share checked byte-size calculation for PCX, WAL, STB-decoded RGBA, full uploads, and sub-rect uploads.
15. Native Vulkan world lightmap atlas allocation now checks face-lightmap table and atlas byte sizes before allocating.
16. Native Vulkan world mesh building now checks vertex, batch, texture-handle, and texture-size-map allocation sizes before allocating.

## Files Changed

- `src/server/commands.c`
- `src/game/sgame/gameplay/g_main.cpp`
- `src/server/game3_proxy/game3_proxy.c`
- `src/shared/base85.c`
- `src/common/bsp.c`
- `src/common/steam.c`
- `src/rend_vk/vk_ui.c`
- `src/rend_vk/vk_world.c`

## Verification

- `meson compile -C builddir-win worr_engine_x86_64 worr_ded_engine_x86_64 sgame_x86_64 worr_vulkan_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` refreshed and validated `.install/`.
- `git diff --check -- src/server/commands.c src/game/sgame/gameplay/g_main.cpp src/server/game3_proxy/game3_proxy.c src/shared/base85.c src/common/bsp.c src/common/steam.c src/rend_vk/vk_ui.c src/rend_vk/vk_world.c docs-dev/first-party-runtime-vulkan-bounds-pass-2026-06-27.md docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md` passed.
- `meson test -C builddir-win --list` reported `No tests defined.`
