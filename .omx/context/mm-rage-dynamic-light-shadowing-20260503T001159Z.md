# Ralph Context Snapshot: mm-rage dynamic_light shadowing

## Task Statement
Investigate and resolve missing light/shadow submission for `dynamic_light` entities on map `mm-rage`. The reported example entity is at `768 176 256`; testing should use player coordinates `768 32 256` and angle `90`.

## Desired Outcome
All eligible map `light` and `dynamic_light` point/spot lights should apply light and cast shadows. Dynamic lights in `mm-rage`, including the reported area, should be visible to the shadowmapping system.

## Known Facts / Evidence
- The shadowmapping implementation is already present in shared renderer frontends and native OpenGL/Vulkan backends.
- Current client-side shadowlight submission is driven from server configstrings and `CL_AddShadowLights`.
- Static/entity shadowlight data flows through `src/game/sgame/gameplay/g_misc.cpp`, `src/game/sgame/gameplay/g_spawn.cpp`, `src/client/precache.cpp`, and `src/client/effects.cpp` / `src/game/cgame/cg_effects.cpp`.
- Prior fixes touched flashlight origin/sway, viewweapon lighting, entity caster filtering, PVS/fade handling, and map light fallbacks.
- `mm-rage.bsp` contains the reported `dynamic_light` at `768 176 256`; it has `shadowlightradius "200"` and `shadowlightconeangle "65"` but no explicit `shadowlightresolution`.
- Before the fix, the focused OpenGL dump showed `candidates=0` and `configstring_lights=0`.
- After the fix, OpenGL and Vulkan focused dumps from `768 32 256` both show `candidates=64`, `selected=4`, `configstring_lights=61`, with the cone light at `768 176 256` selected.

## Constraints
- Keep existing dirty worktree changes intact; do not revert unrelated edits.
- Use native Vulkan paths for Vulkan work; do not redirect to OpenGL.
- Treat `q2proto/` as read-only.
- Significant engineering changes should be documented under `docs-dev/`.
- Verification should include build/tests plus a focused `mm-rage` repro where possible.

## Unknowns / Open Questions
- Whether `mm-rage` is present in the repo staging area, packaged assets, or external game data.
- Whether the affected `dynamic_light` has no entity baseline/current state, an unsupported key format, bad configstring serialization, or client-side culling/filtering.
- Whether the reported light is point or spot and whether its direction/radius values are parsed correctly.

## Likely Codebase Touchpoints
- `src/game/sgame/gameplay/g_misc.cpp`
- `src/game/sgame/gameplay/g_spawn.cpp`
- `src/client/precache.cpp`
- `src/client/effects.cpp`
- `src/game/cgame/cg_effects.cpp`
- `tools/check_shadowmapping_guardrails.py`
- `docs-dev/renderer/`
