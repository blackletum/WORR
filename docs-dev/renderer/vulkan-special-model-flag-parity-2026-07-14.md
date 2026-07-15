# Vulkan special model flag parity

Date: 2026-07-14  
Project task: `FR-01-T04`  
Status: implemented and validated; outline rendering and durable automation completed by the follow-up below

## Objective

Close the native Vulkan alias-model gaps for the special presentation flags that OpenGL applies after ordinary MD2/MD5 lighting:

- `RF_RIMLIGHT`
- `RF_BRIGHTSKIN` translucent/additive routing
- `RF_ITEM_COLORIZE`
- `RF_IR_VISIBLE` while `RDF_IRGOGGLES` is active
- `RF_TRACKER`

The implementation remains native Vulkan. No Vulkan draw path is redirected through OpenGL.

## OpenGL behavior used as the parity contract

The reference behavior was audited in `src/rend_gl/mesh.c` and `src/rend_gl/shader.c`.

- Rim light selects `entity_t::rgba` RGB, or red when the packed color is zero. The fragment result is `1 - max(dot(normal, view_direction), 0)`, squared, and multiplied by the selected color and entity alpha.
- Translucent rim and brightskin copies use additive blending and do not receive ordinary dynamic-light/shadow modulation.
- Item colorization first renders an untinted texture base. A second standard-alpha pass computes Rec. 601 luminance (`0.299`, `0.587`, `0.114`), multiplies it by the item tint, and uses `rgba.a * entity_alpha` as overlay strength.
- IR-visible models select red before ordinary receiver modulation when IR goggles set `RDF_IRGOGGLES`.
- Tracker models select black before ordinary receiver modulation.
- `RF_TRANSLUCENT` selects a blend path even when `entity_t::alpha` is exactly `1.0`.

## Implementation

### Special color precedence

`VK_Entity_LitColor` now follows the relevant OpenGL `setup_color` precedence:

1. rim
2. brightskin
3. fullbright
4. IR-visible under IR goggles
5. tracker
6. ordinary world lighting, `RF_MINLIGHT`, and `RF_GLOW`

Rim and brightskin vertices are marked fullbright/no-shadow/no-dlight so the ordinary receiver work is skipped. IR and tracker retain the same downstream dynamic-light/shadow behavior as OpenGL.

### Native rim shader

The entity fragment shader has a dedicated `VK_ENTITY_VERTEX_RIMLIGHT` mode implementing OpenGL's squared rim term. The shared shadow/receiver uniform block gained one aligned `vec4 view_origin`, populated once per frame by `VK_Shadow_UpdateDlights`.

This avoids expanding the already full 128-byte view push-constant range and avoids adding a camera vector to every entity vertex. `vk_world_shadow.frag` declares the same uniform member so the page and dlight arrays retain identical `std140` offsets in both receiver shaders.

### Item base and overlay

The Vulkan entity shader now has distinct item-base and item-overlay modes:

- the base mode preserves raw texture/intensity output and intentionally ignores entity tint and alpha, matching `GLS_ITEM_COLORIZE_BASE`;
- the overlay mode computes texture luminance, applies the item tint, and uses the authored overlay alpha.

MD2 and MD5 paths record their base triangle range, then construct the overlay from those already transformed vertices. MD5 animation and weighted normal skinning are not repeated. Only item-colorized models pay the extra vertex-copy and draw cost.

Deferred entity recording now uses this order:

1. opaque batches;
2. translucent item bases;
3. item colorize overlays;
4. general translucent/additive batches, including separate rim entities.

This preserves the effective OpenGL base/overlay/rim order even though Vulkan records entity work after frontend submission. The same ordering is applied to depth-hack batches.

### Blend and pipeline parity

MD2, MD5, sprite, and inline-BSP classification now honors `RF_TRANSLUCENT` even at alpha `1.0`. This is required for full-strength rim and brightskin copies.

Rim and brightskin MD2/MD5 batches select the prebuilt additive pipeline. A depth-hack additive pipeline was added so the blend contract does not silently change for depth-hack entities. Pipelines are created with swapchain resources rather than built during gameplay.

### Generated shaders

`src/rend_vk/vk_entity_spv.h` and `src/rend_vk/vk_world_spv.h` were regenerated from their GLSL sources with validation enabled.

## Validation

### Build and shader validation

The following completed successfully:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
```

`git diff --check -- src/rend_vk` reported no whitespace errors (only the repository's expected Windows line-ending notices).

### Runtime comparison scenes

An isolated `.tmp/vulkan-md5-normal-install` runtime used a deterministic `fact2` entity override with red, green, and blue item classes plus an infantry target. Captures were 960x720 and used fixed camera coordinates. OpenGL and Vulkan were run with the same staged engine/cgame/sgame modules.

| Scene | Mean absolute RGB error (0-255) | Pixels with max-channel error > 8 |
|---|---:|---:|
| MD2 item colorize + rim | `1.60939, 1.21251, 0.85542` | `4.365162%` |
| MD5 item colorize + rim | `0.91354, 0.66248, 0.48220` | `2.770255%` |
| IR goggles + IR-visible infantry | `0.34183, 0.15000, 0.10376` | `0.713252%` |

The full-frame metrics include normal backend lighting variation, item bob/rotation, monster animation, HUD state, and view-weapon bob. Visual inspection confirmed the same tint, squared rim distribution, and red IR target treatment. A live disruptor shot also exercised the tracker frontend path and produced the expected black/dynamically lit target in both renderers; particle and projectile timing makes that scene unsuitable for a deterministic pixel threshold.

The first rim comparison exposed two Vulkan ordering/classification defects during this work:

- the item overlay initially ran after the separate rim and covered it;
- `RF_TRANSLUCENT` with alpha exactly `1.0` initially entered the opaque pass.

Both were corrected before the final captures above.

### Staged distributable

The canonical staging workflow completed successfully:

```text
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64
```

The build and staged Vulkan DLLs are byte-identical:

```text
SHA-256 A9A7A139A5269954E155EEE9C828C01D67B8A41DC4EE74267ABAD0263735BED5
```

## `FR-01-T04` follow-up

Native `RF_OUTLINE` / `RF_OUTLINE_NODEPTH` stencil behavior and the owned
GL/Vulkan comparison workflow are documented in
`docs-dev/renderer/vulkan-alias-outline-parity-automation-2026-07-14.md`.
That follow-up completes the scoped `FR-01-T04` MD2/MD5 presentation pass.

No user-facing cvar or workflow changed in this slice, so no `docs-user/` update is required.
