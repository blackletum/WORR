# Q3A BotLib MyMap Bot Queue Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`, `DV-07-T06`

## Summary

This round promotes the Phase 7 MyMap queue slice from a pending match-flow
gap into an implemented smoke proof. The new `mymap_queue_bot_request`
scenario drives a bot-attributed MyMap request through native WORR map queue
state, proves that both `playQueue` and `myMapQueue` receive the request, then
consumes the queued map and verifies final cleanup leaves no bots or queued map
behind.

## Implementation

- Added `BOT_MYMAP_STATUS_API_V1` in `inc/shared/bot_mymap_status.h` and
  exported it through `G_GetExtension`, giving the server smoke a narrow
  game-side boundary for printing status, queueing the first bot's MyMap
  request, consuming the queued map, resetting proof state, and clearing queue
  state between runs.
- Added game-side `q3a_bot_mymap_status`, `q3a_bot_mymap_queue`, and
  `q3a_bot_mymap_consume` markers. The status line records bot/human/player
  counts, MyMap cvars, queue sizes, front queued map/social IDs, and the latest
  queue/consume outcomes.
- Added `sv_bot_mymap_smoke 2` in the dedicated server. The smoke configures
  one bot-only FFA participant, enables MyMap, assigns a deterministic test
  social ID when the fake client does not already have one, validates
  `Commands::CheckMyMapAllowed`, queues the active map, consumes the queue, and
  removes all bots before quitting.
- Added `mymap_queue_bot_request` to `tools/bot_scenarios/run_bot_scenarios.py`
  with semantic marker checks for MyMap enablement, bot presence, social
  attribution, successful queueing, successful consume, empty queues after
  consume, and final zero-bot cleanup. The harness also reports optional
  `mymap_match_flow_signals` fields so future mapdb/nextmap work can observe
  richer status before those fields become promotion gates.

## Map Pool Note

The focused proof runs from the refreshed staged install, where the current
runtime can load packaged assets but `MapPool::LoadMapPool` still reads
`basew/mapdb.json` directly through `std::ifstream`. Because there is no loose
staged `basew/mapdb.json`, the smoke helper seeds a temporary active-map
`MapEntry` only when the map pool is empty. The status surface reports that as
`last_queue_map_seeded=1`.

That makes the proof explicit rather than pretending to validate production
mapdb discovery. This round proves MyMap gate/social attribution, native
`MapSystem` queue insertion, `ConsumeQueuedMap` cleanup, scenario harness
parsing, and bot cleanup. Full mapdb validation, map-vote coverage, and
nextmap transition behavior remain follow-up slices of `FR-07-T01`.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py` passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 37 tests.
- `meson compile -C builddir-win sgame_x86_64` passed.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed.
- Focused `mymap_queue_bot_request` passed from `.tmp\bot_scenarios\20260621T125839Z`.
- Full implemented scenario suite passed from `.tmp\bot_scenarios\20260621T125848Z` with 42 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

No new upstream Q3A, BSPC, Gladiator, idTech3, or q2proto source files were
imported or modified in this round.
