# Renderer-Neutral Gameplay Light Query

Date: 2026-07-14

Task ID: `FR-01-T09`

Status: complete

## Outcome

The gameplay `lightlevel` byte no longer calls renderer-owned `R_LightPoint`.
`V_SetLightLevel` now uses `GameplayLight_Query` from the client engine, so
the byte sent to the server has the same source and conversion path whether
the selected renderer is OpenGL, native Vulkan, Vulkan RTX, or no renderer is
active.

The visual renderer APIs deliberately remain unchanged: their `R_LightPoint`
implementations can continue to support visual fullbright, presentation
controls, and dynamic lights. They are no longer part of gameplay state.

## Contract

`inc/client/gameplay_light.h` defines the engine-owned query contract and
`src/client/gameplay_light.c` implements it.

- Source data is the client-owned `cl.bsp`, not renderer-private map state.
- Face lightmaps and lightgrid samples use their authored RGB bytes normalized
  to the `0..1` legacy sample domain, then apply only the authoritative map
  lightstyle white scales.
- Lightgrid sampling retains the existing trilinear interpolation and
  non-occluded-neighbour averaging behavior. Face sampling retains bilinear
  filtering and supports world and interpolated inline BSP-model hits.
- The query intentionally does not apply map overbright shifts, gamma,
  intensity, brightness, entity modulation, fullbright, renderer selection,
  or dynamic lights.
- A missing/invalid static sample returns the documented white fallback
  (`1, 1, 1`), preserving the historical safe gameplay fallback without
  choosing Vulkan's former visual-only `0.75` fallback.
- `GameplayLight_LevelToByte` selects the brightest RGB component, multiplies
  it by 150, rejects NaN, clamps negative values to 0, and saturates positive
  infinity and over-range values at 255 before the value enters the uint8
  user-command serialization path.

This is intentionally a normalized legacy authored-light domain, not a new
linear-light rendering contract. `FR-02-T13` still owns the latter.

## Integration

`src/client/view.cpp` builds a query from the current client BSP, map
lightstyles, and submitted inline-model entities. It invokes the query before
the refdef is rendered and assigns `cl.lightlevel` through the standalone
byte-conversion helper. There is no renderer export or renderer cvar input in
this call path.

Inline-model tracing follows the established OpenGL light-point behavior:
world is traced first, eligible inline BSP models are traced in transformed
space, and the nearest hit is sampled. When a lightgrid is present it remains
the higher-priority static sample source, matching the pre-existing renderer
query rule.

## Regression coverage

`tools/renderer_parity/gameplay_light_test.c` is a renderer-free C regression
target. It uses controlled BSP lookup stubs and verifies:

- bilinear face sampling with lightstyle scaling and finite extreme
  coordinates;
- `1xN` and `Nx1` lightmaps, including NaN and infinity texture coordinates;
- the `255` missing-style terminator without reading a nonexistent style map;
- trilinear lightgrid sampling, static-grid precedence, and nearest inline
  BSP-model selection;
- the white no-data fallback; and
- below-zero, nominal, over-range, NaN, positive-infinity, and
  negative-infinity protocol-byte conversion.

The native Vulkan and OpenGL backends are absent from this test binary by
design. This directly demonstrates the intended backend independence instead
of comparing two backend-adjusted visual queries.

## Validation

Completed successfully:

```text
ninja -C builddir-win gameplay_light_test.exe
meson test -C builddir-win --suite renderer --no-rebuild --verbose
# 1/1 renderer-gameplay-light: OK

ninja -C builddir-win worr_engine_x86_64.dll
# gameplay_light.c and view.cpp compiled; engine DLL linked

python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew
python tools/test_package_assets.py
# 14 tests: OK; staged package contains 331 assets

python -u tools/renderer_parity/run_vk_debug_smoke.py --install-dir .install --run-root .tmp/renderer-parity/fr01-vk-debug-t09-final --vulkan-validation --json-output .tmp/renderer-parity/fr01-vk-debug-t09-final/results.json
# exit 0; 3,067 changed pixels; maximum channel delta 231; no failures
```

The focused unit suite is the headless/reference path. Since every renderer
selection executes the same client-engine `GameplayLight_Query` call before
rendering, identical BSP positions and lightstyles produce identical gameplay
lightlevel bytes across OpenGL and native Vulkan without renderer routing.

## Files

- `inc/client/gameplay_light.h`
- `src/client/gameplay_light.c`
- `src/client/view.cpp`
- `tools/renderer_parity/gameplay_light_test.c`
- `meson.build`
