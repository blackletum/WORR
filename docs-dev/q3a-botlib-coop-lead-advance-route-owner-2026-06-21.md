# Q3A Botlib AAS Port: Coop LeadAdvance Route Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds the first default-off command owner for no-leader coop
`LeadAdvance` policy. When `sg_bot_coop_lead_advance` is enabled and the coop
objective helper selects the `LeadAdvance` intent, the brain now arms a short
`coop_lead_advance` timed route-goal owner that moves the bot forward along its
current advance direction.

The feature is intentionally narrow: it proves that LeadAdvance can leave the
helper/status layer and reach route ownership without changing default bot
behavior or importing upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source.

## Runtime Behavior

- `sg_bot_coop_lead_advance` defaults to `0` and is reset after bot
  frame-command smoke runs.
- A valid coop `LeadAdvance` policy creates a short timed route source behind
  the bot, then routes toward the bot's current forward/advance direction.
- Existing high-priority timed route owners such as nuke retreat, teleporter
  escape, and coop leader routing are not overwritten.
- Repeated LeadAdvance policy frames refresh the active LeadAdvance owner rather
  than creating unrelated route state.
- The compact `q3a_bot_coop_command_status` marker now exposes
  `coop_lead_advance_*` and `last_coop_lead_advance_*` counters for request,
  activation, refresh, route-request, deferral, expiration, invalid-skip, and
  last-intent diagnostics.

## Scenario Coverage

`tools/bot_scenarios/run_bot_scenarios.py` now promotes `coop_lead_advance` as
an implemented scenario with dedicated smoke mode `27`. The row runs one bot
under `deathmatch 0`, `coop 1`, and `sg_bot_coop_lead_advance 1`, then gates:

- coop readiness and non-deathmatch match state,
- reserved mode `27` target-bot setup,
- objective intent `LeadAdvance`,
- timed route-goal kind `coop_lead_advance`,
- LeadAdvance request, policy, activation, and route-request counters.

The optional-field analyzer also recognizes the new compact LeadAdvance counter
family so historical reports can summarize the feature without requiring every
older run to contain the fields.

## Files Touched

- `src/game/sgame/bots/bot_brain.cpp`
  - Adds `BotTimedRouteGoalKind::CoopLeadAdvance`.
  - Adds `sg_bot_coop_lead_advance` sampling and LeadAdvance activation logic.
  - Adds compact coop command-owner counters for route ownership diagnostics.
- `src/server/main.c`
  - Reserves smoke mode `27` for the one-bot coop LeadAdvance proof.
  - Resets `sg_bot_coop_lead_advance` after scenario execution.
- `tools/bot_scenarios/run_bot_scenarios.py`
  - Adds the `coop_lead_advance` implemented scenario row and optional counter
    family.
- `tools/bot_scenarios/test_run_bot_scenarios.py`
  - Extends parser, catalog, command construction, and optional-field coverage
    for the new scenario.
- `tools/bot_scenarios/README.md`
  - Documents mode `27` and the new compact counter family.
- `docs-dev/plans/q3a-botlib-aas-port.md`
  - Marks Phase 7 follow/wait/lead command proof work complete and updates the
    outstanding scenario totals.
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
  - Records the task progress against bot command ownership and validation
    scenario tracking.
- `docs-dev/q3a-botlib-aas-credits.md`
  - Records that this round was WORR-native replacement/adapter work.

## Validation

Commands run:

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`
- `ninja -C builddir-win sgame_x86_64.dll worr_ded_engine_x86_64.dll`
- `ninja -C builddir-win worr_ded_x86_64.exe`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_lead_advance --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 60 --format text --artifact-dir .tmp/bot_scenarios/coop-lead-advance --json-out .tmp\bot_scenarios\coop-lead-advance.json --markdown-out .tmp\bot_scenarios\coop-lead-advance.md`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario implemented --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 120 --format text --artifact-dir .tmp/bot_scenarios/implemented-coop-lead-advance --json-out .tmp\bot_scenarios\implemented-latest.json --markdown-out .tmp\bot_scenarios\implemented-latest.md`

Result: passed. Ninja printed the existing `premature end of file; recovering`
warning during build steps. The install refresh repacked `maps/mm-rage.aas` into
`.install/basew/pak0.pkz` with SHA-256
`6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.

Focused `coop_lead_advance` smoke result:

- 1 passed, 0 failed, 0 timed out, 0 errored, 0 pending.
- `frames=60`, `commands=60`, `route_commands=60`, `route_failures=0`.
- Compact coop command counters reported
  `coop_lead_advance_requests=60`,
  `coop_lead_advance_policy_leads=60`,
  `coop_lead_advance_activations=60`,
  `coop_lead_advance_refreshes=59`,
  `coop_lead_advance_route_requests=60`,
  `last_coop_lead_advance_intent=4`, and
  `last_coop_lead_advance_intent_name=lead_advance`.

Full implemented suite result: 19 passed, 0 failed, 0 timed out, 0 errored, and
0 pending.
