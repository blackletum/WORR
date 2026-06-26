# Q3A BotLib Team Role Route Proof

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off live team-role route owner for the WORR bot
brain. The new `sg_bot_team_role_route` cvar lets existing FFA/TDM/CTF
match-policy output consume the generic timed route-goal owner, proving that a
selected role/lane can own real route commands without making that behavior
the default autonomous team strategy yet.

The first promoted proof uses a TDM-style four-bot smoke on `mm-rage` because
that map is already staged and does not require a CTF asset. The role/lane
mapping is intentionally conservative:

- attack lanes move forward from the bot's current facing;
- defense lanes fall back;
- midfield/support lanes sidestep.

That keeps the proof about command ownership and status evidence rather than
claiming final map-aware team tactics.

## Implementation

- Added `sg_bot_team_role_route` as a default-off brain cvar.
- Added `BotTimedRouteGoalKind::TeamRole` to the generic timed route-goal
  owner.
- Added `team_role_route_*` and `last_team_role_route_*` frame-command status
  counters for requests, policy selections, activations, refreshes, deferrals,
  route requests, expirations, invalid skips, mode, role, lane, priority, and
  goal-distance evidence.
- Added server smoke mode `32`, which starts four bots under TDM rules and
  enables the new team-role route proof.
- Promoted `team_role_route` in `tools/bot_scenarios/` with marker gates for
  mode `32`, TDM readiness, TDM match-policy selection, timed route owner kind
  `5`, route-owner activation, route requests, and last role/lane metadata.

No Q3A behavior source was imported or copied for this slice. The work is
WORR-native command ownership and validation around previously added match
policy helpers.

## Validation

Focused command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario team_role_route --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 90 --format text --artifact-dir .tmp\bot_scenarios\team-role-route
```

Focused result:

- `pass=1`
- `frames=246`
- `commands=246`
- `route_commands=246`
- `route_failures=0`
- `target=4`
- `tdm_pass=1`
- `team_mode=1`
- `last_timed_route_goal_kind=5`
- `team_role_route_requests>=1`
- `team_role_route_policy_selections>=1`
- `team_role_route_activations>=1`
- `team_role_route_route_requests>=1`
- `last_team_role_route_mode=2`
- `last_team_role_route_lane>=1`
- `last_team_role_route_goal_distance_sq>0`

Additional local validation in this round:

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 120 --format text --artifact-dir .tmp\bot_scenarios\implemented --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md`

Full implemented-suite result:

- 24 passed
- 0 failed
- 0 timed out
- 0 errored
- 0 pending
