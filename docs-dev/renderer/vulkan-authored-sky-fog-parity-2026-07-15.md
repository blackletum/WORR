# Native Vulkan Authored Sky-Fog Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: native Vulkan sky-only fog now has retained OpenGL comparison evidence
through the real six-face sky path. Translucent and effect receivers remain
open.

## Fixture

The existing `base1` sky seam fixture now has a sibling entity override at
`assets/renderer_parity/fr01_sky_fog/base1.ent`. It retains the `unit1_`
six-face sky and authors global fog color/density plus `fog_sky_factor 0.60`.
`fr01_sky_fog.cfg` enables `gl_fog` and native `vk_fog`, launches `base1` with
the override, and uses the deterministic side view. This exercises the
sky-specific branch: global depth and height fog are bypassed while the sky is
mixed with the authored fog color by the sky factor.

## Gate and result

The retained manifest compares the 350 x 220 sky region. The maximum renderer
difference is `2 / 1 / 1`, with mean absolute RGB `0.036 / 0.063 / 0.011` and
no pixels beyond an RGB error of two. The narrow limits are intentionally
slightly above that observation (`0.05 / 0.08 / 0.02`) to retain the native
raster rounding envelope.

The fog-colored coverage probe requires 74,000 pixels per backend in the
`120/80/100` through `130/115/115` range. It records 75,379 OpenGL and 75,480
Vulkan pixels, a 0.134% count delta and 0.99866 intersection-over-union;
these are within the retained `0.2%` and `0.998` bounds. The same region differs
from the clear six-face capture at every one of its 77,000 pixels, confirming
the fog route is active. Vulkan validation emitted no diagnostics.

## Remaining scope

The global, height, and sky world/inline-BSP fog routes now have visual gates.
Fog coverage for transparent world materials, particles, beams, flares, and
other effect receivers remains `FR-01-T12` work.
