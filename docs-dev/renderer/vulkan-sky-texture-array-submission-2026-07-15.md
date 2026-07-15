# Vulkan Sky Texture-Array Submission

Date: 2026-07-15

Task IDs: `FR-01-T05`, `FR-01-T13`, `FR-01-T15`

Status: implemented and validation-backed. Compatible six-face sky sets now
use one native Vulkan draw without changing their static geometry, rotation, or
pixel output. The established per-face Vulkan route remains a native fallback.

## Purpose

After static sky geometry removed the 1,728-byte per-frame cube rewrite, the
fixed bmodel telemetry still recorded eight Vulkan draws: six independent sky
faces, the world, and the inline BSP model. The source sky images were already
native Vulkan images, but each face required a descriptor bind and draw.

## Native design

`VK_World_SetSky` now attempts a one-time texture-array build after all six
source images have loaded. It accepts the fast path only when every face is a
distinct native Vulkan image with the same positive dimensions. Vulkan UI
images expose their `VkImage` and are created with transfer-source usage for
this native copy; ordinary UI rendering continues to sample the same image.

The world renderer allocates a device-local `VK_FORMAT_R8G8B8A8_UNORM` image
with six layers and a `VK_IMAGE_VIEW_TYPE_2D_ARRAY` view. A one-time native
command buffer transitions the six sources from shader read to transfer read,
transitions the new image from undefined to transfer destination, copies each
face to its matching layer, then restores all seven images to shader-read
layout before the descriptor is created. Old array image/view/memory and the
external descriptor are destroyed before source sky images are unregistered.

The 36 static sky vertices retain their original position, UV, colour, flags,
and triangle order. Only on the array path, `lm_uv.x` contains the integral
face layer. `vk_world_sky.frag` samples it through `sampler2DArray`, applies
the existing sky-fog UBO prefix, and is compiled into a dedicated sky-array
pipeline. `VK_World_RecordOpaque` therefore binds the one descriptor and
submits all 36 vertices in one call. The existing regular world fragment
pipeline, white lightmap coordinates, six descriptors, and six draws remain
active when faces are incompatible or allocation/copy setup fails.

No OpenGL path is called or substituted by this work.

## Evidence

The hidden native six-view matrix at
`.tmp/renderer-parity/fr01-sky-array-clean` passed all `FR-01-T05` thresholds
with Vulkan validation enabled. Every crop had zero pixels over its RGB
threshold. Its rotated bottom view retained mean absolute RGB error
`0.0158, 0.0074, 0.0014`; the most divergent seam side remained
`0.0984, 0.1554, 0.0263`. Vulkan process logs contain no VUID,
validation-error, device-lost, or fatal diagnostics.

The bmodel/legacy-lightmap matrix at
`.tmp/renderer-parity/fr01-bmodel-sky-array-clean` remains exact: both crops
have zero RGB error and intersection-over-union `1.0`, also with clean Vulkan
validation logs.

The paired fixed-view capture at
`.tmp/renderer-parity/fr01-renderer-perf-sky-array` collected 120 samples per
backend and 100 post-warmup samples with matching Intel Iris Xe adapter names
and valid Vulkan timestamps. `VK_STATS` records exactly:

```text
draws=3 vertices=48 uploads=192
world_uploads=64 entity_uploads=128
```

This is the intended reduction from eight to three Vulkan submissions in this
fixture while retaining the static-sky steady-state upload contract. OpenGL
reports two profiler draws, but its sky submission is not instrumented under
the identical scope. The local driver provenance is
`not-recorded-local-validation`; its CPU/GPU timings are not a cross-backend
speed claim or a performance budget.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python -m unittest tools.renderer_parity.test_vulkan_static_sky_stream_source
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_sky_seams_manifest.json --run-root .tmp/renderer-parity/fr01-sky-array-clean --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-sky-array-clean --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --run-root .tmp/renderer-parity/fr01-renderer-perf-sky-array --hardware-id "GPU=<adapter>; OS=<version>" --driver "<driver>" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-renderer-perf-sky-array/vulkan.log --opengl .tmp/renderer-parity/fr01-renderer-perf-sky-array/opengl.log --capture-manifest .tmp/renderer-parity/fr01-renderer-perf-sky-array/capture.json --warmup 20 --min-samples 100
```

## Remaining work

This optimization is limited to compatible 2D six-face sky sets and does not
remove broader world-material, entity, alpha, effect, or indirect-submission
work. Representative multi-map, driver-recorded paired measurements are still
required before accepting a general Vulkan performance budget.
