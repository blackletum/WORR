# Shadowmapping Correctness And Tuning Pass

Date: 2026-06-10

## Summary

This pass fixes the correctness and quality issues found by the full GL/VK
shadowmapping review and lands the Vulkan shader sources in-tree.

## Native Vulkan Backend

- **Fixed vertically mirrored shadow sampling.** `VK_Shadow_Record` rendered
  shadow pages with the swapchain-style negative-height viewport, while the
  world receiver reconstructs page UVs as `ndc * 0.5 + 0.5` (GL convention,
  `ndc.y = -1` -> texel row 0). Every local page and sun cascade was sampled
  at the vertical mirror of the correct position. The shadow pass now uses a
  positive-height viewport; the shadow pipeline has `cullMode = NONE`, so
  winding is unaffected.
- **Vulkan shader sources are now in-tree.** The GLSL lives in
  `src/rend_vk/shaders/` and `tools/gen_vk_world_spv.py` regenerates
  `src/rend_vk/vk_world_spv.h` (with optional `--validate` via spirv-val).
  Previously the only sources lived in the git-ignored `.tmp/` directory and
  had drifted ahead of the committed SPIR-V; regenerating from them without
  the matching C-side changes would have skewed the ShadowPages UBO by 16
  bytes.
- **Completed the WIP receiver interface.** The ShadowPages block gains a
  `shadow_moment_tuning` vec4 (x = VSM/EVSM minimum variance, y = EVSM warp
  exponent), mirrored in `vk_shadow_uniform_t`. Hard/PCF receiver taps now use
  a `sampler2DArrayShadow` at set 2 binding 3 (compare op LESS_OR_EQUAL),
  which provides hardware bilinear PCF per tap; the raw-depth sampler remains
  for PCSS blocker search and moment paths. The descriptor-set layout, pool,
  and writes were extended to match.
- The moment pass receives the EVSM exponent through the existing push
  constant pad, so the writer and receiver can never disagree.

## Both Backends

- **EVSM no longer overflows fp16 moments.** The warp exponent dropped from
  10 to 5.4. Moments are stored as `(w, w*w)` in 16-bit float pages, so
  `exp(2*e)` must stay below the fp16 maximum of 65504 (e <= ~5.54); at
  exponent 10 the second moment became Inf for receiver depths beyond ~0.55
  of the page range, producing NaN/undefined Chebyshev results. The constant
  is shared between writer and receiver in each backend
  (`GL_SHADOW_EVSM_EXPONENT_GLSL`, `VK_SHADOW_EVSM_EXPONENT`).
- **Mid-frame page reallocation no longer leaves stale pages.** Growing the
  shared page array (for example a higher per-light `shadow_resolution`
  selected after smaller views already rendered) destroys all page contents,
  but views processed earlier in that frame kept clean resident entries and
  were never re-rendered. Both backends now report "allocated" from
  `ensure_page` for one extra frame after a reallocation, forcing every view
  to re-render once.
- **Receiver dynamic-light loop early-outs.** `calc_dynamic_lights` previously
  ran the full 25-tap PCF (50 for PCSS) for every shadowed light on every
  per-pixel-lit fragment even when the radius attenuation, lambert term, or
  spot cone factor was already zero. Out-of-range, backfacing, and
  outside-cone fragments now skip shadow sampling entirely. Any
  implicit-LOD divergence this introduces for the mipmapped moment array only
  affects quad edges where the light contribution is approaching zero.

## Policy

- `r_shadow_sun` now defaults to `0`. The receivers modulate the entire baked
  lightmap by the sun visibility factor, so any surface occluded from the sun
  direction - every interior under a roof - loses all baked light, not just a
  sun term. Until sun lighting is separated from the lightmap (or gated to
  sky-visible surfaces), sun shadows are opt-in.

## Known Follow-Ups (not in this pass)

- Separate the sun contribution from the lightmap so `r_shadow_sun 1` can
  ship as a default again.
- VK entities do not yet sample shadow pages (GL meshes do).
- Shadow world pass still walks every BSP face per dirty view with per-face
  bounds recomputed from surfedges; casters are not culled per cube face.
- Alpha-tested surfaces cast fully solid shadows; `vk_shadow_alpha_*.frag`
  stubs exist but are unused.
- GL moment mip regeneration covers the whole array each frame; VK blits all
  64 layers per level whenever any job ran.

## 2026-06-11 Follow-Up: Authored Shadowlight Pipeline Regression

A visual deep-dive on `mm-rage` (the `r_shadow_dump` repro from
`mm-rage-dynamic-shadowlights-2026-05-03`) found `candidates=0` in BOTH
renderers: no authored shadowlight reached the shared frontend, so neither
backend rendered or applied any local shadow. The cause was uncommitted
working-tree edits that had reverted the 2026-05-03 fixes (apparently an
older copy of these files overwriting newer ones):

- `src/client/view.cpp`: `V_AddLightExVis` gated shadowlights on
  `light->resolution > 0` again instead of `light->radius > 0.0f`. Authored
  lights without an explicit `shadowlightresolution` key (all 46 on mm-rage)
  were submitted as plain `DL_SHADOW_NONE` dlights.
