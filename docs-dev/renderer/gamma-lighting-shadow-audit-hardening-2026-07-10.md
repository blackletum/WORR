# Gamma, Lighting, and Shadow Audit Hardening

Date: 2026-07-10

Task IDs: `FR-02-T12` (completed), `FR-01-T09`, `FR-02-T13`,
`FR-02-T14`, `FR-02-T15`, `FR-03-T11`, `DV-02-T07`, and `DV-03-T08`.

Forward plan:
`docs-dev/plans/renderer-color-lighting-shadow-modernization.md`.

## Summary

This pass scrutinized gamma, static/dynamic lighting, light sampling,
tone-mapping, and shadowmapping across OpenGL, native Vulkan raster, and the
Vulkan RTX path. It fixes bounded correctness and robustness defects now, while
recording the linear-light/output-color and shadow-resource redesign as
separate task-gated work.

The implementation does not disguise a Vulkan gap with an OpenGL fallback.
All Vulkan changes remain native, and `q2proto/` is untouched.

## Audit Result

The existing renderers did not share a complete color contract:

- OpenGL applied compatibility gamma either through an OS ramp or selected
  source-texture mutation.
- Native Vulkan raster did not implement `r_gamma` and performed lighting and
  blending in legacy code-value space.
- Texture sRGB/linear roles and final presentation encoding were not explicit.
- OpenGL HDR exposure/bloom ordering did not operate on a consistently linear
  scene signal.

That cannot be corrected safely by adding a `pow()` to individual Vulkan draw
shaders: doing so before alpha blending changes the composited result and still
misses UI/overlays. `FR-02-T13` therefore owns a linear scene target plus one
final SDR/HDR presentation pass in both raster backends. Phase 0 deliberately
keeps current visual gamma behavior while removing immediate hazards and
closing lighting/shadow parity defects.

## Implemented Corrections

### Shared cvar and shadow frontend safety

- `Cvar_ClampValue` now detects non-finite cached values, restores a finite
  clamped default (or minimum fallback), and updates the cvar. This protects
  renderer code that correctly uses the common clamp API.
- Shared shadow policy values reject non-finite bias, offset, softness, sun
  size/distance, and direction data before they reach matrices or shaders.
- Shadow float quantization now uses finite double precision, saturates to
  `int32_t`, and avoids undefined `lrintf` overflow/NaN behavior.
- `r_shadow_alpha_mode` is clamped to the only implemented modes (`0..1`).
- Canonical `r_shadow*` cvars and `gl_`/`vk_` aliases now mirror via guarded
  change callbacks. Live last-write behavior no longer depends on each cvar's
  unrelated historical edit count. Callback pointers are removed during
  renderer shutdown.
- Shadow dump output now includes slope bias, normal offset, bias scale,
  softness, sun distance, and sun size for live-policy verification.

### Shadow cache correctness and submitted-light identity

- Cached page projection hashes now include the rasterized slope/constant bias
  policy and point-face softness/guard-band input. A live policy change cannot
  sample a clean page rendered with an old projection or depth bias.
- Owner/source-derived tracked dynamic lights export their existing stable ID
  through `dlight_t::shadow_stable_id`. Moving-position fallback hashes remain
  intentionally non-resident identities.
- Server-provided `CS_SHADOWLIGHTS` records are parsed into temporary storage
  with exact field counts, strict integer/float syntax, finite and range checks,
  fade ordering, cone/style/type validation, and no trailing data. Only a fully
  valid record is committed; empty/malformed updates clear the slot so stale
  lights cannot persist.

### OpenGL gamma and lighting robustness

- `r_gamma` table construction clamps to `0.3..3.0` before exponentiation.
- Shader-side modulate, world modulate, brightness, intensity, and glow
  intensity now use the same finite/range-clamped inputs as CPU lighting paths.
- Spot attenuation clamps cone cosine below one and uses an epsilon-protected
  denominator.
