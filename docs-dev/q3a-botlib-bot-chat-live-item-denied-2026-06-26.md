# Q3A BotLib Bot Chat Live Item-Denied Round

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds the next live bot chat event slice: `item_denied`. The event now comes from real TDM deny-enemy resource policy evidence instead of the smoke-only chat event gate. When `sg_bot_chat_live_events` is enabled and the team resource-denial policy records denial pressure, bots can dispatch event id `5` / `item_denied` through the same conservative chat path used by the earlier live chat events.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` now tracks reply/live `item_denied` counters, exposes event id `5`, adds personality phrase variants for `item_denied`, and dispatches the live event from `BotNav_GetRouteStatus().teamResourceDenialPolicyDenies` while preserving the existing chat rate, duplicate, and consumer checks.
- `src/game/sgame/g_local.hpp` and `src/game/sgame/gameplay/g_svcmds.cpp` expose `reply_chat_item_denied` and `live_chat_item_denied` in `q3a_bot_chat_policy_status`.
- `src/server/main.c` reserves smoke mode `89` as `bot_chat_live_item_denied`, reusing the four-bot TDM team resource-denial setup and emitting a dedicated begin marker.
- `tools/bot_scenarios/run_bot_scenarios.py` adds the `bot_chat_live_item_denied` scenario with strict gates for TDM readiness, `sg_bot_team_resource_denial`, deny-enemy resource counters, item assignments, event id/name `5` / `item_denied`, chat taxonomy size, and zero dispatch/reply/live failures.
- `tools/bot_scenarios/test_run_bot_scenarios.py` adds mode `89` parser, catalog, raw-marker, command-building, and marker-evaluation coverage.
- `tools/bot_scenarios/README.md` records the new mode and required cvars.

No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or `q2proto/` source files were imported or modified.

## Validation

- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`: 53 passed.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64 copy_sgame_dll`: passed.
- `python tools/refresh_install.py --build-dir builddir-win`: passed and refreshed `.install/` with current Windows binaries and packaged `basew` game data.
- Focused scenario: `python tools/bot_scenarios/run_bot_scenarios.py --scenario bot_chat_live_item_denied --artifact-dir .tmp/bot_scenarios/20260626Titem-denied --format text` passed from `.tmp\bot_scenarios\20260626Titem-denied\20260626T154429Z`.
- Focused evidence included `frames=246`, `commands=246`, `route_commands=246`, `route_failures=0`, `item_goal_assignments=16`, `team_resource_denial_policy_denies=112`, `team_resource_denial_selected_goals=16`, `reply_chat_item_denied=4`, `live_chat_item_denied=4`, `live_chat_event_taxonomy=11`, and `last_live_chat_event_name=item_denied`.
- Full catalog: `python tools/bot_scenarios/run_bot_scenarios.py --scenario implemented --artifact-dir .tmp/bot_scenarios/20260626Timplemented-item-denied-json-file --format json > .tmp/bot_scenarios/20260626Titem-denied-report.json` passed 97 of 97 implemented rows from `.tmp\bot_scenarios\20260626Timplemented-item-denied-json-file\20260626T154954Z`, with 0 failed rows, 0 timeouts, 0 errors, and 0 pending rows.

## Completion Impact

- Scenario catalog: 97 implemented rows, 0 pending rows.
- Highest reserved bot frame-command smoke mode: `89`.
- M5 chat/personality event breadth now covers live spawn, route-ready, enemy-sighted, low-health, item-taken, objective-changed, flag-state, blocked, and item-denied events.
- Remaining chat-event breadth is now concentrated on match-result/victory-defeat behavior and user-facing documentation readiness.
