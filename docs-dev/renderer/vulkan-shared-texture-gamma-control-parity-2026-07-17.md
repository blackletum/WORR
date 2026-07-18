# Native Vulkan shared texture-gamma parity

Date: 2026-07-17  
Task: `FR-01-T14` (shared renderer controls and native Vulkan material paths)

## Outcome

Native Vulkan now consumes the existing archived `r_gamma` Video cvar and
keeps legacy `vid_gamma` synchronized. With hardware gamma disabled, the
native material uploader applies the same bounded `[0.3, 3.0]` gamma curve to
wall and skin RGB data as OpenGL. The legacy OpenGL and cgame/RmlUi Video
screens already expose the shared cvar; no Vulkan path redirects to OpenGL.

## Native contract

The native transform excludes `IF_NO_COLOR_ADJUST` material data, runs after
the shared wall-saturation transform, and precedes native picmip/mip
generation. It does not touch alpha or ordinary UI/sprite data. Thus the work
happens only during image registration/reload rather than per draw or per
pixel.

When Windows hardware gamma is active, Vulkan preserves the common platform
gamma-ramp route instead of double-transforming material bytes. The current
software-gamma gate deliberately disables that platform path, so it validates
the native image upload contract deterministically. The linear-light/final
presentation redesign remains separately tracked by `FR-02-T13`.

## Regression fixture

`fr01_world_texture_gamma.cfg` sets `r_hwgamma 0`, `r_gamma 1.3`, and shared
texture saturation zero on the existing authored/replacement opaque wall map.
The target gray material bytes are `38 -> 22` and `174 -> 156` under the
OpenGL gamma formula before filtering.

## Validation

The focused source gate passed, the current renderers built, and the staged
headless matrix ran with `VK_LAYER_KHRONOS_validation`:

```text
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_world_texture_gamma_manifest.json \
  --run-root .tmp/renderer-parity/texture-gamma-final \
  --vulkan-validation
```

The 560 x 420 crop (235,200 pixels) was exactly identical: zero maximum/mean
absolute RGB error and no pixel over the zero threshold. The `22`-gray
authored material mask held 219,459 pixels per backend and the `155`-gray
filtered replacement mask held 15,741; each had IoU `1.0`. Its source material
byte is the expected `174 -> 156`; ordinary texture filtering resolves the
captured replacement receiver one level lower. Log scanning found no VUID,
validation, map-load, or process-error marker.
