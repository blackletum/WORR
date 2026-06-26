# Q3A BotLib Coop Anti-Blocking Command Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off coop anti-blocking command-owner proof behind
`sg_bot_coop_anti_blocking`. When enabled, a bot with a valid close coop leader
can briefly own its movement command, backpedal, sidestep, clear movement-state
jump/crouch buttons, and keep looking toward the leader.

No Q3A, Gladiator, BSPC, idTech3, or q2proto source was imported or modified for
this slice. The work is native WORR behavior, status, and scenario harness code.

## Runtime Behavior

- `Bot_CommandApplyCoopAntiBlocking(...)` consumes the existing
  `BotObjectiveCoopPolicy` after route/recovery command setup and before the
  WaitForLeader stop-and-face bridge.
- The bridge is gated by `sg_bot_coop_anti_blocking` and only acts in coop with a
  valid, alive, non-self leader inside the close-leader threshold.
- Valid anti-blocking ownership writes a small reverse move and deterministic
  left/right strafe into the current `usercmd_t`, clears jump/crouch movement
  state buttons, and turns the bot toward the leader.
- Compact coop command status exposes `coop_anti_block_*` and
  `last_coop_anti_block_*` counters for request, close-policy, command,
  invalid-skip, leader, intent, distance, and movement evidence.
- `Bot_CommandSmokeScenarioMode()` maps the cvar to dedicated server smoke mode
  `29`.
- Server smoke setup targets two coop bots for mode `29` and resets
  `sg_bot_coop_anti_blocking` during smoke cvar cleanup.

## Scenario Contract

`tools/bot_scenarios/run_bot_scenarios.py` now includes
`coop_anti_blocking`, a mode `29` implemented scenario with:

- `deathmatch 0`
- `coop 1`
- `sg_bot_coop_anti_blocking 1`

The scenario requires:

- source smoke pass with route commands and zero route failures,
- coop readiness with two playing bots,
- match readiness in non-deathmatch coop mode,
- anti-blocking requests and close-policy hits,
- anti-blocking command ownership,
- support-combat coop intent evidence on the last anti-block command.

Optional-field discovery now recognizes the compact anti-blocking counters so
future reports can surface this evidence even when a scenario does not hard-gate
it.

## Files Touched

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
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_anti_blocking --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 60 --format text --artifact-dir .tmp/bot_scenarios/coop-anti-blocking --json-out .tmp\bot_scenarios\coop-anti-blocking.json --markdown-out .tmp\bot_scenarios\coop-anti-blocking.md` - passed.
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario implemented --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 120 --format text --artifact-dir .tmp/bot_scenarios/implemented-coop-anti-blocking --json-out .tmp\bot_scenarios\implemented-latest.json --markdown-out .tmp\bot_scenarios\implemented-latest.md` - passed with 21 passed, 0 failed, 0 timeout, 0 error, and 0 pending.
