# Native Vulkan Upload Bounds Follow-up - 2026-06-27

Task: `DV-04-T03`

## Summary

This follow-up extends the first-party bounds cleanup into remaining native Vulkan world/UI upload paths. The work keeps Vulkan renderer behavior native and unchanged while replacing open-coded allocation, upload, atlas, and dirty-rectangle size math with checked helpers.

## Implemented Improvements

1. Added `VK_World_ImageBytes()` so world lightmap and image-like byte calculations share one overflow-checked path.
2. Routed world-face mask allocation and fallback initialization through checked byte sizing.
3. Checked static world vertex upload byte counts before persistent vertex-buffer creation and copy.
4. Checked sky vertex upload byte counts before transient sky-buffer creation, mapping, and copy.
5. Made lightmap atlas pixel initialization failure-aware instead of relying on unchecked atlas-size multiplication.
6. Routed face-lightmap style stride and per-pixel offsets through checked `size_t` math.
7. Checked lightmap atlas RGBA allocation and atlas-column allocation sizes during atlas candidate packing.
8. Hardened dynamic lightmap dirty-rect bounds to avoid signed `x + w` / `y + h` overflow.
9. Checked dirty-rect allocation and per-row copy byte counts before staging lightmap sub-rect uploads.
10. Checked lightmap debug atlas/dirty-area coverage math instead of multiplying `int` dimensions directly.
11. Checked world mesh vertex/batch shrink sizes before optional `realloc()` compaction.
12. Checked dynamic world vertex refresh byte counts before copying CPU vertices into mapped GPU memory.
13. Split Vulkan UI texture pixel-count calculation from byte-size calculation so transparency scans use checked dimensions.
14. Checked Vulkan UI GPU-buffer and per-frame vertex/index upload byte counts.
15. Hardened Vulkan UI sub-rectangle update bounds to avoid signed `x + width` / `y + height` overflow.

## Files Changed

- `src/rend_vk/vk_world.c`
- `src/rend_vk/vk_ui.c`

## Verification

- `meson compile -C builddir-win worr_vulkan_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed and refreshed `.install/`.
- `git diff --check` passed.
- `meson test -C builddir-win --list` reported `No tests defined.`
