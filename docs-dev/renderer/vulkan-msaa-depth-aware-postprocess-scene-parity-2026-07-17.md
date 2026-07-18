# Vulkan MSAA Depth-Aware Postprocess Scene Parity

Date: 2026-07-17  
Project tasks: `FR-02-T13`, `FR-01-T15`  
Status: implemented and validation-backed; broader presentation parity remains open

## Outcome

Native Vulkan now preserves OpenGL's depth-aware DOF output contract when the
user requests `r_multisamples=4`. It does so without redirecting any Vulkan
work to OpenGL and without changing the requested MSAA cvar globally.

Ordinary direct-presentation Vulkan frames still render through the requested
native multisample scene pass. A frame that actually uses depth-aware DOF, or
an MSAA frame that renders to a scaled offscreen scene, instead selects a
native compatible one-sample scene/pass family. This matches OpenGL's internal
single-sample FBO chain before DOF blur or scaled presentation without changing
the requested cvar or routing any work through OpenGL.

## Root cause

OpenGL's `r_multisamples` configures the platform/default framebuffer. Its
`FBO_SCENE`, depth postprocess input, and DOF ping-pong targets remain
`GL_TEXTURE_2D` single-sample. Vulkan's prior native MSAA implementation
correctly resolved depth but supplied genuinely multisampled scene colour to
the DOF blur. That creates a different blur footprint at high-contrast edges,
even when both backends have equivalent resolved depth.

The original four-sample DOF capture quantified the difference as mean absolute
RGB `[0.02712, 0.19895, 0.02625]` with 4.56706% of pixels exceeding RGB error
one. Changing Vulkan depth resolve mode to nearest depth did not change that
envelope, proving that depth resolve was not the cause.

## Native implementation

`vk_main.c` creates compatible one-sample scene and load render passes beside
the normal multisample and no-depth-resolve pass families. Linear-scene frames
also receive a one-sample framebuffer using their existing scene color and
depth attachments.

Every scene pipeline that can draw during the scene pass registers a matching
one-sample pipeline: world, entities, debug/show-tris, sky, alpha/liquid, and
scene UI. `VK_SelectScenePipeline` maps a primary multisample pipeline to its
one-sample companion only while `scene_single_sample_active` is set. Creation
or registration failure fails the affected renderer resource setup rather than
silently binding an incompatible pipeline.

At command recording time, Vulkan selects the companion path only when the
selected scene sample count is greater than one, the compatible framebuffer is
available, and either `VK_PostProcess_UsesDof()` is active or the native scene
extent differs from the presentation extent. The native liquid continuation
selects the matching one-sample load pass while that state is active.
`VK_SceneDepthAttachmentImage` consequently exposes the single-sample scene
depth to its barriers and readers. The state is cleared before bloom and final
presentation.

The scaled DOF composite now reproduces OpenGL's virtual 2D quad instead of
pre-scaling it to the Vulkan scene image. It converts the refdef rectangle
through `R_UIScalePixelRectToVirtual`, rebases OpenGL's bottom-origin virtual
coordinates to Vulkan's inverted viewport, and clips only at rasterization.
An attachment-load render pass preserves prior target pixels for an explicit
menu rectangle. The paired Gaussian coefficients are stored pre-normalized, as
OpenGL emits them into its generated shader, so the native blur loop avoids a
per-fragment normalization divide.

The non-DOF color-resolve path is unchanged. It continues to use requested
native MSAA, including the no-depth-resolve variant whenever liquid refraction,
DOF, and depth-sampled rim bloom are inactive.

`vk_stats` makes the distinction observable:

- `msaa_depth_resolve_elisions` counts ordinary MSAA frames that avoid an
  unnecessary depth/stencil resolve.
- `msaa_single_sample_dof_scene_frames` counts frames that retain a
  multisample request but deliberately use the native OpenGL-compatible
  one-sample DOF scene.
- `msaa_single_sample_scaled_scene_frames` counts the corresponding native
  one-sample compatibility selection for scaled offscreen scenes. It is zero
  on ordinary direct-presentation frames.

