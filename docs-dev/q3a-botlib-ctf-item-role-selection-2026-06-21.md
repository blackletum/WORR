# Q3A BotLib CTF Item Role Selection Proof

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off WORR-native pickup selection bridge for Capture
The Flag item-role policy. The new `sg_bot_ctf_item_roles` cvar lets the
existing CTF match item-role helper shape live pickup-goal scoring in
`bot_nav` before normal item utility and distance penalty select the final
route goal.

The scope remains deliberately narrow: this is a CTF live item-route scoring
proof, not complete autonomous flag-role behavior. No Q3A, Gladiator, BSPC,
idTech3, or `q2proto/` source files were imported or modified.

## Implementation

- `src/game/sgame/bots/bot_nav.*` now has a dedicated CTF item-role scope
  alongside the existing FFA and TDM scopes. CTF mode uses
  `sg_bot_ctf_item_roles`, applies positive item-role priority as a candidate
  score boost, and records `ctf_item_role_*` plus `last_ctf_item_role_*`
  route-status fields.
- `src/game/sgame/bots/bot_brain.cpp` emits compact CTF nav-policy status
  chunks before the larger item-role diagnostic line so selected-goal and
  latest role/item markers survive status-capture limits.
- `src/server/main.c` reserves `sv_bot_frame_command_smoke 47` as a four-bot
  CTF proof path, sets `deathmatch 1`, `g_gametype 5`, enables
  `sg_bot_ctf_item_roles`, emits `ctf_item_roles=1`, and resets the cvar after
  the smoke lane.
- `tools/bot_scenarios/run_bot_scenarios.py` promotes the `ctf_item_roles`
  scenario with marker gates for CTF readiness, objective item-role policy
  selection, nav scoring boosts, selected pickup goals, and latest role/item
  metadata.
- `tools/bot_scenarios/test_run_bot_scenarios.py` covers the scenario catalog,
  command construction, marker gates, optional `ctf_item_role_counters`, and
  same-marker optional-field merging. `tools/bot_scenarios/README.md`
  documents mode `47`.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed: 45 tests.
- `meson compile -C builddir-win`
  - Passed. Ninja printed the existing non-fatal `premature end of file; recovering` warning.
- `meson compile -C builddir-win sgame_x86_64`
  - Passed after the compact CTF status split. Ninja printed the same non-fatal recovery warning.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
  - Passed and refreshed `.install` with packaged `maps/mm-rage.aas`.
- `python tools\bot_scenarios\run_bot_scenarios.py --install-dir .install --scenario ctf_item_roles --json-out .tmp\bot_scenarios\ctf_item_roles_report.json`
  - Passed from `.tmp\bot_scenarios\20260621T175557Z`.
  - Key metrics: `frames=246`, `commands=246`, `route_commands=246`,
    `route_failures=0`, `item_goal_assignments=17`, `pass=1`.
  - CTF item-role counters: `ctf_item_role_evaluations=758`,
    `ctf_item_role_selections=758`, `ctf_item_role_score_boosts=758`,
    `ctf_item_role_selected_goals=17`, `ctf_item_role_invalid_skips=0`,
    `last_ctf_item_role_mode=3`, `last_ctf_item_role_role=2`,
    `last_ctf_item_role_lane=2`, `last_ctf_item_role_item_role=4`,
    `last_ctf_item_role_item=4`, `last_ctf_item_role_score_boost=155`.
- `python tools\bot_scenarios\run_bot_scenarios.py --install-dir .install --scenario implemented --json-out .tmp\bot_scenarios\implemented_ctf_item_roles_report.json`
  - Passed from `.tmp\bot_scenarios\20260621T175605Z`.
  - Summary: 52 passed, 0 failed, 0 timed out, 0 errored, 0 pending.
