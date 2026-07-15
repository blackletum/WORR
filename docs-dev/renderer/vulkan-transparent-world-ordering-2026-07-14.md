# Native Vulkan Transparent World Ordering

Date: 2026-07-14

Task ID: `FR-01-T10`

Status: partial implementation

## Change

Native Vulkan previously coalesced adjacent transparent world faces with the
same descriptor and emitted the resulting batches in BSP build order. This
could blend overlapping `SURF_TRANS33`/`SURF_TRANS66` surfaces in a visibly
incorrect order.

`src/rend_vk/vk_world.c` now keeps each transparent BSP face as a separate
batch, records its authored-space center, and sorts those transparent batches
back-to-front from the current view origin immediately before recording the
alpha pipeline. Equal-depth batches retain their original BSP batch order.
Opaque batches retain their prior descriptor-compatible coalescing and draw
order; this change is scoped to blending correctness rather than the opaque
hot path.

## OpenGL comparison

OpenGL gathers alpha faces separately while traversing the BSP after opaque
world work and draws them in `GL_DrawAlphaFaces`. Client entities are already
distance-sorted before submission and OpenGL additionally separates its
back/front alpha lists. The new Vulkan world ordering applies the equivalent
back-to-front principle to its static transparent world batches without
routing through OpenGL.

## Current boundary

- Covered: static world `SURF_TRANS33` and `SURF_TRANS66` faces.
- Preserved: opaque world batching and the existing client entity ordering.
- Validated: a generated fixture uses an opaque backdrop and two oversized,
  differently transparent planes. The near plane is deliberately first in BSP
  order; the fixture therefore only produces its expected two-layer colour
  when Vulkan draws the farther plane first.
- Completed since this ordering pass: flow scroll and per-pixel turbulent
  warp have an exact paired capture and no longer trigger a full-world CPU
  vertex rewrite. See
  `docs-dev/renderer/vulkan-world-animation-device-local-2026-07-14.md`.
- Completed since this ordering pass: transparent-liquid refraction and
  underwater/full-screen waterwarp plus entity back/front alpha phases now
  run natively in Vulkan; see
  `docs-dev/renderer/vulkan-liquid-refraction-2026-07-15.md`.

This partial record deliberately does not mark `FR-01-T10` complete.

## Validation

Completed against the staged runtime:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew
python -u tools/renderer_parity/run_vk_debug_smoke.py --install-dir .install --run-root .tmp/renderer-parity/fr01-vk-debug-t10-ordering --vulkan-validation --json-output .tmp/renderer-parity/fr01-vk-debug-t10-ordering/results.json
# exit 0; 3,067 changed pixels; maximum channel delta 231; no failures
```

`assets/renderer_parity/fr01_transparent_ordering_manifest.json` and its
generated map/textures now provide the dedicated paired capture. The geometry
is intentionally oversized so map-start view timing cannot move a transparent
raster edge through the measured region. The strict 240 by 210 overlap crop
contains 50,400 pixels and uses both image metrics and a colour-mask probe.

```text
python tools/renderer_parity/generate_transparent_ordering_fixture.py --asset-root assets --validate --json
python tools/test_package_assets.py
python -u tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_transparent_ordering_manifest.json --run-root .tmp/renderer-parity/fr01-transparent-ordering-oversize-1 --vulkan-validation
python -u tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_transparent_ordering_manifest.json --run-root .tmp/renderer-parity/fr01-transparent-ordering-oversize-2 --vulkan-validation
# fixture validation passed; 14 packaging tests passed
# both paired runs passed; max RGB delta [1, 1, 0]; 0 / 50,400 pixels above
# threshold; blend-mask intersection-over-union 1.0
```
