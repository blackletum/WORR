# Q3A BotLib Phase 0 MVP Closeout

Date: 2026-06-21

Tasks: `FR-04-T01`, `FR-04-T10`, `DV-03-T05`, `DV-07-T06`

## Summary

This closes the next ten Phase 0 checklist leaves after the reusable checklist
template. The closed leaves are the current import-ledger continuation gates,
the seven `FR-04-T01` MVP behavior leaves, and the existing-WORR notice
retention check.

No new source code, imported files, public cvars, or user-facing commands were
added for this closeout. The work is an acceptance and traceability update over
already-promoted runtime behavior, scenario harness rows, and source-ledger
records.

## Closed Checklist Leaves

- Current BSPC local-tailoring ledger rows are complete for the files modified
  so far: `tools/q2aas/bspc.c`, `tools/q2aas/be_aas_bspc.c`, and
  `tools/q2aas/map.c`. Future BSPC tailoring must open a new ledger row before
  code lands.
- Current Q3A BotLib runtime ledger rows are complete for the imported
  `src/game/sgame/bots/q3a/` runtime/AAS files now present. Future Q3A runtime
  imports must open ledger rows before code lands.
- Spawn and clean leave are covered by the bot slot lifecycle, profile-backed
  spawn, team-policy cleanup, map-change/restart cleanup, and final zero-bot
  scenario gates.
- Character/profile loading is covered by the Q3-style botfile loader,
  profile smoke, profile-backed scenario row, botfile validator, package
  coverage, and loose staging checks.
- AAS area lookup near spawn is covered by runtime AAS sample/status, frame
  command route state, current-area debug fields, and spawned route-command
  scenario rows.
- Route-to-item/roam behavior is covered by `spawn_route_to_item`, item-goal
  route selection, FFA roam route ownership, item role pickup scoring, and the
  implemented route debug/status families.
- Visible-enemy engagement with a basic weapon policy is covered by
  `engage_enemy`, `switch_weapons`, live aim, weapon metadata, carried-arsenal
  scoring, and attack-button application proofs.
- Simple stuck recovery is covered by `recover_from_stall`, stuck repath,
  short recovery commands, goal blacklist cooldown, and failed-goal reason
  diagnostics.
- FFA/TDM scoring and match flow participation are covered by
  `ffa_tdm_match_readiness`, team policy cleanup, Duel queue, warmup, vote,
  admin audit, tournament, MyMap, nextmap, map vote, scoreboard, intermission,
  and match logging scenario rows.
- Existing WORR/ZeniMax notices are retained on WORR-owned bot files touched by
  this bot/AAS project; imported Q3A/BSPC files retain upstream GPL notices and
  modified-import files carry `Modified for WORR` notes where applicable.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir
  .install --package-q2aas-aas`
- Focused MVP scenario set passed from
  `.tmp\bot_scenarios\20260621T210213Z`:
  `profile_backed_spawn,spawn_route_to_item,recover_from_stall,engage_enemy,switch_weapons,health_armor_pickup,ffa_tdm_match_readiness`
- Full implemented scenario suite passed 56 rows from
  `.tmp\bot_scenarios\20260621T210229Z`.

## Notes

- The reusable 12-row Checklist System template remains unchecked by design.
  It is a template for task checklists, not a task instance.
- Future imports, local edits to imported files, or new public controls should
  add fresh task-local checklist evidence rather than reopening this Phase 0
  closeout.
