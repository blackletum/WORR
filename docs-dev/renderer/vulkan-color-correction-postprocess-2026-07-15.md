# Native Vulkan Colour-Correction Post-process

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: partial implementation. The native Vulkan raster renderer now provides
brightness, contrast, saturation, tint, split-toning, and LUT-grading stages.
The fixed combined basic-colour transform has compliant paired visual evidence.
Bloom, HDR/tone mapping, DOF, CRT, resolution scaling, and coverage for the
remaining post-process combinations remain open within this task.

## Outcome

`vk_postprocess.c` now owns Vulkan-exclusive archived controls:

- `vk_color_correction` (default `1`);
- `vk_color_brightness` (clamped to `[-1, 1]`);
- `vk_color_contrast` (clamped to `[0, 4]`);
- `vk_color_saturation` (clamped to `[0, 4]`); and
- `vk_color_tint` (default `white`).

The same final pass now also owns OpenGL-equivalent split-toning controls:

- `vk_color_split_shadows` (default `white`);
- `vk_color_split_highlights` (default `white`);
- `vk_color_split_strength` (clamped to `[0, 1]`); and
- `vk_color_split_balance` (clamped to `[-1, 1]`).

LUT grading uses `vk_color_lut` and `vk_color_lut_intensity` (clamped to
`[0, 1]`). The image uses the same `NxN` 2D strip contract as OpenGL: either
width equals height squared or height equals width squared. Invalid, missing,
or zero-intensity LUTs stay inactive.

Tint values use the engine's existing colour parser and generator. Named and
hexadecimal values therefore have the same input semantics as OpenGL colour
correction, rather than introducing a Vulkan-only numeric-string syntax.

The existing native full-screen waterwarp pass is generalized to one final
post-process pipeline. It samples the Vulkan scene-copy image and can apply
underwater warp, colour correction, or both in a single three-vertex draw.
It keeps the UI overlay after the final pass, so HUD and menu elements remain
unwarped and uncorrected, matching the renderer's existing presentation
ordering.

## Colour contract

When enabled, `vk_postprocess.frag` performs the same colour-transform order
as the OpenGL `GLS_POSTFX` path:

1. contrast about middle grey;
2. additive brightness;
3. Rec. 709 luminance saturation using `(0.2126, 0.7152, 0.0722)`; and
4. RGB tint multiplication.

When split-toning is active, it then applies OpenGL's shadow/highlight
interpolation: Rec. 709 luminance selects a balance-shifted smoothstep pivot,
the two colour multipliers are interpolated, and the original/toned result is
mixed by the authored strength. Shadow/highlight tints use the same named/hex
colour parser as the OpenGL controls.

Lastly, an active LUT clamps the prior colour, selects the adjacent blue slices
from the horizontal or vertical 2D strip, linearly interpolates them, and mixes
the graded result by `vk_color_lut_intensity`. The shader equations and order
match the OpenGL `GLS_POSTFX` LUT path.

The stage leaves sampled scene alpha unchanged. Waterwarp remains its existing
time-based coordinate perturbation and is conditional inside the same shader,
so a simultaneous underwater colour correction does not require an additional
copy, render pass, descriptor set, or draw.

## Performance behaviour

The renderer computes whether the configured correction differs from identity
once per frame. With correction disabled or at identity (`0`, `1`, `1`,
white), and with identity split tones, it does not copy the scene or run the
final full-screen pass. A missing/invalid/zero-intensity LUT also remains free.
Existing underwater waterwarp still activates that pass as required. This avoids
both the transfer and the fullscreen fragment workload for the common default
scene, while retaining one pass rather than two when any compatible effect is
active.

The pipeline reuses the existing liquid scene-copy image, native descriptor
layout, load render pass, and swapchain lifecycle. An active LUT allocates a
native external descriptor whose first binding is the scene copy and whose
second binding is the clamp-sampled LUT; inactive frames use the ordinary scene
descriptor unchanged. A LUT cvar change defers descriptor replacement to the
normal frame update after the submitted frame fence, preserving descriptor
lifetime safety. It neither includes nor calls an OpenGL renderer path.

Both normal Vulkan swapchain recreation and `R_ModeChanged` now destroy the
previous post-process resources before their swapchain and recreate them after
the UI resources. This is required for the otherwise-valid final compositor
pipeline, scene-copy images, and descriptors to exist after a normal video
mode setup.

## Headless validation

`tools/renderer_parity/test_vulkan_color_correction_source.py` verifies the
Vulkan cvar contract and clamp ranges, identity fast path, native scene-copy
ordering, UI overlay ordering, exact colour, split-tone, and LUT shader
operation order, combined waterwarp control, both post-process swapchain
lifecycle paths, and absence of an OpenGL route. The repository-owned
`fr01_color_correction_manifest.json` exact-compares the combined non-identity
brightness/contrast/saturation/cyan-tint case over 50,000 pixels. Validation
remains non-interactive:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_color_correction_source.py tools/renderer_parity/test_color_correction_fixture.py
```

Further compliant scenes must cover individual controls, split toning, LUT
grading, and the combined underwater case. HDR, bloom, DOF, CRT, and
resolution scaling need their own native evidence or designs before `FR-01-T12`
can be closed. The fixed combined gate and lifecycle correction are documented
in `vulkan-color-correction-visual-parity-2026-07-15.md`.
