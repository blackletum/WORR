# Native Vulkan HUD Filtering and Crosshair Parity

Date: 2026-07-15

Task ID: `FR-02-T05`

Status: native Vulkan now matches the default OpenGL point-filter contract for
the representative gameplay crosshair. This closes one HUD visual defect and
adds a durable gate; it is not full HUD coverage.

## Defect

The first headless gameplay-HUD probe enabled `cl_draw2d` and rendered the
classic 32-pixel crosshair over the fixed first-frame map view. OpenGL produced
the expected twelve opaque white center pixels. Vulkan drew the same geometry
with blurred half-intensity samples because its UI descriptor path always used
a linear sampler. One-pixel paletted crosshair lines therefore blended against
their transparent texel neighbors. The provisional comparison measured 40
different pixels in the 100 x 100 center crop.

The defect was native Vulkan sampling behavior, not an OpenGL fallback or a
capture configuration difference.

## Implementation

`src/rend_vk/vk_ui.c` now owns both linear and nearest samplers for repeat and
clamp addressing. Descriptor selection follows the OpenGL 2D policy:

| Image class | Vulkan policy | Native cvar |
|---|---|---|
| Explicit `IF_NEAREST` | Always nearest | none required |
| Font | Nearest by default | `vk_bilerp_chars` (`0`) |
| Picture | Nearest by default; scrap accepts the OpenGL-compatible intermediate mode | `vk_bilerp_pics` (`0`) |
| Sky | Linear by default | `vk_bilerp_skies` (`1`) |
| Wall/skin | Existing linear material sampler | not changed |

Changing a `vk_bilerp_*` value drains the native device before rewriting the
resident image descriptors. This rare preference path prevents a descriptor
sampler rewrite while a prior frame may still reference it. Post-process
external-image descriptors retain their existing linear clamp sampler.

## Regression fixture

`assets/renderer_parity/fr01_hud_crosshair.cfg` creates a deterministic
headless gameplay view with `cl_draw2d 1`, `cl_alpha 1`, fixed crosshair 3,
white color, and all hit/pulse animation disabled.
`assets/renderer_parity/fr01_hud_crosshair_manifest.json` compares its central
100 x 100 crop with zero RGB tolerance and additionally requires exactly
matching opaque-white crosshair coverage:

```text
minimum white pixels per renderer: 12
backend count delta:               0.0%
intersection-over-union:           1.0
```

The completed validation run initially used an isolated, freshly staged runtime
because unrelated processes held a DLL in the shared `.install` tree:

```text
python tools/refresh_install.py --build-dir builddir-win \
  --install-dir .tmp/renderer-parity/hud-filter-stage \
  --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .tmp/renderer-parity/hud-filter-stage \
  --manifest assets/renderer_parity/fr01_hud_crosshair_manifest.json \
  --run-root .tmp/renderer-parity/fr01-hud-crosshair-final \
  --vulkan-validation
```

Both native renderers completed, Vulkan emitted no validation diagnostics, and
the report records zero maximum/mean RGB error across all 10,000 crop pixels.
Both masks contain 12 pixels with a 1.0 intersection-over-union.

Once those unrelated processes exited, the shared canonical `.install` staging
root was refreshed and the identical validation-enabled headless capture was
repeated there. Package validation succeeded with 349 assets (including 215
RmlUi paths); the repeated exact image/mask result and a final Vulkan-log scan
were clean.

## Classic status-bar extension

The same deterministic gameplay view exposes the classic lower status bar:
health, armor, ammo, and the selected-weapon icon. The added
`assets/renderer_parity/fr01_hud_statusbar_manifest.json` crops its 400 x 60
pixel lower-HUD region and requires exact zero-error RGB parity. A black-detail
mask also requires at least 200 opaque status-bar pixels on each backend with a
1.0 intersection-over-union, preventing an empty lower crop from passing.

The final canonical-install capture reports zero maximum/mean RGB error over
all 24,000 status-bar crop pixels; the opaque-detail mask contains 294 pixels
on both renderers with a 1.0 intersection-over-union. This adds deterministic
classic status data to the HUD gate, but still does not cover live inventory,
chat, modern match HUD, weapon wheel, hit markers, or animated notifications.

Focused verification also passed:

```text
python -m unittest tools/renderer_parity/test_vulkan_ui_filter_parity_source.py \
  tools/renderer_parity/test_vulkan_ui_device_local_stream_source.py
ninja -C builddir-win worr_vulkan_x86_64.dll
```

## Remaining scope

This fixture proves the native 2D picture path for a fixed crosshair only. It
does not cover status-bar data, inventory, chat, hit markers, weapon wheel,
damage/POI animation, split-screen scaling, or arbitrary HUD images. Those
remain `FR-02-T05` work. Canonical `.install` refresh must also be retried once
the unrelated staged runtime processes release their DLL locks.
