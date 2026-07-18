# Native Vulkan shared texture-intensity parity

Date: 2026-07-17  
Task: `FR-01-T14` (shared renderer controls and native Vulkan material paths)

## Outcome

Texture intensity is now exposed through the archived renderer-neutral
`r_intensity` cvar in each Video UI implementation: the legacy menu, cgame
JSON menu, and RmlUi screen. The prior `intensity` spelling remains a synchronized
compatibility alias for existing configurations and commands. No Vulkan path
redirects rendering to OpenGL.

## Native contract

OpenGL keeps its established GLS shader control: `r_intensity` and `intensity`
stay synchronized, clamp to `[1, 5]`, and multiply only material paths whose
OpenGL state sets `GLS_INTENSITY_ENABLE`.

Vulkan registers and synchronizes the same pair in `vk_world.c`, removes the
irrelevant texture-reload flag, clamps to the identical range, and writes the
value to its existing per-frame native receiver uniform. The world shader and
the inline-BSP/model entity shader consume that uniform only for their matched
`*_VERTEX_INTENSITY` material flags. This preserves OpenGL's eligibility rules
without rebuilding images, static geometry, descriptors, or pipelines when a
player changes the setting.

## Regression fixture

`fr01_world_texture_intensity.cfg` runs the existing opaque texture-replace
wall fixture with canonical `r_intensity 2` and luminance-only source texture
data. Its paired manifest requires the doubled authored wall receiver mask in
the standard 560 x 420 crop and compares both native backends headlessly.

## Validation

The focused source gate passed, and the staged headless capture matrix ran
with `VK_LAYER_KHRONOS_validation`:

```text
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_world_texture_intensity_manifest.json \
  --run-root .tmp/renderer-parity/texture-intensity-final \
  --vulkan-validation
```

The `560 x 420` crop (235,200 pixels) was exactly identical: maximum and mean
absolute RGB error were zero, with no pixel over the zero threshold. The
`[75, 75, 75]..[77, 77, 77]` doubled-intensity feature mask contained 219,459
pixels for each backend and had IoU `1.0`. Log scanning found no VUID,
validation, map-load, or process-error marker.
