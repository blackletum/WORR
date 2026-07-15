# Native Vulkan Debug Overlay and Telemetry

Date: 2026-07-14  
Project task: `FR-01-T08`  
Status: implemented and runtime-validated

## Outcome

The native Vulkan renderer now implements the refresh API's world-space debug
line, shape, and text exports instead of dropping them in `vk_main.c`. It also
has a native renderer-stat panel and machine-readable runtime dump for the
draw, upload, query, scene, and capability state that matters when closing
visual parity or modernizing a hot path.

This implementation remains entirely in the Vulkan renderer. It neither calls
OpenGL nor routes Vulkan debug work through the OpenGL backend.

## Native implementation

`src/rend_vk/vk_debug.c` owns the Vulkan backend:

- A host-visible, persistently mapped line vertex buffer is bounded by the
  shared `MAX_DEBUG_VERTICES` limit.
- Two native line-list pipelines are created with each swapchain: depth-tested
  and always-visible. Both use one-pixel core Vulkan lines, avoiding an
  optional wide-line device feature.
- The pipeline shares the renderer's native view/projection push-constant
  layout and records after world/entities but before UI, so world-space
  primitives naturally obey the active view and depth buffer.
- `R_BuildViewPush` supplies exactly the same coordinate convention used by
  world and entity pipelines.
- `R_ClearDebugLines`, lines, points, axes, bounds, spheres, circles,
  cylinders, arrows, curve arrows, and persistent expiry use the shared
  renderer-neutral debug-object pool. Vulkan now builds that source alongside
  its own backend.
- `R_AddDebugText_` uses the shared stroke-font expansion and emits Vulkan
  line geometry. Billboarded text therefore needs no texture atlas or
  OpenGL-only text path.

The debug lifecycle is tied to both swapchain paths. In particular,
`R_ModeChanged` has its own surface/swapchain rebuild sequence; it now creates
the two debug pipelines and refreshes diagnostics as well. This was necessary
to prevent an otherwise successful window-mode change from silently disabling
all debug primitives.

## Runtime controls and diagnostics

- `vk_debug_draw 0|1` disables native debug-line rasterization without
  discarding the public API.
- `cleardebuglines` has the same public clearing command as the GL renderer.
- `vk_stats` emits a stable `VK_STATS` line and a `VK_CAPS` capability line.
- `vk_debug_test` queues a deterministic collection of all major primitive
  types plus billboarded text in front of the current camera. It is an
  automated fixture command, not a replacement for game/debug callers.
- The registered `renderer` stat category reports separate world, entity, UI,
  shadow, and debug draw/vertex/index/upload totals; scene entity/dlight/
  particle counts; flare-query count; debug-line capacity hits; CPU frame
  time; and the missing-capability bitmask.

The mask makes hardware or lifecycle fallbacks visible without pretending they
are rendering success: bit 0 is screenshot transfer support, bit 1 stencil
outlines, bit 2 the sampled-depth gameplay DOF path, and bit 3 the debug pipeline.
`VK_CAPS` provides named booleans for the same state.

The CPU value is deliberately only a frame wall-time baseline. Timestamp-query
GPU phase timings, show-tris/origins parity, multi-frame statistics, budgets,
and before/after performance proof are still `FR-01-T15` work. The counters
are the instrumentation foundation for that task, not a claim that it is
complete.

## Verification

The owned real-client smoke config is
`assets/renderer_parity/fr01_vk_debug_overlay.cfg`. It captures a deterministic
native Vulkan fixture before and after `vk_debug_test`, then writes `vk_stats`.
`tools/renderer_parity/run_vk_debug_smoke.py` checks the process log,
capability dump, captures, and image difference. Its focused parser/image unit
test is `tools/renderer_parity/test_run_vk_debug_smoke.py`.

Final validation was run with `VK_LAYER_KHRONOS_validation`:

```text
python tools/renderer_parity/run_vk_debug_smoke.py --install-dir .install --run-root .tmp/renderer-parity/fr01-vk-debug-validation --vulkan-validation --json-output .tmp/renderer-parity/fr01-vk-debug-validation/results.json
```

Result from `.tmp/renderer-parity/fr01-vk-debug-validation/results.json`:

```text
client exit:                         0
queued native debug lines:           198 / 8192
recorded draws / vertices:           10 / 444
recorded upload bytes:               8640
debug overlay changed pixels:        3067
maximum RGB channel delta:           231
missing capability mask:             0x00
validation errors or VUIDs:          none
```

Focused checks also passed:

```text
python tools/gen_vk_world_spv.py --validate
python tools/renderer_parity/test_run_vk_debug_smoke.py
python tools/renderer_parity/test_compare_captures.py
python tools/test_package_assets.py
ninja -C builddir-win worr_vulkan_x86_64.dll
```
