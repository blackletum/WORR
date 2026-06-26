# Q3A BotLib Runtime and Adapter Implementation Closeout

Date: 2026-06-21

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Purpose

Close the remaining imported runtime/adapter implementation-log checklist row
for the WORR Q3A BotLib/AAS port. This document consolidates the current
runtime surface now implemented in `sgame` and records what is deliberately
outside the imported subset.

## Implemented Runtime Surface

- `src/game/sgame/bots/q3a/` contains the current commit-pinned imported Q3A
  AAS runtime subset used by WORR: file loading, setup/start-frame, sampling,
  reachability, route queries/cache, alternative routes, clustering,
  optimization hooks, entity cache, movement prediction, debug helpers, LibVars,
  memory helpers, and CRC utilities.
- `src/game/sgame/bots/bot_runtime.*` owns WORR runtime gating, active-map AAS
  load/unload status, frame lifecycle calls, entity snapshot categories,
  public `sg_bot_*` cvars, and developer status output.
- `src/game/sgame/bots/botlib_adapter.*` owns the narrow callback bridge from
  imported Q3A code into WORR: filesystem reads, prints, memory accounting,
  time/vector helpers, active-map BSP entity/model/collision/visibility data,
  debug drawing, and dynamic entity collision through the server-game `gi.clip`
  path.
- `src/game/sgame/bots/bot_nav.*` and `src/game/sgame/bots/bot_brain.cpp` own
  the WORR command-side consumption of AAS output, including route caching,
  route goals, movement-state command construction, stuck recovery, debug
  overlays, and current frame-command status.
- The imported Q3A arena AI, EA command layer, bot goal/weight system, chat, and
  game-mode behavior files remain reference-only. WORR behavior continues to be
  implemented natively above the AAS runtime.

## Adapter Ownership Decisions

- Static-world `AAS_Trace` and `AAS_PointContents` are final-owned by the
  active-map Q2 BSP collision bridge.
- `AAS_inPVS` and `AAS_inPHS` are final-owned by the active-map Q2 BSP
  visibility bridge.
- `AAS_EntityCollision` is routed through `botlib_adapter.*`,
  `BotRuntimeEntityTrace`, and the server-game `gi.clip` bridge for linked
  dynamic entities.
- Upstream Q3A `bot_*` LibVars stay internal to imported AAS behavior. Public
  server policy remains under `sg_bot_*`.
- Runtime shutdown is explicit at game-module shutdown so imported memory and
  filesystem state can be audited after map unload and repeated lifecycle tests.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `meson compile -C builddir-win worr_ded_engine_x86_64`
- `meson compile -C builddir-win q2aas-staged-smoke`
- `meson compile -C builddir-win q2aas-stage-aas`
- `meson compile -C builddir-win q2aas-stage-audit`
- Static release workflow evidence: `.github/workflows/nightly.yml` and
  `.github/workflows/release.yml` both consume `tools/release/targets.py`, whose
  matrix includes `linux-x86_64` on `ubuntu-latest` and `macos-x86_64` on
  `macos-15-intel`; both workflows run `meson compile -C builddir` for
  Linux/macOS. The `q2aas` Meson option defaults to enabled, so the generator
  target is part of those release builds.

## Provenance

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto files were
  imported for this closeout.
- Existing imported Q3A files remain covered by the per-file ledger in
  `docs-dev/q3a-botlib-aas-credits.md`.
- This document is a closeout index for existing runtime and adapter
  implementation, not a new source import.
