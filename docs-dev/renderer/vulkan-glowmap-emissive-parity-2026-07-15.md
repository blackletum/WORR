# Native Vulkan Glowmap and Emissive-Material Parity

Date: 2026-07-15

Task ID: `FR-01-T11`

Status: complete for `FR-01-T11`. Native material lookup, world/inline-BSP wall
glow, model-skin emission, runtime intensity, headless structural coverage,
and paired no-window world/model visual gates are implemented.

## Outcome

The Vulkan raster renderer now owns the same paired glow-file convention used
by the OpenGL renderer without forwarding any rendering path to OpenGL. When
an `IT_WALL` or `IT_SKIN` image is registered and `r_glowmaps` is enabled,
`vk_ui.c` derives the canonical `_glow.pcx` name and applies the default native
palettized replacement preference: truecolour siblings are preferred, then
the canonical PCX is used. The paired image is
private to the base image, shares its material type and sampler wrapping, and
is released with the base image. It never falls through to a same-stem base
WAL when the companion is absent.

Skin glow pixels are premultiplied by alpha at registration. Wall glow RGB is
left untouched because, as in OpenGL, only wall glow alpha participates in the
lightmap rule. Missing glow files select a permanent white descriptor and do
not create an alternate pipeline or a failed draw.

## Shader contract

The existing material descriptor is extended with a second combined-image
binding: binding zero is the base image and binding one is its paired glow
image. This keeps the renderer within Vulkan's guaranteed four bound
descriptor sets: world continues to use sets zero through three and entities
use sets zero through two. Their vertex flags distinguish three material
cases:

- static world walls and lightmapped inline BSP faces sample glow alpha and
  interpolate their authored lightmap toward white before shadow, dynamic
  light, brightness, and modulate calculations;
- MD2 and MD5 skins sample premultiplied glow RGB and add it after the lit
  base/tint result, leaving emission independent of receiver lighting;
- skins and walls without a companion file keep the ordinary descriptor and
  bind the white fallback at material binding 1.

This keeps descriptor binding deterministic for every batch without expanding
the set count. A base material descriptor always supplies a valid paired
sampler: real companion image when present, otherwise white fallback. Opaque
world and entity batching retain their existing base-material coalescing and
do not add a separate binding pass.

`r_glowmap_intensity` is a native shared-renderer control with OpenGL's
default of `1` and a runtime clamp of `[0, 10]`. Vulkan adds it to the existing
receiver uniform data rather than rebuilding images or static world geometry.
For surfaces using the existing intensity material flag, it is multiplied by
the active `r_intensity`, matching OpenGL's `u_intensity2` behavior. The
per-frame uniform also makes a cvar change visible to both world and entity
receivers in the next frame.

## Scope and boundaries

Covered material paths are static BSP world walls, lightmapped inline BSP
models, MD2 skins, MD5 replacement skins, and item-colourize model passes.
The implementation deliberately does not claim Vulkan bloom extraction or HDR
emission response; those belong to `FR-01-T12`. The controlled wall and stock
model scenes now run paired no-window captures with Vulkan validation enabled.
They establish this task's world and model receiver contracts, rather than a
claim that every third-party replacement pack has been visually sampled.

## Headless validation

The new non-windowed source regression
`tools/renderer_parity/test_vulkan_glowmaps_source.py` verifies canonical
paired-file lookup/replacement policy, skin premultiplication, native set-4 binding,
separate world and entity batch state, wall alpha/lightmap composition, skin
RGB emission, and uniform-backed intensity control. It also checks that the
native code does not include an OpenGL renderer path.

Validation is run without launching an interactive client:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools/renderer_parity/test_vulkan_glowmaps_source.py
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/renderer_parity/generate_glowmap_fixture.py --validate
python tools/renderer_parity/generate_model_glowmap_fixture.py --validate
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_glowmap_manifest.json --run-root .tmp/renderer-parity/fr01-glowmap-final-override-contract --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_glowmap_manifest.json --run-root .tmp/renderer-parity/fr01-model-glowmap-final-staged --vulkan-validation
```

The staged `.install` tree is refreshed after the DLL build. The visual gates
are documented in `glowmap-wall-companion-visual-parity-2026-07-15.md` and
`model-skin-glowmap-visual-parity-2026-07-15.md`; future evidence must retain
the compliant no-window harness rather than using the retired visible-client
capture path.
