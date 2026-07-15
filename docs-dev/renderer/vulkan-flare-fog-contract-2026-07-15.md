# Native Vulkan Flare Fog Contract

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: native Vulkan matches OpenGL's deliberate no-fog flare contract at
source level and in the retained direct-flare headless visual gate.

## OpenGL contract

`GL_DrawFlares` batches additive flare fans through `GL_FlushFlares`.
`GL_FlushFlares` selects `GLS_DEPTHTEST_DISABLE`, `GLS_DEPTHMASK_FALSE`,
`GLS_BLEND_ADD`, and any default-flare flag, but it does not add
`glr.fog_bits`. Consequently, a visible `RF_FLARE` is intentionally not
fogged by the OpenGL renderer; treating it as another ordinary entity fog
receiver is not visual parity.

## Native Vulkan correction

`VK_ENTITY_VERTEX_NO_FOG` is now bit 11 in both the C vertex contract and
`vk_entity.frag`. Both the occlusion-query quad and the additive flare fan set
this bit. The native entity fragment helper accepts the per-vertex no-fog bit
and returns before global or height fog composition only for those vertices.

The flag is deliberately local to flares. Ordinary model, beam, particle, and
sprite vertices do not carry it and retain their existing native fog behavior.
No OpenGL renderer path is called or redirected from Vulkan.

## Verification

The Windows Meson build completes with the native entity source, and
`test_vulkan_fog_source.py` locks both the OpenGL state contract and the
native flag/shader route. The generated `misc_flare` scene now supplies a
real, query-visible flare under authored dense global fog and exact-compares
the Vulkan result. The focused visual fixture, control result, and commands
are recorded in `vulkan-flare-fog-visual-parity-2026-07-15.md`.
