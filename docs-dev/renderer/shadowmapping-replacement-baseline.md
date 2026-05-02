# Shadowmapping Replacement Baseline

Date: 2026-04-30

Tasks: `FR-02-T09`, `FR-02-T10`, `FR-02-T11`, `DV-02-T06`, `DV-07-T05`

## Purpose

This document is the canonical WORR shadowmapping replacement contract. It supersedes the earlier OpenGL-only shadowmap experiment notes when they conflict with the 2026-04-30 replacement plan.

The baseline is intentionally frontend-first: shadow light selection, caster collection, view residency, cvar policy, and guardrails live in renderer-neutral code. GL, native Vulkan, and RTX/vkpt backends must consume the same selected shadow views rather than inventing separate slot, fallback, or cache policy.

## Non-Negotiable Contract

- Shadow visibility is separate from camera visibility.
- Camera/PVS2/area tests may reject candidate lights, but caster drawing is decided by light-volume overlap.
- Shadow passes must not mutate main-view visibility state (`visframe`, view clusters, or main cluster cache).
- Selection and residency are separate concepts.
- Active frame bindings point at persistent resident pages.
- LRU is allowed only for page eviction, not for light scoring.
- Removed no-slot fallback and sticky slot-churn behavior must not return.
- Vulkan shadowmapping must be native Vulkan work. It must not redirect to OpenGL.

## Implemented Baseline

`inc/renderer/shadow_frontend.h` and `src/renderer/shadow_frontend.c` define the shared frontend:

- `shadow_light_desc_t`
- `shadow_view_desc_t`
- `shadow_caster_t`
- `shadow_cache_key_t`
- `shadow_page_id_t`
- `shadow_backend_ops_t`

The frontend now:

- collects point and cone lights from `refdef_t.dlights`;
- optionally adds cvar-driven sun/cascade shadow views through the same frontend scheduler;
- keeps sun cascade selection separate from the local-light budget;
- preserves rerelease light semantics through existing `dlight_t.shadow` values (`DL_SHADOW_LIGHT`, `DL_SHADOW_DYNAMIC`);
- builds a shadow-only caster list from `refdef_t.entities`, independent from GL camera draw lists;
- asks backend adapters for authoritative model bounds before admitting a caster, so MD2 frame bounds and inline BSP model bounds are not replaced by origin-radius guesses;
- uses PVS2/area checks only at the candidate-light stage;
- performs light/caster overlap checks with a cheap light-influence cluster mask and precise sphere/cone confirmation;
- writes a filtered caster-index span for each shadow view before calling a backend;
- creates six point-light views, one cone-light view, or configured sun cascade views;
- assigns stable cache keys by owner, view type, face/cascade, quantized projection parameters, resolution, filter family, storage family, and BSP revision;
- tracks persistent resident pages separately from active frame views;
- reserves sun cascade resident pages separately from local-light pages;
- records explicit dirty reasons: moved caster, animated caster, light params, filter family, world BSP, eviction, and new page;
- stores the caster membership and quantized bounds hash last rendered into each resident page, so a cached page is dirtied when any entity caster appears, disappears, moves, changes bounds, leaves that light volume, or changes membership;
- treats truncated light/caster cluster masks as invalid and falls back to precise light-volume checks, so the cheap cluster reject cannot become a false reject for large bounds;
- exposes freeze-selection and freeze-dirtying modes;
- provides a runtime visibility guard that drops if a frontend/backend shadow pass changes main-view GL visibility state.

Current GL and native Vulkan hooks call the frontend during `R_RenderFrame` and now materialize both storage families natively:

- OpenGL allocates a sampled depth `GL_TEXTURE_2D_ARRAY` for hard/PCF/PCSS and an optional `GL_RGBA16F` moment array for VSM/EVSM, renders frontend views into per-layer framebuffer attachments, uploads page matrices through a `ShadowPages` UBO, generates moment mip chains, and samples the pages in the dynamic shader path.
- Native Vulkan allocates sampled depth-array `VkImage` storage for hard/PCF/PCSS and optional queried color-array storage for VSM/EVSM moments, records the shadow pass before the main render pass, transitions the shadow and moment images with explicit barriers, exposes a shadow descriptor set, generates moment mip chains when the selected format supports linear blits, and samples the pages from the world fragment shader.
- Vulkan local-light shadow pages are mapped into the owning dynamic light in the same shadow descriptor UBO; the world fragment shader applies those pages while evaluating that light, while sun cascades remain a separate global lightmap factor.

Both backends advertise the materialized storage families they actually support and expose the selected path through `r_shadow_dump`, `gl_shadow_dump`, or `vk_shadow_dump`.

## Server Visibility Policy

`sv_shadow_strict_replication` controls the acknowledged multiplayer tradeoff:

- `0` (default): shadow-affecting entities may use the existing PVS2 expansion, preserving rerelease-style shadow relevance.
- `1`: shadow-affecting entities are replicated through strict normal PVS unless another rule, such as PHS sound/beam handling or explicit force visibility, applies.

PHS remains scoped to sound/beam-style visibility.

## Cvar Inventory

Keep:

