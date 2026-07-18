# Native Vulkan shared lightmap-saturation parity

Date: 2026-07-17  
Task: `FR-01-T14` (shared renderer controls and native Vulkan material paths)

## Outcome

The Video setting for lightmap saturation is now renderer-neutral. Its canonical
archived cvar is `r_lightmap_saturation` in the inclusive range `[0, 1]`;
the former OpenGL-only `gl_coloredlightmaps` spelling remains a synchronized
compatibility alias. No Vulkan path redirects to OpenGL.

The setting is exposed consistently by the legacy menu, cgame JSON menu, and
RmlUi video screen.

## Native implementation

OpenGL rebuilds its lightmap data when either spelling changes. Its established
ordering remains brightness add, world/entity modulation, luminance-based
saturation, then lightmap upload.

Vulkan registers the same two cvars in `vk_world.c`. A change invalidates the
native light-style cache, so the next normal world update rebuilds affected
atlas pixels and uploads the dirty atlas rectangle. The Vulkan atlas applies
the OpenGL luminance formula after light-style accumulation and normalisation.
CPU-sampled entity lighting applies the same saturation after its brightness
and entity-modulate operations, preserving the `GL_AdjustColor` order for
non-atlas entity receivers.

## Regression fixture

`generate_lightmap_saturation_fixture.py` generates
`worr_fr01_lightmap_saturation.bsp` with a deliberately coloured authored
inline-BSP lightmap `(64, 112, 192)`. The light-data block begins with one
unused RGB triplet so the authored fixture explicitly exercises a non-zero
light-data offset.

The staged map is explicitly mirrored loose by `package_assets.py`, which is
necessary for the no-zlib runtime capture environment. The capture config
sets `r_lightmap_saturation 0`, disables lightmap debug presentation, and
reasserts the renderer controls after map load before capture.

The manifest crop is 520 x 420 pixels. It verifies the desaturated inline
material result with a `[39, 184, 80]..[42, 187, 82]` probe, requiring at
least 45,000 matching pixels per backend and IoU `1.0`.

## Validation

The staged, isolated headless matrix ran with `VK_LAYER_KHRONOS_validation`:

```
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_glowmap_lightmap_saturation_manifest.json \
  --run-root .tmp/renderer-parity/lightmap-saturation-final \
  --vulkan-validation
```

Result: pass; 218,400 pixels in the crop, maximum RGB difference `1/1/0`,
MAE `0.216575/0.216575/0`, zero pixels beyond the one-level threshold, and a
47,300-pixel feature-mask intersection/union of `1.0`. The one-level
red/green difference is the known UNORM rounding boundary; blue is exact.

The focused control/source test, generated-fixture validation, package-assets
test, and OpenGL/Vulkan DLL build also pass.
