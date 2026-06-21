# Q3A BotLib Team Resource Denial - 2026-06-21

Task IDs: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off TDM resource-denial pickup scorer behind
`sg_bot_team_resource_denial`. The route scanner now feeds contestable weapons,
powerups, tech, and utility pickups through the existing
`BotObjectiveResourcePolicy` helper as enemy-contested resources. When the
policy intent is `deny_enemy`, the candidate receives a score boost and the
selected goal records compact nav-policy telemetry.

The promoted proof is `team_resource_denial`, server smoke mode `50`. It is a
live pickup-routing bridge for the shared resource policy, not a complete item
economy model.

## Implementation

- Added `sg_bot_team_resource_denial` and reserved mode `50` as a four-bot TDM
  smoke with `deathmatch 1` and `g_gametype 3`.
- Added `BotNavApplyTeamResourceDenialPolicy()` in the pickup-goal scan. It
  only evaluates contestable TDM categories, builds an enemy-contested resource
  context, and boosts candidates whose resource intent is `DenyEnemy`.
- Added compact `team_resource_denial_*` and
  `last_team_resource_denial_*` nav-policy status fields for evaluations,
  deny selections, boosts, selected goals, invalid skips, selected
  mode/role/lane/category/intent, item entity, item id, and final score.
- Promoted the `team_resource_denial` scenario catalog row with marker gates
  for TDM readiness, objective resource-policy deny-enemy selections, nav
  scoring boosts, selected goals, zero invalid skips, and deny-enemy intent.
- Extended optional status discovery, unit-test fixtures, command-builder
  assertions, README scenario lists, and reserved-mode metadata for mode `50`.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests.
- `meson compile -C builddir-win sgame_x86_64`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
- Focused `team_resource_denial` passed from
  `.tmp\bot_scenarios\20260621T200539Z` with 70 resource-denial evaluations,
  70 deny-enemy policy selections, 70 score boosts, 10 selected denial-shaped
  pickup goals, and 0 invalid skips.
- Full implemented scenario suite passed from
  `.tmp\bot_scenarios\20260621T201034Z` with 55 passed, 0 failed, 0 timed out,
  0 errored, and 0 pending.

The `.install` staging root was refreshed after the code rebuild. `q2proto/`
was not modified. No upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source
files were imported or modified for this update.