- `gl_shadows`: legacy projected ground shadows, not shadowmaps.
- `r_shadowmaps`: shared frontend enable.
- `r_shadowmap_size`: default requested page resolution.
- `r_shadowmap_lights`: local light budget.
- `r_shadowmap_dynamic`: include dynamic dlights.
- `r_shadowmap_cache_mode`: `0` no reuse, `1` static reuse dirtied by dynamic caster overlap, `2` world-only reuse. Mode `2` renders BSP world occluders only and deliberately excludes entity casters from cached pages.
- `r_shadow_filter`: `0` hard, `1` PCF, `2` VSM, `3` EVSM, `4` PCSS.
- `r_shadow_pcss_max_lights`: top-N PCSS budget.
- `r_shadow_bias_slope`, `r_shadow_normal_offset`, `r_shadow_bias_scale`.
- `r_shadow_draw_debug`, `r_shadow_debug_light`.
- `r_shadow_freeze_selection`, `r_shadow_freeze_dirtying`.
- `r_shadow_alpha_mode`.
- `r_shadow_model_exclusion_list`.
- `r_shadow_sun`, `r_shadow_sun_cascades`, `r_shadow_sun_resolution`.
- `r_shadow_sun_direction`, `r_shadow_sun_distance`, `r_shadow_sun_size`.
- `sv_shadow_strict_replication`.

Commands:

- `r_shadow_dump [focus]`: one-shot frontend/backend dump. The optional focus value matches a light source index, owner entity, or source configstring.
- `gl_shadow_dump [focus]` and `vk_shadow_dump [focus]`: backend aliases.

Compat aliases:

- GL aliases are registered with `gl_` names such as `gl_shadowmaps`, `gl_shadowmap_size`, `gl_shadowmap_lights`, and `gl_shadowmap_filter`.
- Native Vulkan aliases are registered with `vk_` names such as `vk_shadowmaps`, `vk_shadowmap_size`, `vk_shadowmap_lights`, and `vk_shadowmap_filter`.
- Sun and debug frontend policy follows the same alias rule, for example `gl_shadow_sun` and `vk_shadow_sun`.
- Shared `r_` cvars are canonical. Backend aliases are no-archive compatibility inputs.

Deleted and banned:

- `gl_shadowlight_no_slot_mode`
- `gl_shadowmap_pvs_priority`
- `gl_shadowmap_hysteresis`
- `gl_shadowmap_sticky_ms`
- `gl_shadowmap_sticky_boost`
- `gl_shadowmap_cache_lights`
- no-slot fallback counters/paths such as `fallback_no_slot`

`tools/check_shadowmapping_guardrails.py` enforces the banned list across renderer source trees and is wired into release/nightly CI before configure/build work begins.

## Resource Model

The frontend chooses storage family from filter family:

- hard, PCF, and PCSS: compare-depth pages;
- VSM and EVSM: moment pages.

Backends must expose the materialized storage they actually support through `shadow_backend_ops_t`. Version 1 uses fixed resident page ids; a KEX-style packed atlas is explicitly deferred until deterministic fixed-page residency and dirtying are proven.

## Backend Requirements

GL backend status:

- raw sampled depth pages for hard/PCF/PCSS are implemented, with manual hard compare, PCF, and PCSS receiver filtering;
- array-page framebuffer attachment is implemented through `glFramebufferTextureLayer`;
- `GL_RGBA16F` moment pages, moment rendering, mip generation, VSM sampling, and EVSM sampling are implemented;
- world occluders and frontend-selected brush, alias, and skeletal caster geometry are rendered from frontend views only;
- world occluders are culled against the light sphere/cone/cascade view before submission;
- receiver sampling is implemented through the dynamic shader path;
- `r_shadow_draw_debug` overlays candidate lights, selected lights, cones, cascades, caster bounds, page ids, and dirty data through the existing GL debug-draw layer.

Native Vulkan backend status:

- native `VkImage`/`VkImageView` page storage is implemented;
- explicit shadow-to-main image barriers are implemented;
- per-layer 2D-array views and framebuffers are implemented;
- queried moment color formats, moment render pass attachments, moment sampling, and optional mip generation are implemented;
- world occluders are culled against the light sphere/cone/cascade view before vertex upload;
- world receiver sampling is implemented through the native Vulkan world shader;
- command-buffer threading remains deferred until correctness is stable.

RTX/vkpt may keep its terrain/sun shadowmap internals, but shared local-light policy should converge on this frontend before adding new non-RTX parity work.

## Verification Gates

Required automated gates:

- `python tools/check_shadowmapping_guardrails.py`
- `python tools/shadowmapping_repro_smoke.py --dry-run`
- compile changed renderer/server objects, then full `meson compile -C <builddir>` when the configured toolchain is healthy;
- `tools/refresh_install.py` after successful full build to refresh `.install/`.

Latest implementation log: `docs-dev/renderer/shadowmapping-native-backends-2026-04-30.md`.

Repeatable repro scene families are encoded in `tools/shadowmapping_repro_smoke.py`:

- off-PVS light affecting visible space;
- moving bmodel near a shadowlight;
- translated MD2 caster bounds;
- projectile/explosion self-shadow suppression;
- sun cascade shimmer;
- HOM regression.
