# Vulkan Shared Lightmap Brightness Control Parity

Date: 2026-07-17

Task ID: FR-01-T14

Status: complete for the shared lightmap-brightness control slice.

## Outcome

The Video settings routes now write `r_lightmap_brightness`. OpenGL retains
`gl_brightness` as a synchronized compatibility alias and supplies the shared
value to both its lightmap bookkeeping and receiver uniform. Vulkan retains
the legacy alias, but reads the canonical value through `VK_World_LightmapAdd`
when filling its native shadow/world receiver uniform. No renderer restart,
texture re-upload, or OpenGL fallback is required when this shader-side add
changes.

The add remains clamped to the established `[-1, 1]` engine range; the Video
control keeps its existing practical `[0, 0.3]` range. Existing configs and
scripts that set `gl_brightness` continue to select the same result.

## Evidence

The durable staged fixture is
`assets/renderer_parity/fr01_glowmap_lightmap_brightness.cfg`. It sets
`r_lightmap_brightness 0.2` on the authored lightmap/glow receiver, ensuring
the add is nonzero and observable.

The validation-enabled OpenGL/Vulkan capture exact-compared all 50,000 pixels
of the crop: maximum and mean RGB error were zero, with no pixel above error
zero. Both renderers contained exactly 50,000 `[58, 96, 173]` pixels for the
nonzero lightmap-add probe, with a `1.0` mask IoU. The Vulkan log was free of
VUID, validation, static-range, and residency diagnostics.

Both renderer DLLs rebuilt successfully. Source coverage locks all Video
routes, legacy synchronization, OpenGL's receiver uniform, Vulkan's native
uniform feed, and the nonzero headless fixture.
