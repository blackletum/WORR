# Native Vulkan GPU MD2 Interpolation and Static Mesh Residency

Date: 2026-07-15

Task ID: `FR-01-T14`

Status: partial implementation. Standard Vulkan MD2 instances now use immutable
device-local source meshes and a native vertex-stage interpolation path. This
removes the ordinary MD2 per-frame transformed vertex and index upload. The
related eligible MD5 GPU skinning path is documented separately in
`vulkan-gpu-md5-skinning-2026-07-15.md`. Neither path claims full static
residency for every effect/model type or a measured renderer-wide performance
win.

## Implemented path

At model registration, each MD2 owns three immutable device-local resources:

1. an interleaved position/normal stream for every animation frame;
2. a static UV stream; and
3. the original 16-bit triangle index stream.

The registration transfer uses a one-time native Vulkan command buffer with
explicit transfer-to-vertex-input/index-read barriers. CPU model data remains
owned for shadow-caster emission, outline generation, and the compatibility
fallback; no Vulkan path calls into the OpenGL renderer.

For normal MD2 presentation, the current frame now uploads only a compact
144-byte instance record containing the interpolated entity origin, scaled and
inverse-scale normal axes, front-lerp factor, shell expansion, packed colour,
and presentation flags. `vk_entity_gpu_md2.vert` binds the new and old frame
ranges from immutable memory, interpolates position and normal, applies shell
expansion and the exact existing world transform, then emits the same fragment
interface used by the normal Vulkan entity shader.

Consecutive compatible entities with the same model, skin, frame pair,
pipeline state, and submit phase coalesce into an instanced indexed draw. Their
packed colour, lighting flags, transform, and shell value remain per instance;
this does not reorder alpha or liquid-phase work.

## Visual and functional boundaries

The GPU path preserves MD2 frame resolve, origin interpolation, non-uniform
scale normal handling, shells, alpha/additive routing, glowmaps, depth-hack,
weapon projection, dynamic shadow receiver inputs, and the existing texture
descriptor contract. `RF_ITEM_COLORIZE` and `RF_OUTLINE` deliberately keep the
expanded CPU representation because they reuse generated triangle ranges for
base/overlay/stencil/shell passes. Eligible MD5 replacements now use a native
GPU vertex-skinning path; the CPU still resolves their small animation joint
palette before each draw. MD5 colorize/outline and any unavailable GPU resource
continue on the established CPU-skinned indexed fallback.

`vk_md2_gpu_lerp` is an archived, latched Vulkan-only control and defaults to
`1`. Pipeline or allocation failure explicitly falls back to CPU interpolation
so a feature-capability issue cannot remove model rendering.

## Expected cost change

For an eligible MD2, current-frame transfer is reduced from one 52-byte
transformed vertex per source vertex plus a dynamic index range to one 144-byte
record per instance. Static frame, UV, and index data are transferred only at
model registration. The vertex shader performs interpolation and object-space
transformation once per submitted vertex; compatible adjacent instances also
avoid repeated indexed draw setup.

GPU timing and paired OpenGL/Vulkan capture evidence remain required before
claiming an end-to-end frame-time improvement.

## Headless validation

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools/renderer_parity/test_vulkan_gpu_md2_submission_source.py
```

The checks validate native static buffer ownership, shader interpolation and
world transform, current-frame instance staging/barriers, instanced indexed
submission, special-pass CPU fallback, and the absence of an OpenGL route. No
interactive client window was launched.