- `src/client/precache.cpp`: the shadowlight configstring loop lost the
  `csr.shadowlights != (uint16_t)-1` protocol guard and the
  `max_shadowlights`/`csr.end` bounds (out-of-bounds configstring reads on
  non-rerelease protocols), and `update_configstring` regained an
  off-by-one upper bound.
- `src/game/sgame/gameplay/g_misc.cpp` + `g_spawn.cpp` + `g_local.hpp`: lost
  the `shadowlightradius` -> `radius` -> `light` fallback chain, the
  `shadowconeangle` alias, the `light` temp-field parsing, and the
  value-based `was_key_specified` fallback (the pointer-hashed
  `unordered_set<const char *>` is not reliable across translation units).

All hunks were restored to the committed behavior; `was_key_specified` now
carries a comment explaining why the strcmp fallback must not be removed.

Visual verification (deterministic repro, deathmatch, `minplayers 0`,
`teleport 768 130 256 25 90 0` next to the authored cone light at
`768 176 256`, third person):

- OpenGL: `candidates=10 selected=4 views=4 rendered=4`,
  `materialization=depth-compare 2d-array D24`; screenshot shows the
  caster's soft PCF shadow on the floor grate, cast away from the light.
- Vulkan: same frontend stats; live-window capture shows the player model
  with a correctly attached shadow pool extending away from the cone light —
  also visually confirming the shadow-page viewport (Y-flip) fix from the
  2026-06-10 pass.

Remaining cross-renderer differences observed during capture (pre-existing,
not shadow related): the native Vulkan world shader applies no
modulate/intensity gain, so scenes render flatter/brighter than OpenGL and
dynamic-light pools read dimmer in GL; and Vulkan does not render shell
effects (spawn-protection shell shows as a flat green model in GL only).

## 2026-06-11 Stability / Quality / Performance Pass

Implemented on top of the correctness fixes above, in both backends unless
noted:

- **Selection hysteresis (shadow popping fix).** Light selection was a pure
  per-frame top-N by score, so walking around swapped which lights owned
  shadow pages and their shadows popped in and out. Lights selected last
  frame now get a `SHADOW_SELECT_HYSTERESIS` (1.35x) score boost keyed by a
  *stable* identity (configstring index for authored lights, owner entity
  for tracked lights, position hash otherwise, so it survives movement). A
  challenger must beat an incumbent by 35% before the set changes.
  `r_shadowmap_lights` default also raised 4 -> 6 (archived configs keep
  their saved value).
- **View weapons no longer receive shadow maps (OpenGL).** The first-person
  body occludes the gun from most light directions, so sampling pages on the
  view weapon only produced unstable self-shadowing. The dlight UBO's first
  pad int is now `receiver_flags`; the weapon receiver pass sets
  `RECEIVER_NO_SHADOWS`, which makes `shadow_factor_for_light` and
  `shadow_sun_factor` return 1.0. Vulkan entities do not sample shadow pages
  at all yet, so it is unaffected.
- **`r_shadow_softness` (0.25-4, default 1).** New quality knob plumbed
  through the policy into `shadow_global.z` (previously a dead moment flag in
  both backends): scales the PCF kernel radius and the PCSS penumbra.
- **Cube-face guard band.** Point-light faces now use a slightly wider than
  90-degree FOV (resolution- and softness-aware, ~1-3 degrees) so PCF taps
  near a face boundary stay inside the page instead of clamping along the
  45-degree seams.
- **World face bounds cache.** Both backends cached per-face world bounds
  per map (keyed by bsp pointer + checksum + face count); the shadow pass
  no longer walks every face's surfedges for every view every frame, only
  for faces that pass the cached-bounds frustum test.
- **Per-view caster culling.** Casters are now tested against the view
  frustum before emission; previously all six cube faces of a point light
  re-skinned and re-submitted every caster the light touched.
- **Bounded cluster-mask work.** `visrow_t` is 8K (65536 clusters);
  per-caster/per-light mask clears and struct memsets now cover only the
  active map's vis row size / the non-mask prefix of the structs.
- **GL moment mip regeneration gated** on a moment view actually having been
  rendered this frame (previously regenerated all 64 layers every frame).
- **Page sentinel hardening.** `int(-1.0 + 0.5)` truncates to 0 in GLSL, so
  the -1 "no page" sentinel could alias to page 0; both receivers now reject
  negative page values before the int conversion.

Verified: full rebuild, guardrails pass, spirv-val on regenerated modules,
GL + VK mm-rage runs healthy (`candidates=10`, views rendered, no shader
errors, weapon shader variant compiled), VK live capture with
`r_shadow_softness 2.5` shows the shadow still correctly attached.

## Verification

Commands run from `E:\Repositories\WORR`:

- `python tools/gen_vk_world_spv.py --validate` (glslangValidator + spirv-val
  for all three embedded modules)
- `ninja -C builddir-win worr_opengl_x86_64.pdb worr_vulkan_x86_64.pdb`
- `python tools/check_shadowmapping_guardrails.py`
- Regenerated `vk_world_frag_spv` disassembly confirms ShadowPages member
  offsets 0/16/32/48/64/5184, matching `vk_shadow_uniform_t`, and
  `shadow_sampler_cmp` at set 2 binding 3, matching the descriptor layout.
