# Vulkan Shared Texture Saturation Control Parity

Date: 2026-07-17

Task ID: FR-01-T14

Status: complete for the shared material-desaturation control slice.

## Outcome

The Video settings routes now write `r_texture_saturation` instead of the
OpenGL-specific `gl_saturation` spelling. OpenGL retains `gl_saturation` as a
compatibility alias and applies the shared value through its established
material upload transform. Vulkan retains the same alias for existing configs
and implements the transform natively while registering its own material
images; it never redirects this work through OpenGL or the Vulkan full-scene
post-process path.

The native Vulkan pass copies only eligible wall RGBA data, calculates the
same weighted luminance (`0.2126`, `0.7152`, `0.0722`) as OpenGL, and blends
the three RGB channels toward that luminance using the shared value. It keeps
alpha unchanged, skips turbulent and `IF_NO_COLOR_ADJUST` material data, and
runs before `r_picmip` reduces the upload base level or Vulkan generates
derived mips. Vulkan glow companions are explicitly marked turbulent on
registration, matching OpenGL's temporary glow-upload flags so desaturation
does not alter authored glow data.

`vk_color_saturation` remains a separate full-scene post-process setting. It
is not used as a substitute for material desaturation.

## Evidence

The durable headless fixture is
`assets/renderer_parity/fr01_world_texture_saturation.cfg`, with its manifest
alongside it. It selects `r_texture_saturation 0` on the static unlightmapped
replacement-world scene, which isolates wall-source colour from lightmap
colour.

The validation-enabled staged run exact-compared all 235,200 pixels of the
560x420 capture crop: maximum and mean RGB error were zero, and no pixel
exceeded threshold zero. The activation probes verify 219,459 authored wall
pixels at `[38, 38, 38]` and 15,741 replacement wall pixels at
`[174, 174, 174]` in both backends, with exact mask intersection (IoU `1.0`).
The Vulkan process log contained no VUID, validation, static-upload range, or
residency diagnostic.

Both `worr_opengl_x86_64.dll` and `worr_vulkan_x86_64.dll` rebuilt successfully.
The focused source suite covers the three Video routes, OpenGL aliasing,
native Vulkan eligibility/formula/order, glow-companion exclusion, and the
activation fixture.
