# Q3A BotLib Queued Nextmap Transition Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

## Summary

This round promotes the Phase 7 queued nextmap transition slice into an
implemented smoke proof. The new `queued_nextmap_transition` scenario stages a
bot-only FFA participant, queues the current staged map through the MyMap queue,
drives the native queued-map transition path, observes the dedicated server
reload, and verifies that retained fake clients and queue state are cleaned up
after the reload.

## Implementation

- Added `BOT_NEXTMAP_STATUS_API_V1` in
  `inc/shared/bot_nextmap_status.h` and exported it through `G_GetExtension`,
  giving the dedicated server smoke a narrow game-side boundary for nextmap
  status printing, proof-state reset, and queued-map transition execution.
- Added game-side `q3a_bot_nextmap_status` and
  `q3a_bot_nextmap_transition` markers. The status line records bot/human/player
  counts, connected clients, current map, front queued-map state, queue sizes,
  `changeMap` state, and the latest queued transition outcome. The transition
  marker records play/MyMap queue sizes before and after the transition,
  override flag propagation, target/current map names, consumption status, and
  the `queued_exit` reason for successful proof runs.
- Added `sv_bot_nextmap_smoke 2` in the dedicated server. The smoke configures a
  one-bot FFA setup, enables MyMap, queues the active staged map through the
  existing MyMap helper, executes the new game-side queued transition hook,
  waits for `sv.spawncount` to change after the `gamemap` reload, prints
  post-reload status, removes retained fake clients, and verifies final
  zero-bot completion.
- Added `queued_nextmap_transition` to
  `tools/bot_scenarios/run_bot_scenarios.py` with semantic marker checks for
  accepted bot add requests, bot-attributed queue insertion, play/MyMap queue
  consumption, successful queued transition, observed reload, post-reload queue
  emptiness, retained transition status, and final zero-bot cleanup. The harness
  also reports optional `nextmap_match_flow_signals` fields for future map-vote
  and match-flow expansion.

## Transition Note

The smoke deliberately queues the current staged `mm-rage` map when the runtime
map pool is empty. That keeps the packaged-install proof deterministic while
still exercising the real queued `gamemap` reload path. `ExitLevel(true)` clears
`level.changeMap` as part of the normal transition, so the retained status
records the target map separately from the post-transition `change_map_set=0`
state.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py` passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 40 tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed.
- Focused `queued_nextmap_transition` passed from `.tmp\bot_scenarios\20260621T140550Z`.
- Full implemented scenario suite passed from `.tmp\bot_scenarios\20260621T140557Z` with 45 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

No new upstream Q3A, BSPC, Gladiator, idTech3, or q2proto source files were
imported or modified in this round.
