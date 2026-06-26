# Q3A BotLib Runtime Entity and Lifecycle Closeout

Date: 2026-06-21

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Purpose

Close the next BotLib/AAS checklist slice by promoting the remaining
point-contents and visibility adapter callbacks to final WORR ownership,
hardening module-level BotLib initialization, documenting internal Q3A LibVar
policy, and expanding the first `Entity_UpdateState` coverage rows.

## Implementation

- Promoted `AAS_PointContents`, `AAS_inPVS`, and `AAS_inPHS` ownership in the
  BotLib boundary documentation. Point contents remains owned by the active-map
  Q2 BSP collision bridge, while visibility remains owned by the active-map Q2
  BSP leaf-cluster/PVS/PHS bridge in `q3a_botlib_import.c`.
- Split level unload from module shutdown. `BotLibAdapter_Init()` is now
  idempotent for the game-module lifetime, while `Bot_RuntimeShutdown()` calls
  the adapter shutdown path after `ShutdownGame()` has unloaded the current
  level and printed lifecycle smoke status.
- Documented the upstream `bot_*` LibVar policy: Q3A LibVars stay internal, the
  WORR public surface remains `sg_bot_*`, and only the `phys_*` / `rs_*`
  movement and reachability inputs are seeded for imported AAS movement code.
- Expanded BotLib entity snapshot classification to distinguish active human
  players, active bot clients, spectators, and monsters/NPCs before forwarding
  snapshots through imported `AAS_UpdateEntity`.
- Added runtime entity snapshot counters for those first four categories and
  exposed them through `sg_bot_debug_aas` status output.

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `git diff --check`

## Notes

- This round does not import new Q3A, BSPC, idTech3, Quake3e, baseq3a,
  Gladiator, or q2proto source files.
- Later entity snapshot rows for items, dropped items, hazards, movers, and
  objectives remain tracked separately in the plan.
