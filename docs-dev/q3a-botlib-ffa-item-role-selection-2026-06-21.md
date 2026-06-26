# Q3A BotLib FFA Item Role Selection Proof

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off WORR-native pickup selection bridge for Free For
All item-role policy. The new `sg_bot_ffa_item_roles` cvar lets the existing
FFA match item-role helper shape live pickup-goal scoring in `bot_nav` before
the normal item utility and distance penalty select the final route goal.

The scope is intentionally narrow: this is a proof bridge for live FFA item
route scoring, not broad autonomous role behavior. No Q3A, Gladiator, BSPC,
idTech3, or `q2proto/` source files were imported or modified.

## Implementation

- `src/game/sgame/bots/bot_nav.*` now separates item-role policy accounting
  into FFA and team scopes. FFA mode uses `sg_bot_ffa_item_roles` and records
  `ffa_item_role_*` plus `last_ffa_item_role_*` status fields, while the
  existing `sg_bot_team_item_roles` TDM bridge keeps its existing counters.
- `BotNavFindPickupGoal` evaluates the existing match policy when either FFA
  or team item-role scoring is enabled. In FFA mode, valid item-role policy
  priority is applied as a positive candidate score boost before
  `BotItems_Evaluate()`.
- `src/game/sgame/bots/bot_brain.cpp` prints the new FFA nav-policy status
  fields through `q3a_bot_nav_policy_status`. It also emits compact
  item-role and interaction nav-policy lines before the full verbose
  diagnostic so promoted scenario gates are not vulnerable to console-line
  truncation.
- `src/server/main.c` reserves `sv_bot_frame_command_smoke 46` as a four-bot
  FFA proof path, sets `deathmatch 1`, `g_gametype 1`, enables
  `sg_bot_ffa_item_roles`, emits `ffa_item_roles=1`, and resets the cvar after
  the smoke lane.
- `tools/bot_scenarios/run_bot_scenarios.py` promotes the `ffa_item_roles`
  scenario with marker gates for FFA readiness, objective item-role policy
  selection, nav scoring boosts, selected pickup goals, and latest role/item
  metadata.
- `tools/bot_scenarios/test_run_bot_scenarios.py` covers the scenario catalog,
  command construction, marker gates, and optional `ffa_item_role_counters`
  parser family. `tools/bot_scenarios/README.md` documents mode `46`.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed: 45 tests.
- `meson compile -C builddir-win`
  - Passed after rerun. The first attempt timed out before reporting a compile
    error; the successful rerun printed a non-fatal Ninja recovery warning.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
  - Passed and refreshed `.install` with packaged `maps/mm-rage.aas`.
- `python tools\bot_scenarios\run_bot_scenarios.py --install-dir .install --scenario ffa_item_roles --json-out .tmp\bot_scenarios\ffa_item_roles_report.json`
  - Passed from `.tmp\bot_scenarios\20260621T173656Z`.
  - Key metrics: `frames=246`, `commands=246`, `route_commands=246`,
    `route_failures=0`, `item_goal_assignments=24`, `pass=1`.
  - FFA item-role counters: `ffa_item_role_evaluations=1074`,
    `ffa_item_role_selections=1074`, `ffa_item_role_score_boosts=1074`,
    `ffa_item_role_selected_goals=24`, `ffa_item_role_invalid_skips=0`,
    `last_ffa_item_role_mode=1`, `last_ffa_item_role_item_role=1`,
    `last_ffa_item_role_score_boost=140`.
- `python tools\bot_scenarios\run_bot_scenarios.py --install-dir .install --scenario implemented --json-out .tmp\bot_scenarios\implemented_ffa_item_roles_after_compact_report.json`
  - Passed from `.tmp\bot_scenarios\20260621T173703Z`.
  - Summary: 51 passed, 0 failed, 0 timed out, 0 errored, 0 pending.
