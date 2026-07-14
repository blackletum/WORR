# Vulkan MD5 Smooth-Normal Parity and Skinning Reuse

Date: 2026-07-12

Task ID: `FR-01-T04`

Status: In Progress

## Outcome

The native Vulkan renderer now reconstructs and animates MD5 replacement-model
normals using the same algorithm as OpenGL. MD5 lighting is smooth across
connected surfaces instead of assigning one flat face normal to every triangle,
and color-shell expansion follows the animated per-vertex surface direction.

The visible and shadow paths also reuse one skinned result per unique mesh
vertex. Weighted joint evaluation is therefore proportional to vertex count,
not the larger expanded triangle-index count.

This is a native Vulkan implementation. It does not redirect through OpenGL and
does not modify `q2proto/`.

## Root Cause

OpenGL computes MD5 normals while loading a replacement mesh:

1. reconstruct every bind-pose vertex from its weighted joints;
2. compute angle-weighted triangle normals;
3. weld vertices with identical bind-pose positions so UV seams share the same
   accumulated surface normal;
4. normalize each accumulated result;
5. transform that bind-pose normal into weighted joint-local space.

At render time OpenGL rotates and blends those joint-local normals through the
animated skeleton. Vulkan previously reconstructed only positions, calculated a
single world-space face normal after each triangle was emitted, and copied that
normal to all three vertices. This produced faceted lighting and made MD5 shell
expansion follow triangle planes instead of the smooth model surface.

## Implementation

`src/rend_vk/vk_entity.c` now stores a joint-local normal in every parsed MD5
vertex. `VK_MD5_ComputeNormals` mirrors the OpenGL loader algorithm, including
angle weighting and exact-position welding through the renderer hash-map API.
The result is generated once when the replacement mesh is loaded.

`VK_Entity_MD5Vertex` now optionally skins both position and normal in one
weight loop. The normal uses each animated joint axis and weight bias, while the
existing entity normal transform continues to handle non-uniform entity scale.
Shell displacement is applied in model space before the entity transform,
matching the OpenGL skeletal shell path.

A reusable scratch vertex array is grown to the largest MD5 mesh encountered.
For each visible mesh, Vulkan now:

1. skins every unique vertex once;
2. transforms its position and normal once;
3. applies texture coordinates, color, and flags once;
4. expands indexed triangles into the existing transient entity stream by
   copying cached vertices.

The MD5 shadow-caster path uses the same scratch storage and caches transformed
positions before emitting indexed triangles. No per-model or per-frame heap
allocation was added; scratch storage grows geometrically through the existing
renderer allocation helpers and is released at entity-renderer shutdown.

## Map-Driven Validation

A local `fact2` entity override placed two instances of
`models/monsters/infantry/tris.md2` in a fixed corridor scene. One used frame 0
and the other frame 150. The runtime confirmed that the replacement loaded as
one mesh with 18 joints and 264 animation frames.

The same camera and scene were captured four ways:

- OpenGL with `gl_md5_use 0`;
- Vulkan with `vk_md5_use 0`;
- OpenGL with `gl_md5_use 1`;
- Vulkan with `vk_md5_use 1`.

Legacy blob shadows and shared shadow maps were disabled so the images isolate
model geometry, skinning, textures, and lighting. At 960 by 720 pixels:

- MD2 fallback comparison:
  - mean absolute RGB error: `0.12190`, `0.06411`, `0.06922` on a `0..255`
    channel scale;
  - `0.17245%` of pixels differed by more than 8 in any channel.
- MD5 replacement comparison:
  - mean absolute RGB error: `0.11885`, `0.06252`, `0.06817`;
  - `0.16450%` of pixels differed by more than 8 in any channel.

Visual inspection confirmed matching poses, silhouettes, smooth lighting, skin
selection, placement, and frame mapping in both backends. The residual image
error is concentrated in small UI/rasterization edge differences rather than
model-shape or lighting mismatches.

An attempted synthetic `EF_COLOR_SHELL` plus `RF_SHELL_RED` `misc_model` scene
stalled before capture in both OpenGL and Vulkan. Because the failure is shared
above the backend boundary, it is not evidence against this Vulkan change, but
the shell-specific runtime harness still needs a valid entity construction path.

## Build and Runtime Evidence

- `ninja -C builddir-win worr_vulkan_x86_64.dll`
  - passed warning-free after the renderer change.
- `python tools/check_shadowmapping_guardrails.py`
  - passed.
- `git diff --check -- src/rend_vk/vk_entity.c`
  - passed.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64`
  - refreshed `.install`, packed 280 assets, and passed staged Windows x86-64
    payload validation.
- A concurrent game-API transition required rebuilding the executable, engine,
  cgame, and sgame together before the canonical smoke. The final staged run:
  - loaded native renderer `vulkan`;
  - loaded game API 2025 consistently;
  - loaded the two-frame entity override;
  - loaded the infantry MD5 replacement as 1 mesh, 18 joints, and 264 frames;
  - switched from `vk_md5_use 0` to `vk_md5_use 1` in one session;
  - exited with code zero and no Vulkan entity failure.
- Build-tree and staged Vulkan DLL SHA-256 values matched:
  `C23C7561F114B71CB815C6B13D2789DA996366BFD0FF6041B764221F46A1EDF5`.

The canonical smoke log is
`.install/basew/logs/vk_md5_normal_final_smoke.log`. Comparison images, converted
OpenGL captures, entity overrides, and launch configs are under
`.tmp/vulkan-md5-normal-install`; they are local validation evidence rather
than release artifacts.

## Remaining `FR-01-T04` Work

This pass closes MD5 smooth-normal construction, animated normal skinning, and
duplicate per-index skinning work. `FR-01-T04` remains open for model-specific
special flags and durable comparison automation, notably `RF_IR_VISIBLE`,
`RF_TRACKER`, `RF_RIMLIGHT`, `RF_ITEM_COLORIZE`, and outline behavior. A valid
shell runtime scene is also still required even though the implemented shell
math now follows the OpenGL path.
