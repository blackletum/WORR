# Native Vulkan Bloom Baseline

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: partial implementation. Native Vulkan now has the OpenGL renderer's
scene-only bloom fallback: threshold/knee prefiltering, firefly clamping,
downsampled separable Gaussian blur, and final saturation/intensity blending.
OpenGL's optional emissive MRT input and multi-level mip pyramid remain open,
as do HDR, DOF, CRT, resolution scaling, and no-window visual comparison.

## Outcome

`vk_postprocess.c` owns two device-local, downsampled colour attachments. Once
the current scene copy is available, Vulkan renders a four-tap prefilter into
the first target, then alternates horizontal and vertical Gaussian blur passes
between the targets. The native final post-process blends the completed bloom
image before colour correction, split toning, and LUT grading, matching the
ordering of the OpenGL postfx path.

The implementation is deliberately the reliable OpenGL scene-only fallback,
not an OpenGL redirect. It does not depend on GL framebuffer objects, GLSL
program state, or renderer calls. The final descriptor set has three native
sampled-image bindings: scene, LUT/fallback, and bloom/fallback. This allows
bloom and LUT grading to run together without a second scene copy or an
additional full-resolution composition pass.

## Controls and identity behavior

The Vulkan-exclusive controls mirror the active OpenGL baseline:

- `vk_bloom`
- `vk_bloom_iterations` (`1..8`, each setting produces a horizontal and
  vertical blur pass)
- `vk_bloom_downscale` (`1..8`)
- `vk_bloom_firefly` (`0..1000`)
- `vk_bloom_sigma` (`1..25`)
- `vk_bloom_threshold` (`0..10`)
- `vk_bloom_knee` (`0..1`)
- `vk_bloom_intensity` (`0..10`)
- `vk_bloom_saturation` and `vk_bloom_scene_saturation` (`0..4`)

When `vk_bloom` is disabled, the bloom targets are not recorded and the final
descriptor falls back to the scene image. Existing identity fast paths for
waterwarp, colour correction, split toning, and LUTs stay intact. Resource and
descriptor changes occur only after the submitted-frame fence has signalled,
so an in-flight command buffer never references a destroyed descriptor or
intermediate image.

## Performance characteristics

Bloom runs at the configured downscaled resolution and uses a separable blur,
avoiding full-resolution multi-pass work. Its persistent ping-pong targets are
retained across ordinary frames and rebuilt only when a change in downscale or
swapchain extent requires different dimensions. The renderer's native phase
telemetry attributes this work to composition rather than hiding it in a
single aggregate frame time.

## Headless validation

`tools/renderer_parity/test_vulkan_bloom_source.py` verifies the native cvar
surface, ping-pong colour targets, prefilter/blur/composite ordering, combined
scene/LUT/bloom descriptors, and absence of an OpenGL renderer route. The
validation is non-interactive:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_bloom_source.py
```
