# Shadowmapping Full Plan Closure

Date: 2026-04-30

Tasks: `FR-02-T09`, `FR-02-T10`, `FR-02-T11`, `DV-02-T06`, `DV-07-T05`

## Scope

This note records the Ralph follow-up that closed the remaining implementation gaps from `docs-dev/proposals/shadowmapping-new-30apr2026.md` after the first native backend pass. It extends the baseline from compare-depth v1 into the full shared frontend and non-RTX GL/Vulkan feature set.

## Closure Matrix

- Contract freeze: `docs-dev/renderer/shadowmapping-replacement-baseline.md` is the canonical contract, and `tools/check_shadowmapping_guardrails.py` bans the removed no-slot fallback and sticky slot-retention paths.
- Shared frontend: `shadow_light_desc_t`, `shadow_view_desc_t`, `shadow_caster_t`, `shadow_cache_key_t`, `shadow_page_id_t`, and `shadow_backend_ops_t` own light selection, caster lists, cache residency, dirty reasons, filter policy, debug policy, and backend materialization reporting.
- Rerelease semantics: `CS_SHADOWLIGHTS` resolution/style/source metadata now survives into `dlight_t` and the frontend; point, cone, owner/configstring, strict-PVS, and tracked-entity data are preserved for shadow selection.
- Visibility/touching: candidate lights use PVS2/area gating; caster drawing uses dedicated caster lists plus light-volume and cluster-overlap checks, independent from camera-visible entity lists.
- World occluders: GL and Vulkan shadow passes cull BSP faces against the active shadow view and maintain separate shadow counters instead of touching main-view `visframe`.
- Residency: active views bind persistent resident pages keyed by owner, face/cascade, projection, resolution, filter, storage, and BSP revision. LRU is eviction-only, not scoring.
- Cache modes: mode `2` is literal world-only reuse, so entity casters are excluded from cached page rendering instead of being baked once and reused stale.
- Resource split: hard/PCF/PCSS use depth pages. VSM/EVSM use moment pages. GL uses `GL_RGBA16F` with generated mips; Vulkan queries color formats and generates mips when the format supports linear blits.
- Quality policy: PCSS is limited by `r_shadow_pcss_max_lights` and demotes the rest to PCF; nonzero slope and normal-offset bias defaults ship with a shared bias scale; sun cascades are separately budgeted and receiver sampling blends near cascade edges.
- Caster materialization: GL and Vulkan now render frontend-selected brush, MD2/alias, and MD5 skeletal caster geometry instead of bounding boxes. The GL backend preserves CPU copies of uploaded model buffers for shadow emission; the Vulkan backend uses a native entity-geometry callback into `vk_entity.c`.
- Sun stability: cascades are fitted to camera frustum splits, padded in light space, and texel-snapped before cache-key generation.
- Bias parity: Vulkan receiver sampling applies normal-offset bias before projecting into each shadow page, matching the GL receiver path.
- Alpha/content policy: translucent casters are excluded by default, `r_shadow_alpha_mode` can opt into solid cutout approximation, and `r_shadow_model_exclusion_list` provides a model-path exclusion list for content-specific no-cast cases.
- Instrumentation: `r_shadow_dump`, `gl_shadow_dump`, and `vk_shadow_dump` report frontend stats, materialization, selected lights, views, dirty reasons, and focused caster chains. `r_shadow_draw_debug` draws candidate/selected lights, cones, cascades, caster bounds, page ids, and dirty data through available renderer debug hooks.
- Repro workflow: `tools/shadowmapping_repro_smoke.py` provides repeatable local launch commands for the plan's off-PVS, moving bmodel, translated MD2, projectile self-shadow, sun-cascade, and HOM scene families.

## Backend Notes

Both GL and Vulkan use 2D-array face pages for v1 parity. This keeps point-light face choice, page ids, uniform layout, and receiver shader behavior aligned across the two non-RTX backends. A packed atlas or true cube-array path remains a later optimization, not part of this correctness baseline.

Vulkan debug drawing remains a no-op at the renderer hook level, matching the existing native Vulkan debug stubs. The one-shot dump path is backend-neutral and works in both GL and Vulkan.

## Verification Evidence

Commands run from `E:\Repositories\WORR`:

- `ninja -C builddir-win`
- `python tools/check_shadowmapping_guardrails.py`
- `python -m py_compile tools/shadowmapping_repro_smoke.py tools/check_shadowmapping_guardrails.py`
- `C:\VulkanSDK\1.4.313.1\Bin\spirv-val.exe .tmp\vk_world_shadow.vert.spv`
- `C:\VulkanSDK\1.4.313.1\Bin\spirv-val.exe .tmp\vk_world_shadow.frag.spv`
- `C:\VulkanSDK\1.4.313.1\Bin\spirv-val.exe .tmp\vk_shadow_moment.frag.spv`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `meson test -C builddir-win --print-errorlogs`
- `python tools\shadowmapping_repro_smoke.py --dry-run --scene off-pvs-light --filter pcf --renderer opengl`
- `python tools\shadowmapping_repro_smoke.py --scene moving-bmodel --scene translated-md2 --filter pcf --wait 120`
- `python tools\shadowmapping_repro_smoke.py --scene sun-cascade --filter vsm --filter evsm --filter pcss --wait 120`
- `git diff --check`

Result: the full build linked all configured targets, guardrails passed, both Python tools compiled, all three SPIR-V modules validated, `.install` refreshed and validated, Meson reported no tests defined for this build, the repro dry run emitted the expected staged command, moving-bmodel and translated-MD2 smokes exited `0` on both OpenGL and Vulkan, and both OpenGL and Vulkan staged dump smokes exited `0` for VSM, EVSM, and PCSS sun-cascade runs. The logs confirmed moment materialization for VSM/EVSM and depth materialization for PCSS on both backends.
