# Native Vulkan Scaled MSAA DOF Parity

Date: 2026-07-17  
Project tasks: `FR-02-T13`, `FR-01-T15`  
Status: implemented and validation-backed; broader scaled presentation coverage remains open

## Scope

This follow-up closes the sample-count mismatch exposed when a user requests
both `r_multisamples=4` and fixed resolution scaling. It covers the native
Vulkan path only. No Vulkan frame is redirected through OpenGL, and the shared
`r_multisamples` request remains unchanged.

## Implementation

OpenGL keeps its post-process and scaled offscreen FBOs single-sample even
when the platform framebuffer request is four samples. Vulkan now makes the
same frame-local choice for an MSAA scaled scene by selecting the existing
native one-sample scene/load render-pass family and pipeline variants. The
normal direct-presentation MSAA path is unchanged.

`VK_STATS` distinguishes the two compatibility selections:

- `msaa_single_sample_dof_scene_frames` for active depth-aware DOF;
- `msaa_single_sample_scaled_scene_frames` for a scaled offscreen scene.

The DOF composite also now follows OpenGL's virtual 2D quad contract. It
converts the refdef viewport through `R_UIScalePixelRectToVirtual`, rebases the
bottom-origin OpenGL virtual coordinates for Vulkan's inverted viewport, and
lets raster clipping handle a reduced scene target. An attachment-load render
pass preserves already-composited pixels when an explicit menu DOF rectangle
updates only part of that target.

The native blur coefficient buffer stores the same pre-normalized paired
Gaussian weights emitted by OpenGL's generated shader. The Vulkan blur shader
therefore accumulates directly instead of computing a normalization divide per
fragment.

The telemetry record now includes the previously collected `gpu_scene_ms`
phase explicitly between entity and post-process time. Before this correction,
the format string omitted that field while passing its argument; as a result,
the following validity fields were shifted and could be reported incorrectly.
`vk_stats` remains the one-shot console command and `vk_stats_log` is the
headless per-frame logging cvar.

## Validation

All captures ran headlessly at 960x720 with input disabled; Vulkan capture used
`VK_LAYER_KHRONOS_validation`.

`assets/renderer_parity/fr01_multisample_depth_dof_resolution_scale_manifest.json`
now contains two fixed-half 4x scenes:

- Active inventory-layout DOF passes an explicit visual edge budget: MAE
  `[0.005758, 0.043229, 0.005745]`; 821/307,200 pixels (0.267253%) exceed RGB
  error one, below the 0.3% gate. The localized difference is a high-contrast
  inline-BSP edge in the scaled postprocess sample domain.
- The identical scene with `r_dof=0` is RGB-exact over all 307,200 pixels.

`assets/renderer_parity/fr01_multisample_depth_dof_resolution_scale_menu_manifest.json`
repeats that contract for the explicit `quit_confirm` menu bokeh rectangle.
The active result is bounded to MAE `[0.005697, 0.041803, 0.005697]` and the
same 0.267253% edge allowance; the 4x `r_dof=0` control is RGB-exact.

The separately retained scaled no-menu DOF fixture is also exact. These strict
controls make the bounded active inventory/menu result explicit rather than
hiding it through a relaxed general renderer threshold.

A separate final headless Vulkan telemetry probe used `vk_stats_log=1` on the
fixed-half, 4x DOF fixture. Its steady frames reported both
`msaa_single_sample_dof_scene_frames=1` and
`msaa_single_sample_scaled_scene_frames=1`, with valid GPU timestamps and the
separate `gpu_scene_ms` field. The probe produced no Vulkan-validation
findings. It is retained under
`.tmp/renderer-parity/fr01-msaa-scaled-dof-stats-final/` as local evidence.

## Remaining work

Exercise dynamic scale changes, more explicit menu rectangles, HDR/CRT and
liquid combinations, and representative multi-adapter performance budgets.
