# Native Vulkan shared vertical-sync parity

Date: 2026-07-17  
Task: `FR-01-T14` (shared renderer controls and native Vulkan presentation)

## Outcome

Vertical sync now has the archived renderer-neutral Video cvar `r_vsync`.
The legacy OpenGL `gl_swapinterval` spelling remains a synchronized
compatibility alias. The legacy menu, cgame JSON menu, and RmlUi screen all
write the canonical setting.

## Native presentation contract

OpenGL applies the normalized boolean setting through its existing platform
swap-interval callback. Vulkan applies it natively at swapchain creation:
`r_vsync 1` chooses FIFO presentation, while `r_vsync 0` prefers IMMEDIATE,
then MAILBOX when immediate presentation is unsupported, and finally falls
back to the guaranteed FIFO mode.

A live Vulkan cvar change marks only the native swapchain family dirty. The
normal frame boundary waits for submitted work and recreates the swapchain and
its dependent native resources; it does not restart the renderer, reload
assets, or redirect to OpenGL. The cvar is clamped to `[0, 1]` in both paths.

## Regression fixture

`fr01_vsync_runtime.cfg` starts the standard opaque wall fixture with VSync
enabled, loads the map, then sets `r_vsync 0` after the swapchain exists. The
paired headless manifest locks the unchanged material output so a live native
swapchain recreation cannot lose world, replacement-image, or descriptor
state.

## Validation

The focused source gate passed and the current OpenGL/Vulkan DLLs built before
the staged headless matrix ran with `VK_LAYER_KHRONOS_validation`:

```text
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_vsync_runtime_manifest.json \
  --run-root .tmp/renderer-parity/vsync-runtime-final \
  --vulkan-validation
```

After the live `r_vsync 0` transition, the 560 x 420 crop (235,200 pixels)
had exact OpenGL/Vulkan output: zero maximum/mean RGB error and no pixel over
the zero threshold. The authored `[38, 38, 38]` mask contained 219,459 pixels
per backend and the replacement `[174, 174, 174]` mask contained 15,741; both
had an intersection-over-union of `1.0`. Log scanning found no VUID,
validation, map-load, or process-error marker.
