# Vulkan Flare Fog Visual Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: direct flare no-fog visual gate complete.

## Purpose

OpenGL deliberately excludes additive `RF_FLARE` fans from its fog state. The
native Vulkan implementation already expressed that rule with
`VK_ENTITY_VERTEX_NO_FOG`, but source coverage alone could not prove the
occlusion-query flare actually became visible in the non-window capture
environment. This fixture closes that evidence gap without redirecting any
Vulkan work through OpenGL.

## Deterministic scene

`tools/renderer_parity/generate_flare_fog_fixture.py` writes:

- `assets/maps/worr_fr01_flare_fog.bsp`, a global-fog world with a regular
  `misc_flare` entity at a fixed clear line of sight;
- `assets/textures/parity/fr01_flare.tga`, an opaque white custom flare image
  requested through the entity's normal `image` key.

The entity uses the normal extended snapshot, cgame, `RF_FLARE`, and
occlusion-query path. Its fixed radius, lock-angle spawn flag, and explicit
fade limits make the visible fan stable. The capture configuration enables
both fog controls and flares, then refreshes the client flare setting after
connection so the server's optional flare filtering cannot suppress the
ordinary game entity.

## Gate contract

The focused `[400, 220, 400, 400]` crop must have exact OpenGL/Vulkan RGB
equality. It additionally requires at least 98,000 pixels in the visible
flare mask whose red channel is exactly 204, with matching mask counts and
IoU `1.0`. The surrounding world is still under the authored dense fog; the
mask distinguishes the additive fan from the fogged blue backdrop.

## Evidence

The post-build staged no-window run with Vulkan validation at
`.tmp/renderer-parity/fr01-flare-fog-final-staged` passed with:

- 160,000 compared flare-region pixels;
- maximum and mean absolute RGB error `[0, 0, 0]`;
- 98,550 matching visible-flare pixels in each renderer;
- 98,550 intersecting pixels, 98,550 union pixels, and IoU `1.0`.

The fog-disabled control at
`.tmp/renderer-parity/fr01-flare-fog-no-fog-control` confirms the intended
composition instead of merely matching two hidden flares. At the same fan
pixels, the fogged capture is typically `[204, 129, 180]` and the clear
control is `[152, 42, 74]`: the red delta is exactly the world-background
change (`76 - 24 = 52`), leaving the additive flare contribution unchanged at
128. That is the expected no-fog flare behavior; fog affects the backdrop,
not the flare fan.

## Regression and validation

The generated fixture test locks the world fog, the real `misc_flare` entity,
its custom image, line-of-sight placement, and fade/orientation inputs.
`test_vulkan_fog_source.py` locks the OpenGL fog-state omission, native
no-fog flag/shader branch, and the exact visual manifest.

```text
python tools/renderer_parity/generate_flare_fog_fixture.py --validate
python -m unittest tools/renderer_parity/test_generate_flare_fog_fixture.py
python -m unittest tools/renderer_parity/test_vulkan_fog_source.py
meson compile -C builddir-win
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_flare_fog_manifest.json --run-root .tmp/renderer-parity/fr01-flare-fog-final-staged --vulkan-validation
```

All renderer launches use the hidden native-surface capture mode. No
interactive client is launched.
