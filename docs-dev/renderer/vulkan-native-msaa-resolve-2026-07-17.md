# Native Vulkan MSAA Resolve Path

Date: 2026-07-17  
Project tasks: `FR-01-T15`, `FR-02-T13`, `FR-03-T11`  
Status: implemented and validation-backed; broader presentation/performance work remains open

## Outcome

The Video anti-aliasing control is now the shared archived `r_multisamples`
control in the legacy menu, cgame JSON menu, and RmlUi Video page. OpenGL and
native Vulkan both use the request. `gl_multisamples` remains a synchronized,
non-archived compatibility spelling for existing configs and console scripts.

Vulkan no longer accepts the setting as a silent no-op. It creates native
multisampled scene color and depth/stencil attachments, resolves colour at the
end of every scene pass, and clamps an unsupported request to `0` in both cvar
spellings. It resolves depth/stencil only when a later native pass consumes
resolved scene depth.

## Native design

`vk_main.c` intersects the physical-device framebuffer color/depth sample
limits with format-specific image sample limits. It selects the largest legal
count no greater than the requested count (`64`, `32`, `16`, `8`, `4`, then
`2`). MSAA is enabled only when the device exposes the required
`VK_KHR_create_renderpass2` and `VK_KHR_depth_stencil_resolve` functionality
and `vkCreateRenderPass2KHR` can be loaded.

The per-frame resources are deliberately split:

- A multisampled color image and depth/stencil image receive scene rendering.
- The existing single-sample scene color/depth images receive resolves and
  remain the inputs to liquid refraction, bloom, screenshots, post-processing,
  and presentation. Active depth-aware DOF with an MSAA request, and any MSAA
  frame using a scaled offscreen scene, instead select a native compatible
  single-sample scene family, matching OpenGL's internal FBO input contract
  before blur or scaled presentation.
- A full-resolution presentation depth image is allocated only when a scaled
  scene requires a distinct presentation framebuffer depth attachment.

The native scene and liquid load render passes use `VkRenderPassCreateInfo2`.
Their subpass resolves color normally and chains
`VkSubpassDescriptionDepthStencilResolve` with sample-zero depth/stencil
resolve modes. This keeps a deterministic resolved depth receiver available
after opaque and post-liquid rendering without copying depth through OpenGL or
falling back to it.

For an MSAA scene that has no depth reader, Vulkan also creates a compatible
render-pass2 variant. It retains the colour resolve but sets both depth and
stencil resolve modes to `VK_RESOLVE_MODE_NONE` and omits the resolve
attachment. `VK_RecordCommandBuffer` selects it only when liquid refraction,
depth-aware DOF, and the depth-sampled rim-bloom extraction are all inactive.
The frame-local `vk_stats` telemetry records each selection as
`msaa_depth_resolve_elisions`; failure to create the optional variant leaves
the correct resolving pass in use. This is entirely native Vulkan work: it
does not reuse an OpenGL render target or renderer path.

World, entity, and debug scene pipelines now use `ctx->scene_samples`; bloom
extract and depth-sampling passes remain single-sample by design. UI owns a
second scene-pipeline pair when MSAA is active, so in-scene 2D/RmlUi draws have
a pipeline compatible with the multisampled scene render pass. No-world entity
previews are also submitted in that scene pass under MSAA, then resolved before
later presentation overlays.

For an actual depth-aware DOF frame, Vulkan creates and selects native
single-sample scene and load render passes, a linear-scene companion
framebuffer, and registered one-sample companions for world/entity/debug scene
pipelines. The selection is frame-local and only applies while DOF consumes the
scene; it is not a cvar downgrade and never routes rendering through OpenGL.
`msaa_single_sample_dof_scene_frames` exposes the choice in `vk_stats`.
`msaa_single_sample_scaled_scene_frames` separately exposes the scaled-scene
selection, keeping direct native-MSAA presentation measurable.

## Evidence

All launches were headless (`win_headless=1`, input disabled) on
Intel(R) Iris(R) Xe Graphics at 960x720.

- The source guardrail
  `tools/renderer_parity/test_shared_multisample_control_source.py` asserts
  shared UI bindings, alias synchronization, extension/capability selection,
  color and depth resolves, sample-compatible pipelines, and no-world preview
  placement.
- `fr01_multisample_static_world_manifest.json` launches both renderers with
  `r_multisamples=4`. Vulkan reports `native scene MSAA enabled at 4x`; OpenGL
  reports `framebuffer MSAA 4x`. The validation-enabled paired capture has no
  validation findings and exact RGB output across its 235,200-pixel crop.
