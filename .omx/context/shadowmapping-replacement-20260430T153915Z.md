# Ralph Context Snapshot: shadowmapping replacement

## Task statement

Study `docs-dev/proposals/shadowmapping-new-30apr2026.md` and methodically implement the full implementation plan under the Ralph workflow.

## Desired outcome

Replace the abandoned/fragile shadowmapping stack with a canonical, backend-neutral shadow frontend, deterministic policy and cache records, explicit GL/Vulkan backend adapter contracts, guardrails against reintroducing removed fallback/slot-churn behavior, and supporting development documentation/task tracking.

## Known facts/evidence

- The proposal identifies the existing historical shadowmapping work as architecturally unstable: camera visibility and shadow visibility were coupled, shadow passes mutated main-view state, caster bounds and bmodel transforms were repeatedly fixed, sampling vectors were inconsistent, and cache policy evolved through contradictory experiments.
- Current focused grep finds the GL renderer tree now only exposing legacy ground shadows through `gl_shadows`; the removed OpenGL shadowmap stack is represented mainly in historical docs.
- Existing server send logic already uses `DVIS_PVS2` for shadow-affecting entities while retaining PHS for sound/beam-style behavior.
- Rerelease-compatible symbols already exist: `RF_CASTSHADOW`, `RF_NOSHADOW`, `MAX_SHADOW_LIGHTS`, `CS_SHADOWLIGHTS`, and `shadow_light_data_t`.
- Vulkan RTX sun shadow code exists separately under `src/rend_rtx/vkpt`, but the requested work forbids redirecting Vulkan renderer paths to OpenGL.

## Constraints

- Follow `AGENTS.md`: significant work must be documented under `docs-dev/`, task work must reference project tasks, and the canonical SWOT roadmap must stay current.
- Treat `q2proto/` as read-only.
- Do not redirect Vulkan renderer work to OpenGL; Vulkan paths must remain native.
- Preserve rerelease gameplay/network semantics for `RF_CASTSHADOW`, `RF_NOSHADOW`, and `CS_SHADOWLIGHTS`.
- Do not reintroduce no-slot unshadowed fallback, sticky slot retention, or cache residency coupled to active-slot order.
- Do not overwrite unrelated dirty work in the repository.

## Unknowns/open questions

- Exact build target available in the local environment.
- Whether the intended native non-RTX Vulkan renderer files are present or still planned.
- The desired final atlas/cube-array storage choice remains intentionally deferred by the proposal; v1 should document and enforce a fixed-page/2D-array-compatible policy.

## Likely codebase touchpoints

- Shared renderer contract: `inc/renderer/renderer.h` and a new renderer-neutral shadow frontend module.
- GL renderer integration: `src/rend_gl/main.c`, `src/rend_gl/gl.h`, `src/rend_gl/world.c`, and build files.
- Server visibility policy: `src/server/entities.c`.
- Client shadowlight parsing/emission: `src/client/precache.cpp`, `src/client/effects.cpp`.
- Build/regression guardrails: `tools/` and Meson targets.
- Docs/task tracking: `docs-dev/renderer/`, `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`, and possibly project task docs.

