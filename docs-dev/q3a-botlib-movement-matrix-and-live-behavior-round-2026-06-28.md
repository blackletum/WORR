# Q3A BotLib Movement Matrix And Live Behavior Round

Date: 2026-06-28

Tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T04`, `FR-04-T05`,
`FR-04-T06`, `FR-04-T11`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`,
`DV-03-T05`, and `DV-07-T06`.

## Summary

This round expands the bot implementation roadmap from the profile-backed
autofill and roam stabilization work into a broader movement, retreat, and live
decision-making pass. The scenario catalog now has 110 implemented rows with no
pending rows, and the highest bot frame-command smoke mode is `94`.

The work stays WORR-native. No new Q3A, Gladiator, BSPC, idTech3, Quake3e,
baseq3a, or `q2proto/` source files were imported or modified.

## Implementation Notes

- `src/server/main.c` reserves movement matrix modes `92`, `93`, and `94`.
  Modes `93` and `94` prove map-backed swim and waterjump route ownership on
  `q2dm2`; mode `92` remains reserved for crouch route diagnostics as suitable
  map coverage is identified.
- `tools/bot_scenarios/run_bot_scenarios.py` adds eleven implemented movement
  scenarios: forced jump, crouch, and swim command rows; map-backed jump,
  ladder, walk-off-ledge, elevator, barrier-jump, rocket-jump, swim, and
  waterjump rows.
- `src/game/sgame/bots/bot_brain.cpp` now lets live item decisions defer a
  generic active FFA roam route, preserves active FFA roam goals instead of
  regenerating from the bot's current facing every frame, and avoids snapping
  view angles to visible enemies unless the selected decision is actually
  firing.
- Role combat now defers when the base action layer is switching weapons,
  underpowered/weak, or not producing a real attack decision. This keeps a
  nearby visible opponent from stealing route, item, retreat, or recovery
  ownership simply because the bot can see them.
- Coop mode `3` route-readiness now counts as valid reservation evidence when
  coop cvars own the row, keeping existing coop readiness rows aligned with the
  current command status contract.
- `src/game/sgame/bots/bot_nav.cpp` keeps last item-goal telemetry after an
  item-goal clear so scenario evidence can report the last assigned pickup even
  after the active reservation has been released.
- Chat live-event scenarios now gate durable counters instead of final
  last-event ordering, because natural follow-up events can legitimately occur
  after the event under test.

## Validation

- Build:
  `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64`
  passed. Ninja still reports the existing `premature end of file; recovering`
  warning before completing successfully.
- Install refresh:
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  passed, packaging 94 files and validating 31 botfile release payload files.
- Full bot scenario suite:
  `.tmp\bot_scenarios\implemented_rerun_after_fixes\20260627T234219Z`
  passed 110/110 implemented rows with zero failures, timeouts, errors, or
  pending rows.
- Movement matrix:
  `.tmp\bot_scenarios\movement_matrix_expansion_rerun\20260627T232805Z`
  passed 11/11 rows covering forced jump/crouch/swim commands and map-backed
  jump, ladder, walk-off-ledge, elevator, barrier-jump, rocket-jump, swim, and
  waterjump routes.
- Focused behavior sanity:
  `.tmp\bot_scenarios\behavior_sanity_rerun\20260627T232911Z`
  passed 18/18 rows covering spawn-to-item, profile-backed spawn, FFA/Duel
  pacing, behavior arbitration, combat/survival, threat retreat, role combat,
  and coop interaction rows.
- Direct `.install` min-player smoke:
  `.tmp\bot_scenarios\min_players_direct_absolute_rerun` loaded 5 active
  profiles from the packaged botfile manifest and autofilled `B|Bulwark`,
  `B|Relay`, and `B|Vanguard`.
- Unit and package tests:
  `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -q`
  passed 54 tests, and
  `python -m pytest tools\test_package_assets.py tools\q2aas\test_validate_worr_q2aas.py -q`
  passed 17 tests.
- Naming audit:
  `rg -n "sg_bot_|sv_bot_" src tools assets docs-user --glob '!**/__pycache__/**'`
  found no current public source, tool, asset, or user-doc references to the
  retired prefixes.

## Follow-Up

- Find or add a reference-map crouch route row for mode `92`; the forced crouch
  command row is covered, but natural map-backed crouch travel remains a
  diagnostic gap.
- Expand M6 next into doors, teleporters, hazards, and map-specific fallback
  behavior rather than simply adding more route-type rows.
- Run a fresh source-counter eight-bot soak after the movement matrix settles.
