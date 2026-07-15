# Native Vulkan Authored Height-Fog Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: the altitude-dependent height-fog world and inline-model path now has
retained exact OpenGL-versus-native-Vulkan visual evidence. Sky-only fog and
translucent/effect receivers remain open.

## Fixture

`tools/renderer_parity/generate_height_fog_fixture.py` creates
`assets/maps/worr_fr01_height_fog.bsp` with worldspawn height-fog inputs:

```text
heightfog_falloff     0.015
heightfog_density     0.20
heightfog_start_color 0.15 0.25 0.55
heightfog_start_dist  -480
heightfog_end_color   0.75 0.50 0.20
heightfog_end_dist    480
```

The generated full-height world face forces the established rerelease
height-fog equation to traverse its start/end colour and distance range. It
uses normal spawn replication into the client refdef rather than an ad-hoc
console command. Both `gl_fog` and native `vk_fog` are explicitly enabled.

## Gate and result

`assets/renderer_parity/fr01_height_fog_manifest.json` exact-compares the
central 560 x 420 scene region. Its gradient-mask range (`20/35/55` through
`35/45/70`) requires at least 100,000 pixels on both backends at 1.0
intersection-over-union, so clear or globally-flat fog cannot satisfy the
gate.

The canonical staged validation run produced zero maximum/mean RGB error across
all 235,200 compared pixels. The retained height-fog gradient mask contains
106,004 pixels on both backends with a 1.0 intersection-over-union. Against the
clear fixed view, all 235,200 pixels differ; 193,854 exceed RGB error 8 and the
mean absolute RGB delta is `22.161 / 16.692 / 13.368`. Vulkan validation
emitted no diagnostics.

## Remaining scope

Global, height, and sky world/inline-BSP fog now have visual gates. Transparent
and effect receiver evidence plus the rest of the broader post-processing task
remain `FR-01-T12` work.
