# Vulkan Static Inline-BSP Residency

Date: 2026-07-15

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: implemented for ordinary inline BSP entities; visual parity and
validation evidence recorded. This is a targeted steady-state upload reduction,
not completion of the renderer performance objective or a general performance
budget.

## Problem

`VK_Entity_AddBspModel` had correct output, but it transformed every visible
inline-BSP triangle on the CPU every frame. It appended world-space position,
normal, texture/lightmap UV, color, and flags to the entity vertex stream, then
copied the stream through each frame slot's staging buffer into a device-local
buffer. The fixed `worr_fr01_bmodel_first_frame` view made that cost visible in
the paired telemetry baseline: Vulkan reported 2,032 entity upload bytes per
sample while OpenGL reported zero streamed bytes.

That structure was particularly wasteful for ordinary doors, platforms, and
`func_wall` entities: their topology, UVs, local plane normals, face lightmap
coordinates, alpha caps, and material flags do not change after map load.
Only the entity transform, entity lighting classification, and entity alpha
change per frame.

## Native Vulkan design

The renderer now builds immutable geometry for all inline model face ranges of
the active BSP on first eligible bmodel use:

```text
BSP local face triangles
    -> device-local vk_bmodel_gpu_vertex_t buffer (once per map)
    -> frame-local vk_bmodel_gpu_instance_t buffer (one record per bmodel)
    -> vk_entity_gpu_bmodel.vert
    -> existing native entity fragment/lightmap/shadow path
```

`vk_bmodel_gpu_vertex_t` stores model-space position, base/lightmap UV,
model-space plane normal, a quantized per-surface alpha ceiling, and static
surface flags. It intentionally keeps triangle-fan expansion compatible with
the existing malformed-edge skipping behavior; the optimization changes
residency and transformation, not BSP topology interpretation.

The 128-byte instance record stores origin, scaled axes, inverse-scale-correct
normal axes, packed entity colour, and dynamic lighting flags. The shader uses
the same axis representation as the existing native GPU MD2/MD5 paths. It
therefore preserves interpolated origin, rotation, non-uniform scale, normal
transformation, dynamic-light/shadow world-space inputs, lightmapped-white
modulation, fullbright, alpha-test, glowmap, and intensity behavior without
routing any work through OpenGL.

Dedicated Vulkan vertex-input layouts and opaque/alpha/additive pipelines bind
the static vertex buffer plus the current frame's instance buffer. The command
uses `firstInstance` to reuse one instance record across every visible face of
the same bmodel. Static geometry is retired only after the graphics queue is
idle when the active BSP changes, preventing a submitted frame from retaining
a destroyed buffer.

## Compatibility boundary

The fast path is deliberately conservative. Bmodels carrying shell, outline,
rimlight, or item-colourize flags retain the established CPU-expanded native
Vulkan path. Pipeline/resource allocation failure also falls back to that
path. This preserves the previously validated behavior while avoiding a
partial special-pass implementation being presented as parity.

Neither branch redirects to OpenGL.

## Evidence

The hidden native-surface matrix was rerun from the refreshed `.install/`:

```text
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json \
  --run-root .tmp/renderer-parity/fr01-bmodel-static-sky-clean \
  --vulkan-validation

python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json \
  --run-root .tmp/renderer-parity/fr01-bmodel-static-sky-clean \
  --compare-only \
  --json-output .tmp/renderer-parity/fr01-bmodel-static-sky-clean/results-runner.json
```

Both FR-01-T06 scenes passed with zero maximum and mean RGB error, zero pixels
over threshold, and bmodel/legacy-lightmap mask IoU `1.0`. The validation
capture log contains no VUID, validation-error, device-lost, or fatal-error
text. The hidden client remains a native Vulkan HWND and does not foreground or
display a window.

The same fixed-view telemetry collector then completed with 120 matching-adapter
samples per renderer and 100 valid post-warmup samples:

```text
evidence root: .tmp/renderer-parity/fr01-renderer-perf-static-bmodel
adapter:       Intel(R) Iris(R) Xe Graphics (both renderers)
fixture SHA:   1b65468863a0d849a319aaf9b12ec00abdf7cfd995fc733b6aca99b4b70ae873
config SHA:    f34b460305830e00305e0f3b42577b4e98e006344b56688b23544f67dfac4ad4
Vulkan uploads: 1,872 bytes mean/p95
OpenGL uploads:  0 bytes mean/p95
Vulkan draws:    8 mean/p95
OpenGL draws:    2 mean/p95
```

Against the prior capture of the identical fixture/configuration, Vulkan's
steady-state upload counter drops from 2,032 to 1,872 bytes per sample: 160
bytes (7.87%). The static BSP allocation occurs before the opt-in sample
interval, so this counter reflects the compact bmodel instance stream rather
than map-residency upload.

The subsequent domain telemetry explicitly attributes 128 of those bytes to
`entity_uploads`: one `vk_bmodel_gpu_instance_t` record. The remaining 1,744
bytes were world work, later localized to the per-frame sky-cube rewrite and
removed by the static-sky follow-up documented in
`vulkan-static-sky-and-liquid-validation-2026-07-15.md`.

The local validation manifest records `driver: not-recorded-local-validation`;
CPU/GPU timings from it are not suitable for a cross-run performance claim or
a budget. The collector passed its integrity conditions only: matching adapter,
fixture/config/log hashes, required samples, valid Vulkan timestamps, and clean
validation diagnostics. A representative repeated hardware/driver-recorded
scene family is still required before claiming performance superiority.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools.renderer_parity.test_vulkan_gpu_bmodel_submission_source
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-static-vk --compare-only
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --run-root .tmp/renderer-parity/fr01-renderer-perf-static-bmodel --hardware-id "GPU=<adapter>; OS=<version>" --driver "<driver>" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-renderer-perf-static-bmodel/vulkan.log --opengl .tmp/renderer-parity/fr01-renderer-perf-static-bmodel/opengl.log --capture-manifest .tmp/renderer-parity/fr01-renderer-perf-static-bmodel/capture.json --warmup 20 --min-samples 100
```

The source regression checks immutable device-local source allocation,
frame-local instance upload/binding, dedicated embedded shader/pipelines,
axis-based normal transformation, and explicit CPU fallback for special
bmodels.

## Remaining FR-01 work

This closes only ordinary inline-BSP residency. The broader `FR-01-T14` work
still needs static paths or measured alternatives for remaining expanded
special-pass models, sprites/beams/particles, and a general transient ring
allocator. `FR-01-T15` still needs representative repeated performance
captures and a justified budget; the fixed fixture does not support an overall
Vulkan-versus-OpenGL superiority claim.
