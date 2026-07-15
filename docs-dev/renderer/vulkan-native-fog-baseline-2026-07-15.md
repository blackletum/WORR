# Native Vulkan Fog Baseline

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: partial implementation. Global, height, and sky fog now run in the
native Vulkan world and entity receivers. Bloom/HDR, colour correction, DOF,
CRT, and resolution scaling remain separate work within this task.

## Outcome

`vk_shadow.c` now carries the current `refdef_t` fog parameters in the same
per-frame receiver uniform already used for shadows, lighting gains,
intensity, fullbright, and glow-map intensity. `vk_fog` is a Vulkan-exclusive
toggle with the OpenGL default of `1`. The uniform updates once per rendered
frame from `VK_Shadow_UpdateDlights`; no image, descriptor, render pass, or
per-object allocation is added.

The data exactly preserves the OpenGL inputs:

- global fog colour and `density / 64`;
- height-fog start/end colours and distances, density, and falloff;
- sky fog factor.

## Receiver contract

The native world shader applies global fog followed by the rerelease
height-fog equation after material lighting, glow, and intensity. It keeps
the directional epsilon from the OpenGL shader's height-fog implementation.
The entity shader applies the same composition after skins, lightmapped inline
BSP faces, item colourization, rim lighting, particles, beams, and flares.

Sky geometry receives a dedicated `VK_WORLD_VERTEX_SKY` flag. Sky bypasses
global and height fog, then uses only the global-fog colour with the authored
sky factor—matching OpenGL's separate `fog_bits_sky` material state.

All paths are native Vulkan shader code; no OpenGL renderer function or
framebuffer is involved. The uniform layout remains 16-byte aligned and both
world and entity shaders consume the identical order.

## Performance characteristics

Fog adds no draw calls, descriptor sets, pipeline variants, or CPU geometry
updates. The feature is a branch on a single per-frame bit field, so disabled
fog exits before the exponential calculations. Enabled fog is evaluated in
the existing receiver fragment pass, avoiding an additional full-screen pass
and preserving translucent material order.

## Headless validation

`tools/renderer_parity/test_vulkan_fog_source.py` validates the native cvar,
one-frame uniform update, refdef parameter mapping, global/height/sky shader
equations, world sky marker, and absence of an OpenGL route. Validation runs
without launching an interactive client:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools/renderer_parity/test_vulkan_fog_source.py
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
```

A compliant no-window visual scene must still compare global, height, and sky
fog across both renderers before the complete post-processing task can close.