- The validation-enabled guarded RmlUi main-shell route renders 372 frames at
  4x on native Vulkan, then passes the 691,200-pixel paired main-shell
  envelope against OpenGL (MAE `0.29308 / 0.24923 / 0.19777`, 0.94184% above
  threshold eight; all within the existing manifest limits).
- The validation-enabled guarded RmlUi `players` route likewise renders 372
  frames at 4x without an error. That route submits the native
  `RDF_NOWORLDMODEL` player-preview subview, exercising the resolved-scene
  placement used for menu entity previews.
- Release telemetry collected 100 warm samples per backend and sample count.
  Vulkan's mean full-frame GPU time changed from `0.93283 ms` at 0x to
  `1.93155 ms` at 4x (2.0706x); CPU time changed from `0.31806 ms` to
  `0.40665 ms` (1.2785x). OpenGL's corresponding full-frame telemetry changed
  from `0.20115 ms` to `0.35573 ms` (1.7685x). These numbers measure MSAA
  cost on one integrated adapter, not a cross-driver performance claim. They
  predate the global-fullbright static-world specialization and are retained as
  the historical baseline for that MSAA implementation slice.
- A current 100-sample rerun with the native global-fullbright path selected
  changes Vulkan's 0x full-frame mean from `0.44018 ms` to `1.97537 ms` at
  4x (4.4876x); OpenGL changes from `0.23084 ms` to `0.27364 ms` (1.1854x).
  The Vulkan opaque-world mean rises only from `0.37880` to `0.56036 ms`, while
  the scene span rises from `0.40925` to `1.94674 ms`. That isolates the new
  bottleneck to scene-render-pass completion/resolution rather than the
  optimized world fragment path. Both Vulkan samples select one fog-free
  texture-replace world draw and retain three total draws/192 upload bytes.
- The conditional no-depth-resolve follow-up uses that same 4x static-world
  scene. Its validation-enabled paired capture remains exact across all
  235,200 crop pixels with no Vulkan validation findings. The new telemetry
  counter is `1.0` for every analysed Vulkan frame, confirming that the
  no-depth variant, rather than the ordinary depth-resolving pass, was used.
- On that fixed Intel Iris Xe run, 100 warm Vulkan samples reduce mean full
  GPU-frame time from `1.97537 ms` to `1.10714 ms` (43.95%) and median from
  `2.44650 ms` to `1.01850 ms` (58.37%). Mean scene span falls from
  `1.94674 ms` to `1.05662 ms` (45.72%); the draw count and upload volume are
  unchanged. The timing is a focused before/after measurement on one adapter,
  not a renderer-to-renderer comparison.
- The strict four-sample DOF manifest keeps both `r_dof=1` and
  `r_multisamples=4` active at process startup and now passes unchanged:
  maximum RGB error `[1, 1, 1]`, mean absolute RGB
  `[0.004349, 0.004935, 0.002051]`, and zero pixels above error one over
  307,200 pixels. An eight-scene four-sample matrix adds centre-depth, menu
  rectangle, wide-range, and disabled-control coverage; all enabled scenes
  pass the same zero-pixel-over-one threshold and every disabled control is
  exact. The 100-warm-sample DOF telemetry records
  `msaa_single_sample_dof_scene_frames=1.0` and
  `msaa_depth_resolve_elisions=0.0`, proving that the native compatible scene
  family—not an accidental no-depth fast path—is selected.
- The fixed-half scaled 4x DOF fixture is validation-clean. Its paired
  DOF-disabled control is RGB-exact over 307,200 pixels; the active
  inventory-layout frame is held to MAE `[0.005758, 0.043229, 0.005745]` and
  0.267253% pixels above RGB error one, below its explicit 0.3% bounded
  postprocess gate. It takes the native scaled one-sample companion and records
  `msaa_single_sample_scaled_scene_frames`, never a global `r_multisamples`
  downgrade or OpenGL path.

## Remaining work

The 4x path is functionally native and does not create a new unresolved
renderer-selection gap. `FR-02-T13` remains open for the broader
linear-light/HDR output contract, expanded HDR/scaled/post-effect MSAA scenes,
and product-level performance budgets. The unconditional depth-resolve cost for
non-consuming scenes remains
removed; the depth-consuming DOF input contract is now also exact without
discarding MSAA for ordinary frames. Existing whole-renderer telemetry still
shows a Vulkan/OpenGL baseline gap on this micro-scene; optimizing that broader
gap is separate from, and must not be hidden by, the MSAA implementation.

The scaled 4x compatibility selection is documented in
`docs-dev/renderer/vulkan-scaled-msaa-dof-parity-2026-07-17.md`.
