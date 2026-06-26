# Q3A BotLib Coop Resource Share Route Selection

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off coop resource-sharing route-selection proof behind
`sg_bot_coop_resource_share`. When enabled, item route-goal selection consumes
the existing WORR-native coop/resource objective policy and can mark pickup
candidates as reserved for a teammate before item scoring accepts them.

No Q3A, Gladiator, BSPC, idTech3, or q2proto source was imported or modified for
this slice. The work is native WORR behavior, status, and scenario harness code.

## Runtime Behavior

- `BotNavFindPickupGoal` now builds match and coop policy once per scan while
  `sg_bot_coop_resource_share` is enabled.
- Each active pickup candidate is evaluated through
  `BotObjectives_BuildResourceContext(...)` and
  `BotObjectives_EvaluateResourcePolicy(...)` before `BotItems_Evaluate(...)`.
- Reserve-for-teammate resource policy marks the item context as reserved, so
  item scoring defers the candidate and increments `item_reserved_deferrals`.
- `Bot_CommandSmokeScenarioMode()` maps the cvar to dedicated server smoke mode
  `28`.
- While the resource-share proof is active, the competing coop leader-route and
  LeadAdvance route owners are suppressed so the smoke exercises item route-goal
  selection instead of a timed coop route owner.
- Server smoke setup targets two coop bots for mode `28` and resets
  `sg_bot_coop_resource_share` during smoke cvar cleanup.

## Scenario Contract

`tools/bot_scenarios/run_bot_scenarios.py` now includes
`coop_resource_share`, a mode `28` implemented scenario with:

- `deathmatch 0`
- `coop 1`
- `sg_bot_coop_resource_share 1`

The scenario requires:

- source smoke pass with route commands and zero route failures,
- coop readiness with two playing bots,
- match readiness in non-deathmatch coop mode,
- objective policy evidence for `team_objective_coop_policy_resource_share`,
- reserve-for-teammate evidence through `team_objective_resource_policy_reserve`,
- item-scoring evidence through `item_reserved_deferrals`.

Compact and proof `q3a_bot_action_status` rows now expose
`item_reserved_deferrals` so the promoted scenario can validate this behavior
without depending on oversized verbose diagnostics.

## Files Touched

- `src/game/sgame/bots/bot_nav.cpp`
- `src/game/sgame/bots/bot_brain.cpp`
- `src/server/main.c`
- `tools/bot_scenarios/run_bot_scenarios.py`
- `tools/bot_scenarios/test_run_bot_scenarios.py`
- `tools/bot_scenarios/README.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `python tools/bot_scenarios/test_run_bot_scenarios.py` - 32 tests passed.
- `ninja -C builddir-win sgame_x86_64.dll worr_ded_engine_x86_64.dll worr_ded_x86_64.exe` - passed.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas` - passed, including q2aas archive audit.
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_resource_share --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 60 --format text --artifact-dir .tmp/bot_scenarios/coop-resource-share --json-out .tmp\bot_scenarios\coop-resource-share.json --markdown-out .tmp\bot_scenarios\coop-resource-share.md` - passed.
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario implemented --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 120 --format text --artifact-dir .tmp/bot_scenarios/implemented-coop-resource-share --json-out .tmp\bot_scenarios\implemented-latest.json --markdown-out .tmp\bot_scenarios\implemented-latest.md` - passed with 20 passed, 0 failed, 0 timeout, 0 error, and 0 pending.

