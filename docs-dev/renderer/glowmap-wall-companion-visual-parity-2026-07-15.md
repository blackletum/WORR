# Wall Glowmap Companion Visual Parity

Date: 2026-07-15

Task ID: `FR-01-T11`

Status: wall receiver visual gate complete. The paired model-skin gate is
recorded separately in `model-skin-glowmap-visual-parity-2026-07-15.md`.

## Purpose

The existing native Vulkan glowmap implementation had source-level coverage,
but no paired non-window evidence for a wall glow companion. This change adds
an authored deterministic world fixture and corrects a baseline lookup edge
case uncovered by that fixture.

## Companion resolution contract

Glow companions use a canonical `*_glow.pcx` name. The default texture
replacement preference still applies: truecolour companion files take
precedence over the PCX. If no replacement is present, both native renderers
load the canonical PCX. A missing wall companion must not be interpreted as a
same-stem WAL, because that file is the base material rather than emission
data.

OpenGL now uses its dedicated `load_glow_image_data` path to implement that
rule. Vulkan retains native ownership with its dedicated
`VK_UI_LoadGlowmapData` path during `VK_UI_AssociateGlowmap`; it does not route
any image or rendering work through OpenGL.

## Fixture and gate

`tools/renderer_parity/generate_glowmap_fixture.py` deterministically writes:

- `assets/maps/worr_fr01_glowmap.bsp`, a lightmapped world wall using
  `parity/fr01_bm_bg`;
- `assets/textures/parity/fr01_bm_bg_glow.pcx`, a conventional 16-by-16,
  opaque, indexed PCX companion.

`assets/renderer_parity/fr01_glowmap.cfg` fixes the shared glowmap control and
the OpenGL intensity control at one, disables fullbright, and captures from a
fixed camera. Its manifest samples a 250-by-200 wall-only rectangle and
requires both exact RGB equality and full matching coverage of the expected
`[48, 80, 144]` glow-lit wall colour. The fixture deliberately excludes the
green inline BSP model: that material's non-glow lighting difference is owned
by the separate inline-BSP parity work and must not be hidden by a glowmap
threshold.

## Evidence

The no-window paired run with Vulkan validation at
`.tmp/renderer-parity/fr01-glowmap-final-override-contract` completed with:

- 50,000 compared wall pixels;
- maximum and mean absolute RGB error of `[0, 0, 0]`;
- zero pixels above the zero-error threshold;
- 50,000 matching glow-colour pixels in each backend and intersection-over-
  union `1.0`.

## Validation

```text
python tools/renderer_parity/generate_glowmap_fixture.py --validate
python -m unittest tools/renderer_parity/test_generate_glowmap_fixture.py
python -m unittest tools/renderer_parity/test_vulkan_glowmaps_source.py
meson compile -C builddir-win
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_glowmap_manifest.json --run-root .tmp/renderer-parity/fr01-glowmap-final-override-contract --vulkan-validation --scene wall_glowmap_opaque_companion
```

The companion model-skin result is deliberately separate, so the zero-error
wall assertion remains scoped to the wall receiver rather than masking model
differences. See `model-skin-glowmap-visual-parity-2026-07-15.md`.
