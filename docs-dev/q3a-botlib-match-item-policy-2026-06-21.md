# Q3A BotLib Match Item Policy Umbrella

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off `sg_bot_match_item_policy` umbrella cvar for the
current match item-policy pickup scoring proofs. When enabled, it activates the
existing FFA, CTF, and TDM item-role scoring bridges plus the TDM deny-enemy
resource-denial scoring bridge without requiring the individual proof cvars.

The first promoted use is the four-bot TDM `match_item_policy` scenario in
server smoke mode `51`. It verifies that the umbrella cvar alone drives both
TDM item-role pickup scoring and deny-enemy resource scoring while
`sg_bot_team_item_roles` and `sg_bot_team_resource_denial` remain disabled.

## Implementation

- `src/game/sgame/bots/bot_nav.cpp` now registers
  `sg_bot_match_item_policy` lazily and treats it as an enable source for the
  existing match item-role and team resource-denial scoring gates.
- `src/server/main.c` reserves smoke mode `51`, sets the TDM scenario cvars,
  emits a `match_item_policy` begin marker, and resets the umbrella cvar in the
  common runtime cleanup path.
- `tools/bot_scenarios/run_bot_scenarios.py` adds the implemented
  `match_item_policy` row with hard gates for TDM readiness, objective
  item-role/resource policy selections, nav score boosts, selected pickup
  goals, zero invalid skips, and latest TDM item/resource metadata.
- `src/game/sgame/bots/bot_brain.cpp` now prints the compact
  `last_team_objective_lane` and `last_team_objective_target_source` fields at
  the front of the objective-detail status row so long 246-frame CTF counters
  cannot hide the proof fields before the scenario parser sees them.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests.
- `meson compile -C builddir-win worr_ded_engine_x86_64`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
- Focused `match_item_policy` passed from `.tmp\bot_scenarios\20260621T203348Z`.
- Focused `ctf_dropped_flag_route` status-regression rerun passed from `.tmp\bot_scenarios\20260621T204037Z`.
- Full implemented suite passed 56 short-run rows from `.tmp\bot_scenarios\20260621T204044Z`.

## Provenance

No new Q3A, Gladiator, BSPC, idTech3, or q2proto source files were imported or
modified. The changes are WORR-owned behavior glue, status ordering, smoke-mode
plumbing, and scenario validation layered on top of the existing native
match-policy and item/resource scoring helpers.
