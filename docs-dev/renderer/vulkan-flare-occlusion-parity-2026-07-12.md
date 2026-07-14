# Native Vulkan Flare and Occlusion Parity

Date: 2026-07-12

Task ID: `FR-01-T03`

Status: Complete

## Outcome

The native Vulkan raster renderer now implements the OpenGL `RF_FLARE` path,
including visibility queries, temporal fading, flare-specific geometry, and
the default flare texture treatment. Flare entities no longer pass through the
ordinary Vulkan entity renderer.

This is a native Vulkan implementation. It does not redirect Vulkan work to
OpenGL and does not modify `q2proto/`.

## OpenGL Contract Reproduced

`src/rend_vk/vk_entity.c` now classifies every valid `RF_FLARE` entity by its
server entity number in `skinnum`. The corresponding Vulkan flare state
reproduces the OpenGL behavior:

- a 2.5-unit flare bound is tested against the same four view-frustum planes;
- visibility is sampled with an occlusion query no more often than every 33 ms;
- query results are polled without `VK_QUERY_RESULT_WAIT_BIT`, so rendering does
  not stall for flare visibility;
- stale state is reset after 2500 ms without a scheduled visible query;
- query geometry is a camera-facing quad at the flare's computed scale;
- when the BSP point is solid, the query point moves five units toward the
  viewer, matching the OpenGL workaround;
- visibility fraction advances using frame time and the same fade-speed
  semantics, exposed as `vk_flare_fade_speed` with default `8` and instant
  transitions at `0`.

Visible flares use the same five-vertex, four-triangle fan, texture coordinates,
and `(25 << default_flare) * scale * visibility_fraction` size rule as OpenGL.
`RF_FLARE_LOCK_ANGLE` uses the entity angles; other flares face the viewer.
Inner and outer alpha, shell-color tinting, additive blending, and disabled
depth testing also match the reference renderer.

The entity fragment shader has a dedicated default-flare flag. For textures
registered with `IF_DEFAULT_FLARE`, it applies the reference luminance and
vertex-alpha modulation without changing other entity materials. The UI image
registry exposes its stored image flags to make this selection from the native
Vulkan entity path.

## Query and Pipeline Design

The entity renderer owns one `VK_QUERY_TYPE_OCCLUSION` pool with one slot per
edict. Result retrieval requests and checks availability without waiting, and a
slot is not rescheduled while its result remains pending. Resets and replacement
queries are submitted later on the same graphics queue, preserving command
ordering even when an off-screen or stale result is deliberately discarded.

Only query slots scheduled for the current frame are reset. Consecutive slots
are coalesced into reset ranges, and `VK_Entity_ResetFlareQueries` records those
resets before the main render pass because Vulkan query-pool resets cannot be
issued inside that render pass.

Two swapchain-lifetime pipelines are prebuilt:

- the occlusion pipeline keeps depth testing, disables depth writes, and has a
  zero color-write mask;
- the visual flare pipeline uses additive blending with depth testing and depth
  writes disabled.

Query and visual triangles reuse the transient entity vertex stream. Visual
flare batches with a common descriptor set remain coalesced. Frame flags avoid
binding either special pipeline when the frame contains no flare work, so the
ordinary entity path incurs no extra draw or bind submissions.

## Validation

- `python tools/gen_vk_world_spv.py --validate`
  - regenerated and validated the embedded entity shader SPIR-V.
- `ninja -C builddir-win worr_vulkan_x86_64.dll`
  - passed warning-free; the native Vulkan DLL compiled and linked.
- `python tools/check_shadowmapping_guardrails.py`
  - passed.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64`
  - refreshed `.install`, repacked 280 assets, and passed Windows x86-64 staged
    payload validation.
- Build-tree and staged Vulkan DLL SHA-256 values matched:
  `BA9E0583DA26231951EDAEEFF28C1C8DCD3A70C42188BDB0CD247E48DD471601`.
- Canonical staged smoke on `fact2`:
  - loaded renderer `vulkan`;
  - loaded a local two-flare entity override;
  - reported `"vk_flare_fade_speed" is "8"`;
  - exited with code zero and no Vulkan entity failure.
- Deterministic direct-view comparison:
  - used the same `fact2` map override, camera, and two `misc_flare` entities in
    OpenGL and Vulkan;
  - exercised a locked default white flare and an oriented red-shell flare;
  - confirmed matching shape, size, placement, orientation, tint, and alpha.
- Deterministic occlusion comparison:
  - aimed the same camera toward a flare through a nearby wall;
  - used instant fade to make a hidden query result unambiguous;
  - both OpenGL and Vulkan fully suppressed the occluded flare.

The canonical smoke log is
`.install/basew/logs/vulkan_flare_final_smoke.log`. Capture conversions, entity
overrides, and comparison harnesses under `.tmp/vulkan-flare-captures` and
`.tmp/vulkan-flare-install` are local validation evidence, not release
artifacts.

## Remaining Work

This closes `RF_FLARE` rendering and occlusion behavior. The map-driven MD2/MD5
parity pass (`FR-01-T04`), remaining sky seam work (`FR-01-T05`), and the final
renderer parity checklist and automated comparison coverage remain separate
roadmap tasks.
