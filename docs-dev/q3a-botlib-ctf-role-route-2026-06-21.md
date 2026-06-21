# Q3A BotLib CTF Role Route Bridge

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off live CTF role-route bridge behind
`sg_bot_ctf_role_route`. The existing objective match-policy helpers already
classify Capture the Flag roles and lanes; the new command-side bridge consumes
that policy as a timed route-goal owner so CTF attack, defense, and midfield
role decisions can affect live navigation without turning on broader combat or
objective automation.

No upstream Q3A, Gladiator, BSPC, idTech3, or `q2proto/` source files were
imported or modified. The implementation is WORR-native and uses the existing
`bot_objectives.*`, `bot_brain.*`, and scenario harness surfaces.

## Implementation

- Added `sg_bot_ctf_role_route`, defaulting to `0`.
- Added a `ctf_role` timed route-goal owner kind, preserving the previous
  `team_role` owner for the TDM route proof.
- Added CTF-specific frame-command counters:
  - `ctf_role_route_requests`
  - `ctf_role_route_policy_selections`
  - `ctf_role_route_activations`
  - `ctf_role_route_refreshes`
  - `ctf_role_route_owner_deferrals`
  - `ctf_role_route_route_requests`
  - `ctf_role_route_route_deferrals`
  - `ctf_role_route_expirations`
  - `ctf_role_route_invalid_skips`
  - `last_ctf_role_route_*`
- Reused the existing match role/lane direction mapping so attack roles move
  forward, defense/own-base roles move backward, and midfield/support-response
  lanes move laterally.
- Restricted the new bridge to `BotObjectiveMatchMode::CaptureTheFlag` so TDM
  and CTF route proof counters remain isolated.

## Scenario Promotion

`ctf_role_route` is now an implemented scenario using smoke mode `35`. The
promoted checks validate:

- Four active bots in a CTF team-mode match.
- Objective match-policy selection for Capture the Flag.
- Live timed route-goal ownership through the new `ctf_role` owner kind.
- Nonzero CTF role-route requests, policy selections, activations, route
  requests, and last role/lane/distance metadata.

The scenario proves policy consumption at the route-command boundary. It does
not yet claim full autonomous flag attack/defense behavior, carrier support,
dropped-flag response, base-return priorities, or combat ownership for CTF
roles.

## Validation

Validation completed:

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed
  with 32 tests.
- `meson compile -C builddir-win` passed. Ninja emitted the recurring
  `premature end of file; recovering` warning after generation.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
  passed, refreshed `.install/`, packed 93 assets, and completed the q2aas
  package audit.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario team_objective --timeout 90 --base-port 28000 --format text --json-out .tmp\bot_scenarios\team_objective_report.json`
  passed after the CTF route-mode split, preserving the legacy
  `team_objective` smoke's `g_gametype 1` setup.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario ctf_role_route --timeout 90 --base-port 28100 --format text --json-out .tmp\bot_scenarios\ctf_role_route_report.json`
  passed. The focused run observed 246 frames, 246 command frames, 246 route
  commands, zero route failures, active CTF team-mode readiness, and nonzero
  `ctf_role_route_*` owner counters.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md`
  passed with 27 total scenarios, 27 passed, 0 failed, 0 timed out, 0 errored,
  and 0 pending.
