# Q3A BotLib Team Item Role Selection Proof

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds the first live item-route consumer for the existing
FFA/TDM/CTF match item-role policy helper. The new default-off
`sg_bot_team_item_roles` cvar lets TDM match role/lane policy shape item
pickup-goal scoring in `bot_nav`, without making that behavior the default
team strategy yet.

The proof deliberately stays narrow. It does not add a new autonomous team
planner, lane-aware map objective database, or broad CTF attack/defend
ownership. It proves that a live route-goal scan can consult the match
item-role policy, boost role-appropriate candidate scores, and report the
final role-shaped pickup goal through status markers.

## Implementation

- Added default-off `sg_bot_team_item_roles` in the nav item-goal selection
  path.
- When enabled, `BotNavFindPickupGoal` builds the current match policy and
  evaluates `BotObjectives_EvaluateItemRolePolicy` for active pickup
  candidates.
- Valid item-role policies add a positive score boost to the normal
  `BotItems_Evaluate` candidate score before distance penalty selection.
- `BotNavRouteStatus` now exposes `team_item_role_*` counters and
  `last_team_item_role_*` metadata through `q3a_bot_nav_policy_status`,
  including mode, role, lane, item category, item role, score boost, entity,
  item id, and final selected score.
- Added server smoke mode `33`, which starts four bots under TDM rules and
  enables `sg_bot_team_item_roles`.
- Promoted `team_item_roles` in `tools/bot_scenarios/` with marker gates for
  mode `33`, TDM readiness, objective item-role helper selection, nav-policy
  item-role evaluation, score boosting, selected pickup goals, and final
  role/category metadata.

No Q3A behavior source was imported or copied for this slice. The work is
WORR-native policy consumption and validation around previously added match
policy helpers.

## Validation

Focused command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --install-dir .install --scenario team_item_roles --json-out .tmp\bot_scenarios\team_item_roles_report.json
```

Focused result:

- `pass=1`
- `frames=246`
- `commands=246`
- `route_commands=246`
- `route_failures=0`
- `item_goal_assignments=19`
- `team_item_role_evaluations=846`
- `team_item_role_selections=846`
- `team_item_role_score_boosts=846`
- `team_item_role_selected_goals=19`
- `team_item_role_invalid_skips=0`
- `last_team_item_role_mode=2`
- `last_team_item_role_item_role=1`
- `last_team_item_role_score_boost=140`

Additional local validation in this round:

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
- `python tools\bot_scenarios\run_bot_scenarios.py --install-dir .install --scenario implemented --json-out .tmp\bot_scenarios\latest_report.json`

Full implemented-suite result:

- 25 passed
- 0 failed
- 0 timed out
- 0 errored
- 0 pending
