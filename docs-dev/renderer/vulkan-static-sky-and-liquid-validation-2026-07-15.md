# Vulkan Static Sky Residency and Liquid-Pass Validation

Date: 2026-07-15

Task IDs: `FR-01-T05`, `FR-01-T13`, `FR-01-T15`

Status: implemented. The native Vulkan sky cube is immutable device-local
geometry; its camera-relative rotation is supplied by the normal frame-local
world stream, and compatible six-face sets submit through one native texture
array draw. The liquid/post-process pass is now render-pass compatible with
the scene pipelines and uses explicit native synchronization.

## Problem

The world renderer rebuilt all 36 six-face sky vertices every frame. The CPU
applied the current sky rotation and added `refdef.vieworg`, then wrote the
1,728-byte vertex stream to host-visible GPU memory. The fixed bmodel view
therefore reported `world_uploads=1744`: 1,728 bytes for the sky plus the
16-byte animated-world record. This was independent of world mesh residency
and occurred even for static, unrotated skies.

Validation on the first static-sky capture also uncovered a separate native
Vulkan correctness problem. A load-style liquid render pass had dependencies
that were incompatible with the base pass, but alpha world/entity/debug/UI
pipelines were built for that base pass. The liquid pass also shared base-pass
framebuffers. Correct pixels did not make either usage valid.

## Native Vulkan design

At map/sky change, Vulkan now builds the same six textured faces as local cube
coordinates and uploads the 36 `vk_world_vertex_t` records once via staging to
a device-local vertex buffer. UV inset, face ordering, lightmap-white values,
and triangle order remain unchanged. `sky_geometry_dirty` is raised only when
the map bounds or sky definition changes.

The regular per-frame `vk_world_frame_vertex_t` grows from 16 to 64 bytes:

```text
time, refraction scale, effects/refraction flags      16 B
three sky rotation rows (vec4 each)                   48 B
                                                     ------
per current frame                                    64 B
```

The vertex shader identifies the existing sky flag, applies those three rows
to local coordinates, transforms the result through the view matrix with
`w=0`, then assigns `w=1` before projection. This preserves both legacy sky
rotation and the prior camera-relative cube behavior without recomputing or
uploading sky vertices. Non-sky world vertices retain the original projection
path.

The render-pass repair deliberately keeps the liquid load pass compatible with
the base scene pipelines and framebuffers. `VK_SceneCopy_Record` already makes
the copied colour image available to fragment sampling and restores the
swapchain image to colour-attachment layout. A dedicated depth attachment
barrier supplies the required late-fragment-test to early-fragment-test
ordering before the loaded liquid pass. All depth image barriers include the
stencil aspect when the selected depth format contains stencil, avoiding an
invalid depth-only subresource transition on devices where separate
depth/stencil layouts are unavailable.

No OpenGL renderer path is used by this work.

Compatible six-face sky images now additionally copy once into a device-local
six-layer texture array at sky setup. The dedicated native fragment/pipeline
samples the face layer stored in static vertex data and records all 36 sky
vertices in one draw; incompatible images retain the prior six-face native
fallback. Full design, lifecycle, and evidence are in
`vulkan-sky-texture-array-submission-2026-07-15.md`.

## Expected steady-state contract

For the fixed bmodel scene after setup, the static sky performs no per-frame
vertex upload. The expected Vulkan domain counters are:

```text
world_uploads   64 B  (current animated/sky-transform record)
entity_uploads 128 B  (one ordinary inline-BSP instance)
total uploads  192 B
draws            3   (one sky, world, inline BSP in the fixed fixture)
```

The one-time 1,728-byte sky upload occurs before the telemetry sample interval
and is deliberately not treated as a steady-state frame cost.

## Evidence

The refreshed staged runtime passed the complete hidden-native six-face matrix
at `.tmp/renderer-parity/fr01-static-sky-clean` with Vulkan validation enabled.
All six `FR-01-T05` scenes passed; every crop had zero pixels over its RGB
threshold. The rotated-X bottom-sky case passed with mean absolute RGB error
`0.0158, 0.0074, 0.0014`; the largest seam-side mean remained
`0.0984, 0.1554, 0.0263`. The Vulkan logs contain no VUID, validation-error,
device-lost, or fatal diagnostics.

The paired fixed-view collector then completed at
`.tmp/renderer-parity/fr01-renderer-perf-sky-array` with 120 samples per
renderer, 100 post-warmup samples, matching `Intel(R) Iris(R) Xe Graphics`
adapter names, valid Vulkan timestamps, and clean validation diagnostics. Its
`VK_STATS` records show the expected steady-state split and consolidated draws
exactly:

```text
draws=3 vertices=48 uploads=192 world_uploads=64 entity_uploads=128
```

The total is 1,680 bytes (89.74%) lower than the prior static-bmodel capture's
1,872 bytes per sample, and 1,840 bytes (90.55%) lower than the original
2,032-byte baseline. The local provenance records
`driver: not-recorded-local-validation`; CPU/GPU times from this run are not a
cross-run performance claim or a performance budget. The array removes five
Vulkan sky submissions in this fixture; broader material/entity batching and
indirect work remain open.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python -m unittest tools.renderer_parity.test_vulkan_static_sky_stream_source
python -m unittest tools.renderer_parity.test_vulkan_liquid_refraction_source
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_sky_seams_manifest.json --run-root .tmp/renderer-parity/fr01-sky-array-clean --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --run-root .tmp/renderer-parity/fr01-renderer-perf-sky-array --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" --driver "<display-driver>" --vulkan-validation --min-samples 100
```

The source regression checks device-local static sky upload, the 64-byte
frame layout and instance bindings, and camera-relative shader projection. The
liquid regression checks compatible render-pass dependencies, an explicit
depth attachment barrier, and stencil-aware depth transitions.

## Remaining work

This removes a persistent native upload and validation blocker; it does not
establish a general performance budget or Vulkan superiority over OpenGL.
Representative multi-map, hardware/driver-recorded paired captures and the
remaining transient entity/effect submissions are still required.
