# Ralph Context Snapshot: shadowmapping native backend completion

## Task statement

Resume the shadowmapping replacement implementation under the Ralph workflow and loop until the implementation plan in `docs-dev/proposals/shadowmapping-new-30apr2026.md` is complete.

## Desired outcome

Finish the remaining native renderer work after the shared frontend baseline: OpenGL must allocate/render/sample shadow pages visibly when `r_shadowmaps 1`, Vulkan must implement its own native raster shadow page model without redirecting to OpenGL, documentation and roadmap status must match the completed implementation, and verification must include fresh build/regression evidence plus architect review.

## Known facts/evidence

- The shared shadow frontend, canonical docs, server visibility policy, guardrail script, and GL/VK hooks already exist.
- `worr_vulkan_x86_64.dll` and `worr_opengl_x86_64.dll` linked after fixing renderer-local `AddPointToBounds` and box-leaf traversal dependencies.
- GL and Vulkan backend ops currently advertise no page-render support: no `ensure_page`, no `render_view`, no sampler binding, and no visible shadow application.
- The proposal acceptance criteria require actual GL and native Vulkan backends under the shared frontend, not just selection/caching policy.

## Constraints

- Follow `AGENTS.md`: keep engineering docs under `docs-dev/`, keep the SWOT roadmap current, and reference task IDs.
- Do not touch `q2proto/`.
- Never redirect Vulkan renderer paths to OpenGL.
- Do not reintroduce the banned sticky/no-slot/fallback shadow stack.
- Do not overwrite unrelated dirty files in the repository.
- Prefer a fixed-page 2D-array-compatible v1 path before attempting any KEX-style atlas.

## Unknowns/open questions

- Exact GL shader and draw-call surface needed for a minimal visible page render/sample path.
- Exact Vulkan render-pass/pipeline integration points for the non-RTX raster backend.
- Whether full configured builds remain blocked by pre-existing third-party archive/builddir issues.

## Likely codebase touchpoints

- Shared frontend contract: `inc/renderer/shadow_frontend.h`, `src/renderer/shadow_frontend.c`.
- OpenGL backend: `src/rend_gl/main.c`, `src/rend_gl/gl.h`, `src/rend_gl/shader.c`, `src/rend_gl/world.c`, `src/rend_gl/mesh.c`, `src/rend_gl/tess.c`, `src/rend_gl/qgl.*`.
- Vulkan backend: `src/rend_vk/vk_main.c`, `src/rend_vk/vk_local.h`, `src/rend_vk/vk_world.c`, `src/rend_vk/vk_entity.c`, shader SPV generation or embedded shader definitions.
- Docs/task tracking: `docs-dev/renderer/shadowmapping-replacement-baseline.md`, `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.
