# Native Vulkan Beam Style Parity

Date: 2026-07-12

Task ID: `FR-01-T02`

Status: Complete

## Outcome

The native Vulkan raster renderer now implements the two beam styles exposed by
OpenGL. `vk_beam_style 0` selects the fast textured billboard style, while any
nonzero value selects the original Quake II-style 12-sided polygonal tube.

`RF_GLOW` beam entities also use the OpenGL lightning policy: between three and
seven length-dependent segments are displaced along stable perpendicular axes,
then each segment is emitted using the selected billboard or polygonal style.

This remains native Vulkan work. No Vulkan path redirects through OpenGL, and
`q2proto/` is unchanged.

## OpenGL Contract Reproduced

The implementation in `src/rend_vk/vk_entity.c` mirrors these OpenGL details:

- style `0` uses a generated 16 by 16 white beam texture with the same
  cross-beam alpha profile as `GL_InitBeamTexture`;
- style `0` uses the `1.2` beam-width scale and camera-facing quads;
- style `1` uses the `0.5` beam-width scale and 12 points around the beam axis;
- polygonal segment rings share one orientation across a lightning chain;
- `RF_GLOW` uses the same 16-unit minimum segment length, three-to-seven segment
  range, and 35 percent random perpendicular displacement;
- palette-index and direct-RGBA beam colors retain the existing client
  selection behavior.

The visual comparison exposed an older Vulkan mismatch outside the style
selector itself. Beam entities receive `ent.alpha = 0.30` from the client but
do not require `RF_TRANSLUCENT`. Vulkan previously passed that value through the
generic entity-alpha helper, which returned full opacity without the flag. The
beam path now consumes and clamps `ent.alpha` unconditionally, matching
OpenGL's `color.a *= ent->alpha` contract.

## Performance Characteristics

The default textured style remains the low-geometry path at two triangles per
straight segment. The polygonal compatibility style emits 24 triangles per
segment, but all triangles sharing texture and depth state are appended to the
existing transient entity vertex stream and coalesced into one compatible
batch. There is no draw call per tube side or lightning segment.

The beam texture is created once with the entity renderer, uses the existing
descriptor/sampler infrastructure, and is released through the normal entity
shutdown lifecycle. `vk_beam_style` can change at runtime without pipeline or
descriptor recreation.

## Validation

- `ninja -C builddir-win worr_vulkan_x86_64.dll`
  - passed; the native Vulkan DLL compiled and linked.
- `python tools/check_shadowmapping_guardrails.py`
  - passed.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64`
  - refreshed `.install`, repacked 280 assets, and passed Windows x86-64 staged
    payload validation.
- The staged and build-tree Vulkan DLL SHA-256 values matched:
  `F0711ED196E83978A0C41F8692782A66A661AEDDE15FCEA6D33C04C2698712C5`.
- Canonical staged smoke on `fact2`:
  - loaded renderer `vulkan`;
  - switched from `vk_beam_style 0` to `vk_beam_style 1` in one session;
  - reported `"vk_beam_style" is "1" default: "0"`;
  - exited with code zero and no Vulkan entity failure.
- Deterministic visual comparison:
  - used the same `fact2` camera at `128 -256 -168`, angles `0 0 0`;
  - captured the four always-on `target_laser` entities after their delayed
    startup for OpenGL styles `0` and `1` and Vulkan styles `0` and `1`;
  - confirmed the textured soft-edge/wider style and solid polygonal/narrower
    style distinction in both renderers;
  - the comparison identified and verified the beam-alpha correction above.

The canonical smoke log is
`.install/basew/logs/vulkan_beam_style_final_smoke.log`. Capture conversions and
harness files live under `.tmp/vulkan-beam-captures` and
`.tmp/vulkan-beam-install`; user-profile screenshots and `.tmp` files are local
validation evidence, not release artifacts.

## Remaining Work

This closes beam style selection and lightning geometry parity. Flare behavior
(`FR-01-T03`), broader particle-shape options, post-processing parity, and
automated pixel-tolerance gates remain separate roadmap work.