## Validation evidence

All launches were headless (`win_headless=1`, input disabled), at 960x720 on
Intel(R) Iris(R) Xe Graphics, with `VK_LAYER_KHRONOS_validation` enabled for
the visual captures.

- The 4x static-world gate remains exact across its 235,200-pixel crop. It
  covers the normal MSAA path and does not take the DOF companion path.
- The strict four-sample DOF gate now passes its unchanged 307,200-pixel
  comparison: maximum RGB error `[1, 1, 1]`, mean absolute RGB
  `[0.004349, 0.004935, 0.002051]`, and zero pixels above error one.
- `fr01_multisample_depth_dof_matrix_manifest.json` extends coverage to eight
  deterministic fixtures: fixed focus, centre-depth automatic range, menu
  rectangle, wide explicit range, and their disabled controls. All eight pass
  with zero pixels above error one; disabled controls are exact.
- `fr01_multisample_depth_dof_refraction_manifest.json` covers the native
  one-sample liquid continuation with refraction and DOF at 4x. The fixture
  explicitly hides the unrelated animated viewweapon. It passes its established
  OpenGL/Vulkan refraction envelope (maximum RGB error one, zero pixels above
  error one, MAE `[0.480072, 0.096634, 0.432409]`), and repeated Vulkan 4x
  captures plus a Vulkan 4x-versus-1x comparison are byte-identical.
- `fr01_multisample_depth_dof_resolution_scale_manifest.json` adds a fixed
  half-resolution, inventory-layout 4x DOF case and a 4x DOF-disabled
  control. The disabled control is RGB-exact across all 307,200 crop pixels.
  The active case is validation-clean and bounded to MAE
  `[0.005758, 0.043229, 0.005745]` with 0.267253% of pixels above RGB error
  one (821 pixels on a single high-contrast inline-BSP edge), below its
  explicit 0.3% / `[0.01, 0.05, 0.01]` gate.
- `fr01_multisample_depth_dof_resolution_scale_menu_manifest.json` proves the
  same native virtual-quad contract for the explicit `quit_confirm` menu
  rectangle. Its active result has MAE `[0.005697, 0.041803, 0.005697]` and
  the same 821-pixel/0.267253% edge bound; its 4x DOF-disabled control is
  RGB-exact across the 307,200-pixel crop. The no-menu scaled DOF fixture also
  remains exact, so the bounded active seam is explicit rather than hidden by
  weakening any strict control.
- The source guardrail
  `tools/renderer_parity/test_shared_multisample_control_source.py` asserts
  companion-pass resources, pipeline selection, the frame-local framebuffer,
  strict matrix, and telemetry. The analyzer test covers the new counter.

## Performance evidence

Paired release telemetry used 120 collected frames and a 20-frame warmup,
leaving 100 samples per backend.

- In the non-DOF 4x static-world workload,
  `msaa_depth_resolve_elisions_mean=1.0` and
  `msaa_single_sample_dof_scene_frames_mean=0.0`. Vulkan GPU-frame mean/p50
  were `1.12504 / 0.99450 ms`, confirming the normal optimized MSAA path.
- In the active four-sample DOF workload,
  `msaa_depth_resolve_elisions_mean=0.0` and
  `msaa_single_sample_dof_scene_frames_mean=1.0`. Vulkan GPU-frame mean/p50
  were `1.15182 / 1.24700 ms`; the matched OpenGL full-frame p50 was
  `1.36600 ms`. This is a focused local run, not a cross-driver product
  performance claim.

## Remaining work

This closes the deterministic MSAA/DOF colour-input mismatch, including the
native liquid continuation and the scaled-scene sample-count mismatch.
`FR-02-T13` remains open for broader HDR, dynamic-scale, menu-rectangle, and
final-presentation output-contract coverage.

The scaled implementation and capture contract are detailed in
`docs-dev/renderer/vulkan-scaled-msaa-dof-parity-2026-07-17.md`.
`FR-01-T15` remains open for representative hardware budgets and renderer-wide
performance work.