- Entity scale treats zero/non-finite components as Quake's implicit `1`,
  retains reflected axes, and computes a positive conservative culling scale.
- Mesh, skeletal, and generic dynamic-light normals use the inverse transpose
  of the renderer's orthogonal scaled entity axes, including non-uniform and
  reflected transforms. A collapsed malformed basis has a defined fallback.
- `r_map_overbright_cap` now applies when lightmap shift is zero as well as when
  it is positive/negative.
- Face lightmap sampling clamps/finitizes coordinates, clamps the second tap at
  the last row/column, and supports `1xN`, `Nx1`, and `1x1` maps without
  out-of-bounds reads.
- Lightgrid sampling validates finite coordinates and grid extents before
  unsigned conversion, clamps neighbor indices at grid boundaries, and falls
  back to face/static lighting when the point is outside the grid.
- Both one-pixel auto-exposure history textures initialize to identity. The
  first valid exposure update snaps to the current target instead of adapting
  from undefined history, then restores the configured temporal alpha.

### OpenGL shadows

- The depth array uses nearest filtering because the receiver reads raw depth
  and performs hard/PCF/PCSS comparisons manually. Interpolating raw depth
  before comparison produced false visibility at discontinuities.
- EVSM empty texels now encode far depth as `(exp(e), exp(2e))`, using the same
  shared exponent as writer/receiver. The previous `(1,1)` clear represented
  near warped depth and falsely shadowed untouched page areas.
- The numeric EVSM exponent and generated GLSL string now originate from one
  definition.

### Native Vulkan raster lighting and presentation safety

- Native Vulkan now registers/synchronizes canonical `r_intensity` and legacy
  `intensity`, clears their unnecessary texture-reload flags, clamps to the GL
  range, and uploads the value through the shared receiver UBO.
- World and inline-BSP intensity eligibility mirrors OpenGL's BSPX-lightmap,
  opaque/non-warp, and lava rules; MD2/MD5 meshes and shells receive mesh
  intensity, while sprites/beams/particles do not.
- Vulkan PCF/VSM/EVSM/PCSS visibility now applies the same exponent curve as
  OpenGL before strength blending.
- Spot attenuation has the same finite denominator protection as OpenGL.
- Swapchain selection now considers R8G8B8A8 UNORM and prefers any compatible
  B8/R8 UNORM format before an SRGB image-format fallback. This avoids a silent
  second encode for current legacy display-referred raster output.
- Lightgrid sampling ignores style-255/occluded samples instead of treating
  them as black contributors, validates grid coordinates before unsigned
  conversion, and clamps edge neighbors.
- EVSM render-pass clears encode warped far moments rather than `(1,1)`.
- The modified native shaders were regenerated into
  `vk_world_spv.h`/`vk_entity_spv.h` and validated.

### Vulkan RTX tone mapping and sun shadow setup

- Tone-curve application clamps interpolation to bins `0..127`; the right bin
  cannot read element 128.
- Partial 16x16 histogram edge workgroups initialize and flush shared bins from
  all relevant lanes rather than gating barrier-adjacent work on image bounds.
- The 128-lane curve pass no longer rejects lanes by render width/height before
  workgroup barriers; it is a bin-space dispatch, not an image-space dispatch.
- Auto-exposure bounds are finite, positive, ordered, and constrained to the
  histogram domain. The low-noise path cannot index `shared[-1]`, and its
  algebra no longer produces `0/0` when target log luminance is zero.
- Tone-map reset uses explicit shader-to-transfer and
  transfer-to-compute buffer dependencies. `vkCmdFillBuffer` writes are now
  visible to subsequent reads and atomic writes.
- The sun shadow basis uses an alternate up vector for both `+Z` and `-Z`
  directions, avoiding a zero cross product for a common downward sun.

### Gameplay lightlevel serialization

