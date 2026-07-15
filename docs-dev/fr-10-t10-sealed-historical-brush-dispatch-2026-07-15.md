# FR-10-T10 sealed historical brush dispatch

Date: 2026-07-15

Project task: `FR-10-T10`

Status: implemented fail-closed historical brush-query increment; full mover
runtime/load/release promotion remains open.

## Outcome

Canonical lag-compensation queries now use the mover poses previously captured
in their sealed rewind scene. The implementation replaces each represented
live mover in the current-entity baseline with exactly one immutable BSP trace
at the transform stored in that scene.

The sequence is:

1. Build and validate the sealed canonical scene for the accepted command.
2. Resolve every brush candidate's live entity generation and current
   map-epoch-bound immutable asset. Any mismatch rejects the entire historical
   scene and executes the existing uncompensated current-world trace.
3. Run the current-world baseline while omitting only those generation-checked
   live movers. World BSP, unrelated entities, and ignored/pass-entity rules
   continue through the normal trace path.
4. Dispatch each immutable brush asset using the historical scene origin and
   angles, merge its result with historical player-bounds proxies, and attach
   the trace only to the revalidated current mover entity.

No mover edict is moved, unlinked, relinked, or used as transformed collision
input. The engine provider receives a pointer-free asset handle, contents mask,
trace endpoints/bounds, and the copied historical transform. It returns no
entity; sgame sets the entity only after the generation check.

If the merged query is ultimately blocked by one of those historical movers,
the observation journal records a historical hit rather than a historical miss.
This distinguishes compensated brush blocking from an unblocked rewind query
without misclassifying unrelated live baseline entities as historical results.

## Bounded safety and rejection behavior

The historical brush context is fixed at the existing 64 mover-track maximum.
It caches one immutable asset descriptor and one current entity reference per
sealed mover, so the baseline can exclude movers without allocations or live
world mutation. A candidate is rejected when any of these invariants fail:

- the scene is not still valid and sealed;
- the current provider map epoch differs from the sealed scene;
- the current entity no longer resolves to the same generation-qualified mover;
- the inline brush asset, collision asset ID, or captured local bounds differs;
- the transformed trace callback rejects the request; or
- more brush candidates are present than the bounded context can represent.

All such failures return the existing uncompensated `NativeTrace` rather than
mixing a partially replaced current scene with historical state. The
before/after observation fingerprint now includes every eligible live mover as
well as players, and rate-limited `lagcomp` diagnostics report dispatched and
rejected historical brush counts. Historical mover blockers are also classified
as historical hits in the observation journal.

The brush path intentionally does not apply the axis-aligned player-proxy
bounds cull: a rotated inline BSP model cannot be safely rejected using an
unrotated AABB. The per-query cost is therefore explicitly bounded by 64
transformed brush calls, pending measured load budgets and broader promotion.

## ABI boundary

`rewind_collision_import.hpp` now mirrors the full fixed trace-request layout
and uses sgame's ABI-compatible `trace_t` for the output callback. Compile-time
assertions cover the request's 104-byte size and critical offsets, in addition
to the existing provider/asset layout checks. The C engine provider still owns
the authoritative map/asset validation and performs the transformed BSP trace.

## Validation

Focused validation passed:

```text
ninja -C builddir-win sgame_x86_64.dll

meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-lag-compensation-mover-capture-contract \
  network-rewind-core \
  network-rewind-collision-provider \
  network-rewind-collision-real-bsp-parity \
  network-rewind-acceptance-probe
```

The five focused gates prove common scene/mover integrity, engine provider
request and transactional rejection behavior, real-BSP transformed-trace
parity, acceptance evidence, live sgame ordering/dispatch wiring, and
historical-mover hit classification. No interactive client is launched.

Final validation results:

- The focused gates passed 5/5, then passed 15/15 across three consecutive
  repetitions.
- The existing-build networking suite passed 129/129 with
  `meson test -C builddir-win --no-rebuild --suite networking --print-errorlogs`.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64` refreshed and
  validated the staged runtime.
- The headless staged dedicated-server command-gap gate completed and wrote
  `.tmp/networking/command-gap-acceptance.json`.

A normal Meson regeneration is currently prevented by an unrelated dirty
`meson.build` reference to the absent generated renderer header
`src/rend_vk/vk_dof_spv.h`. The current sgame DLL compiled before that
regeneration failure; this networking increment neither changes nor generates
renderer files.

## Remaining promotion gates

`FR-10-T10` remains open. The next evidence must include a dedicated moving
brush runtime scenario with player-on-mover and blocker cases, moving-platform
fairness decisions per hitscan weapon, broader rotating/complex BSP and BSPX
coverage, per-frame/query CPU and memory budgets under concurrency, sustained
soak, and supported-platform release evidence. Player mover-relative fields are
captured and sealed; explicit transform composition should be added only with a
separate collision-model decision and its own deterministic scenarios.

## Roadmap completion

- Overall strategic roadmap: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.
- This is a subtask increment; no parent checkbox is closed.
