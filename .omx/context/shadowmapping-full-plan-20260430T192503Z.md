# Shadowmapping Full Plan Ralph Context - 2026-04-30T19:25:03Z

## Task Statement
Use the Ralph loop to study `docs-dev/proposals/shadowmapping-new-30apr2026.md` and fully implement the entire shadowmapping replacement plan for WORR.

## Desired Outcome
The repository should contain a complete, verified renderer-neutral shadowmapping replacement that satisfies the proposal's checklist, including the shared frontend, server visibility policy, deterministic view/page residency, native OpenGL and Vulkan raster backends, compare-depth and moment storage families where requested by quality modes, preserved rerelease shadowlight semantics, debug instrumentation, documentation, CI guardrails, and fresh build/test evidence plus architect approval.

## Known Facts / Evidence
- Earlier Ralph iterations implemented `inc/renderer/shadow_frontend.h`, `src/renderer/shadow_frontend.c`, native OpenGL compare-depth rendering/sampling in `src/rend_gl/shadow.c`, and native Vulkan compare-depth rendering/sampling in `src/rend_vk/vk_shadow.c`.
- Earlier verification passed targeted GL/VK renderer builds, a full `ninja -C builddir-win`, `.install` refresh, guardrail scan, SPIR-V validation, `meson test` (no tests defined), and OpenGL/Vulkan startup smokes with `r_shadowmaps 1`.
- Final prior architect review approved the compare-depth baseline after fixes for dynamic/static caster invalidation, cluster-mask overflow, and Vulkan local-page ownership.
- The proposal checklist is broader than the earlier compare-depth milestone. Remaining gaps include actual moment page support or explicit filter-family materialization reporting, richer one-shot/debug instrumentation, stronger tracked-entity light semantics, golden repro evidence artifacts, alpha/model exclusion policy, and completion documentation that maps checklist items to implementation state.
- `CS_SHADOWLIGHTS` is present in the protocol/game/client path and appears to be converted into shadow-capable dlights before the renderer frontend collects lights.
- `SHADOW_LIGHT_CLASS_TRACKED_ENTITY` exists in the frontend enum but needs verification and likely stronger use in light collection/cache identity.
- The OpenGL/Vulkan backends currently report no moment-page support and clamp VSM/EVSM requests to PCF.

## Constraints
- Follow the repo AGENTS instructions: document significant changes under `docs-dev/`, keep the roadmap current, refresh `.install/` after builds, keep `q2proto/` read-only, and never redirect Vulkan work to OpenGL.
- Keep the Vulkan renderer native.
- Use the existing code style and C cvar naming conventions.
- Avoid reverting unrelated dirty work.
- Use `apply_patch` for manual file edits.
- Ralph requires persistence, fresh verification, and architect sign-off before completion. OMX MCP state tools are unavailable, so the file-backed `.omx/state/shadowmapping-replacement/ralph-progress.json` ledger is used.

## Unknowns / Open Questions
- Whether VSM/EVSM moment rendering should be fully implemented now for both GL and Vulkan, or whether a proposal-complete v1 may expose deterministic materialization reporting while clamping unsupported filters.
- Whether direct renderer access to raw `CS_SHADOWLIGHTS` is needed, or whether the existing client conversion into `DL_SHADOW_LIGHT` is the intended preservation layer.
- Which debug overlay primitives already exist in this renderer and can be extended without creating a new debug rendering system.
- Whether scripted golden repros can be real executable tests in this repo or should be documented manual repro commands until test harnesses exist.

## Likely Codebase Touchpoints
- `inc/renderer/shadow_frontend.h`
- `src/renderer/shadow_frontend.c`
- `src/rend_gl/shadow.c`
- `src/rend_gl/main.c`
- `src/rend_gl/shader.c`
- `src/rend_vk/vk_shadow.c`
- `src/rend_vk/vk_shadow.h`
- `src/rend_vk/vk_world.c`
- `src/rend_vk/vk_world_spv.h`
- `.tmp/vk_world_shadow.vert`
- `.tmp/vk_world_shadow.frag`
- `src/client/effects.cpp`
- `src/client/view.cpp`
- `docs-dev/renderer/shadowmapping-replacement-baseline.md`
- `docs-dev/renderer/shadowmapping-native-backends-2026-04-30.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `tools/check_shadowmapping_guardrails.py`
