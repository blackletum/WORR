# Q3A BotLib Coop Leader Route Scenario Gate

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This pass promotes the existing coop leader-route owner into its own scenario
contract. The previous owner already consumed coop follow, regroup, and
support-combat policy by arming the generic timed route-goal owner with kind
`coop_leader`. The new work makes that behavior visible on the compact coop
command status marker and adds a dedicated `coop_leader_route` smoke row.

This is intentionally a validation and status-surface round, not a new coop
decision tree. It proves that the current coop policy consumer reaches route
ownership under cooperative cvars. No-leader LeadAdvance route ownership is
tracked separately in
`docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`, while
door/elevator progression policy, monster target sharing, resource sharing, and
anti-blocking behavior remain for later coop slices.

## Behavior

- The `q3a_bot_coop_command_status` marker now mirrors leader-route activation,
  refresh, deferral, source-selection, invalid-skip, and last-leader metadata.
- The promoted `coop_leader_route` scenario reuses `sv_bot_frame_command_smoke`
  mode `3` with `deathmatch 0` and `coop 1`.
- The gate requires route-clean frame commands, at least one route command, coop
  readiness, `last_timed_route_goal_kind=3`, leader-route activations and
  refreshes, support-spacing source generation, and a concrete coop intent.

## Code Changes

- `src/game/sgame/bots/bot_brain.cpp`
  - Adds `coop_leader_route_*` and `last_coop_leader_route_*` fields to both
    compact coop command status rows.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Treats leader-route fields as reserved compact coop command metrics.
  - Lets optional-field discovery report the leader-route family from either
    frame-command status or compact coop command status.
  - Adds the implemented `coop_leader_route` scenario.

- `tools/bot_scenarios/test_run_bot_scenarios.py`
  - Covers the new catalog row, cvars, marker checks, promoted fixture, and
    optional compact-status field discovery.

- `tools/bot_scenarios/README.md`
  - Documents the new implemented scenario, optional field family, and promoted
    mode `3` reuse row.

## Validation

Commands run:

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`
- `python tools/bot_scenarios/run_bot_scenarios.py --catalog --format json --json-out .tmp\bot_scenarios\catalog-coop-leader-route.json`
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `ninja -C builddir-win sgame_x86_64.dll`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_leader_route --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 60 --format text --artifact-dir .tmp/bot_scenarios/coop-leader-route-scenario --json-out .tmp\bot_scenarios\coop-leader-route-scenario.json --markdown-out .tmp\bot_scenarios\coop-leader-route-scenario.md`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario implemented --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 120 --format text --artifact-dir .tmp/bot_scenarios/implemented-coop-leader-route --json-out .tmp\bot_scenarios\implemented-latest.json --markdown-out .tmp\bot_scenarios\implemented-latest.md`

Result: passed. Ninja printed the existing `premature end of file; recovering`
warning during build steps. The install refresh repacked `maps/mm-rage.aas` into
`.install/basew/pak0.pkz` with SHA-256
`6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.

Focused `coop_leader_route` smoke result:

- 1 passed, 0 failed, 0 timed out, 0 errored, 0 pending.
- `frames=17`, `commands=17`, `route_commands=17`, `route_failures=0`.
- Compact coop command counters reported
  `coop_leader_route_activations=16`,
  `coop_leader_route_refreshes=14`,
  `coop_leader_route_spacing_sources=16`,
  `last_coop_leader_route_intent=5`, and
  `last_coop_leader_route_intent_name=support_combat`.

Full implemented suite result: 18 passed, 0 failed, 0 timed out, 0 errored, and
0 pending.