- `V_SetLightLevel` now handles non-finite/negative samples and saturates to
  `0..255` before conversion to the protocol byte. Overbright samples no longer
  wrap and NaN cannot trigger undefined float-to-int conversion.
- This is a safety fix, not the completion of `FR-01-T09`: the sample still
  originates in backend-adjusted `R_LightPoint` and can vary with visual
  settings. The forward task separates gameplay static-light semantics.

## Deliberately Deferred Architecture Work

The following are real audit findings and are tracked, not silently accepted:

- `FR-02-T13`: explicit sRGB/linear image roles, FP16 linear scene, final
  exposure/tone/gamma/OETF pass, SDR/HDR negotiation, UI/screenshot/capture
  semantics, and capability-gated hardware gamma.
- `FR-01-T09`: backend/settings-independent gameplay light queries.
- `FR-02-T14`: active/budgeted shadow capacity, resolution buckets/atlas,
  transactional allocation, RG16F moments, dirty-layer mip generation, true
  hard/comparison sampler capability paths, linearized PCSS, alpha-tested
  casters, persistent BSP geometry, and cube-face seam handling.
- `FR-02-T15`: separate direct sun from baked/ambient light before default-on
  cascaded sun visibility.
- `FR-01-T04`: smooth skinned native Vulkan MD5 normals.
- RTX: initialize/gate all polygonal/dynamic light contributions, guard
  coincident shadow rays, fix the UNORM/SRGB mutable-view fallback, centralize
  finite tone-map cvar policy, and widen/bound histogram accumulation for very
  high resolutions.
- `FR-03-T11`: expose only active renderer/capability controls and add practical
  shared shadow controls.
- `DV-02-T07` and `DV-03-T08`: mandatory shader freshness/validation plus
  semantic, fuzz, GPU-validation, and pixel-tolerance coverage.

## Verification

Executed from `E:\Repositories\WORR`:

- `python tools/gen_vk_world_spv.py --validate`
  - rebuilt native world/entity embedded SPIR-V;
  - `spirv-val` passed.
- All 50 VKPT shader modules compiled with Vulkan SDK 1.4.313.1 and passed
  `spirv-val --target-env vulkan1.2` during the focused RTX validation.
- `meson compile -C builddir-win worr_opengl_x86_64 worr_vulkan_x86_64 worr_rtx_x86_64 worr_engine_x86_64`
  - final run: no work remaining; all four targets built successfully.
- `python tools/check_shadowmapping_guardrails.py`
  - passed.
- `python tools/shadowmapping_repro_smoke.py --install-dir .install --renderer opengl --renderer vulkan --scene hom-regression --filter pcf --filter evsm --wait 8`
  - all four processes exited successfully; the scene contains no selected
    shadow light and therefore checks startup/no-HOM behavior only.
- `python tools/shadowmapping_repro_smoke.py --install-dir .install --renderer opengl --renderer vulkan --scene flashlight-owner --filter pcf --filter evsm --wait 16`
  - OpenGL PCF: one selected tracked light, one depth view rendered,
    `unsupported=0`, owner exclusion counted;
  - OpenGL EVSM: one selected tracked light, one moment view rendered,
    `unsupported=0`;
  - Vulkan PCF: the same one-light/one-depth-view result, `unsupported=0`;
  - Vulkan EVSM: the same one-light/one-moment-view result, `unsupported=0`.
- Live alias probes set `gl_shadow_softness 2.5` and
  `vk_shadow_softness 3.25`; `r_shadow_dump` reported those exact canonical
  policy values.
- A native Vulkan intensity probe set `r_intensity 9`; the render-frame clamp
  saturated it and its legacy alias, and the console reported
  `"intensity" is "5"`.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - staged current binaries and 274 packaged assets;
  - staged payload validation passed.
- `git diff --check`
  - passed.

The current repro smoke runner validates launch/exit and dump availability; it
does not yet assert pixels. That limitation is explicitly assigned to
`DV-03-T08` rather than being treated as visual proof.
