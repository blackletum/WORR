# Q3A BotLib Intermission Bot Cleanup Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

## Summary

This round promotes the Phase 7 intermission/reconnect cleanup slice into an
implemented smoke proof. The new `intermission_bot_cleanup` scenario drives two
bot-only FFA participants into WORR's native `BeginIntermission()` path, verifies
that `MoveClientToIntermission()` freezes and moves both bots into freecam
non-solid state, removes the bot clients, and proves the game-side sorted-client
state is cleared before the map exits intermission.

## Implementation

- Added `BOT_INTERMISSION_STATUS_API_V1` in
  `inc/shared/bot_intermission_status.h` and exported it through
  `G_GetExtension`, giving the dedicated server smoke a narrow game-side
  boundary for intermission status printing, native intermission entry, and
  proof-state reset.
- Added game-side `q3a_bot_intermission_status` and
  `q3a_bot_intermission_begin` markers. The status line records bot/human/player
  counts, connected and sorted-client counts, intermission/queued/post-exit
  state, current/change-map target state, frozen/freecam/non-solid bot counts,
  and the latest begin-intermission outcome.
- Added `sv_bot_intermission_smoke 2` in the dedicated server. The smoke
  configures a bot-only FFA setup, queues `InterOne` and `InterTwo`, waits until
  both fake clients have materialized, records pre-intermission status, begins
  intermission through the game extension, verifies both bots are in
  intermission-owned client state, removes all bots, and verifies cleanup leaves
  zero bots, zero connected clients, and zero sorted clients.
- Added `intermission_bot_cleanup` to
  `tools/bot_scenarios/run_bot_scenarios.py` with semantic marker checks for
  accepted bot add requests, bot-only standings before intermission, successful
  native intermission entry, frozen/freecam/non-solid bot state, retained
  current-map transition target, and final zero-bot/sorted-client cleanup. The
  harness also reports optional `intermission_match_flow_signals` fields for
  future nextmap transition work.

## Cleanup Note

The smoke intentionally leaves the map in active intermission after bot removal.
That makes the cleanup proof stricter: fake-client disconnect must remove bot
clients and sorted rows without relying on a map restart to wipe the state. The
observed final status also reports `ready_to_exit=1`, which is expected for a
bot-only deathmatch intermission with no humans left to acknowledge the exit.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py` passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 39 tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed.
- Focused `intermission_bot_cleanup` passed from `.tmp\bot_scenarios\20260621T134839Z`.
- Full implemented scenario suite passed from `.tmp\bot_scenarios\20260621T134846Z` with 44 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

No new upstream Q3A, BSPC, Gladiator, idTech3, or q2proto source files were
imported or modified in this round.
