# Native Vulkan Authored Global-Fog Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: the global-fog world and inline-model path now has retained exact
OpenGL-versus-native-Vulkan visual evidence. Height fog and sky fog remain
separate open cases.

## Fixture

`tools/renderer_parity/generate_global_fog_fixture.py` creates
`assets/maps/worr_fr01_global_fog.bsp`, reusing the deterministic first-frame
geometry while adding these worldspawn inputs:

```text
fog_color      0.30 0.50 0.70
fog_density    0.50
fog_sky_factor 0.60
```

The worldspawn path makes the normal sgame spawn flow send the fog state to the
client refdef. It deliberately avoids the client `fog` console command, which
is not suitable for a fixed-frame batch capture. The fixture config enables
both `gl_fog` and native `vk_fog`, uses the same fixed view, and disables HUD
and unrelated post-processing.

## Gate and result

`assets/renderer_parity/fr01_global_fog_manifest.json` compares the central
560 x 420 scene region with exact zero-error RGB tolerance and requires an
authored fog-backdrop mask in the range `74/125/172` through `78/129/182`.
At least 180,000 pixels must occur in each renderer with a 1.0 mask
intersection-over-union, so matching unfogged captures cannot pass.

The canonical staged run was:

```text
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_global_fog_manifest.json \
  --run-root .tmp/renderer-parity/fr01-global-fog-authored-probe \
  --vulkan-validation
```

Both renderers produced the 960 x 720 capture, with zero maximum/mean RGB
error across all 235,200 compared pixels. The authored backdrop mask contains
187,900 pixels on both backends with a 1.0 intersection-over-union. Compared
with the clear fixed-view capture, all 235,200 pixels differ and the mean
absolute RGB delta is `46.984 / 87.207 / 100.377`, confirming the fixture is
visibly fogged. Vulkan validation emitted no diagnostics.

## Remaining scope

This gate covers the global-fog world and inline-BSP receiver path only. The
separate authored height-fog scene is documented in
`vulkan-authored-height-fog-parity-2026-07-15.md` and sky fog is documented in
`vulkan-authored-sky-fog-parity-2026-07-15.md`; translucent/effect receivers
and the broader bloom/HDR/DOF/resolution work in `FR-01-T12` remain open.
