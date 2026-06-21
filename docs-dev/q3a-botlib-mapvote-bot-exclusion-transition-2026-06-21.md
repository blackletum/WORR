# Q3A BotLib Map-Vote Bot Exclusion Transition Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

## Summary

This round promotes the Phase 7 map-vote selector slice into an implemented
smoke proof. The new `mapvote_bot_exclusion_transition` scenario stages a
bot-only FFA match, starts the native map selector against the current staged
map, proves bot selector ballots are ignored before they can count, finalizes
the selector through the normal map-change path, observes the dedicated server
reload, and verifies retained fake clients are cleaned up after reload.

## Implementation

- Added `BOT_MAPVOTE_STATUS_API_V1` in
  `inc/shared/bot_mapvote_status.h` and exported it through `G_GetExtension`,
  giving the dedicated server smoke a narrow game-side boundary for selector
  status printing, proof-state reset, selector start, guarded bot-vote
  attempts, and finalization.
- Added game-side `q3a_bot_mapvote_status`,
  `q3a_bot_mapvote_begin`, `q3a_bot_mapvote_bot_vote`, and
  `q3a_bot_mapvote_finalize` markers. The status line records bot/human/player
  counts, selector activity, candidate names, vote counts, bot/human vote
  attribution, `changeMap` state, and retained latest bot-vote/finalize
  outcomes.
- Hardened `MapSelector_CastVote()` so bot clients return before selector vote
  storage or broadcast work. That preserves the human-only selector invariant
  even if later fake-client command dispatch can issue vote commands.
- Added `sv_bot_mapvote_smoke 2` in the dedicated server. The smoke configures
  a two-bot FFA setup, enables voting and the map selector, seeds the current
  staged map only when the runtime map pool is empty, starts a deterministic
  one-candidate selector, attempts the first bot ballot, finalizes the selector,
  waits for the `sv.spawncount` reload edge, prints post-reload status, removes
  retained fake clients, and verifies final zero-bot completion.
- Added `mapvote_bot_exclusion_transition` to
  `tools/bot_scenarios/run_bot_scenarios.py` with semantic marker checks for
  accepted bot add requests, active selector state, blocked bot ballot,
  zero counted bot votes, selected-current-map finalization, observed reload,
  retained finalize status, and final cleanup. The harness also reports
  optional `mapvote_match_flow_signals` fields for future match-flow expansion.

## Transition Note

The smoke deliberately finalizes to the current staged `mm-rage` map when the
runtime map pool is empty. That keeps packaged-install validation deterministic
while still exercising the real selector-finalize and `ExitLevel(true)` reload
path. The post-reload status retains the latest finalize target separately from
the live post-transition `change_map_set=0` state.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py` passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 41 tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed.
- Focused `mapvote_bot_exclusion_transition` passed from `.tmp\bot_scenarios\20260621T142951Z`.
- Full implemented scenario suite passed from `.tmp\bot_scenarios\20260621T142957Z` with 46 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

No new upstream Q3A, BSPC, Gladiator, idTech3, or q2proto source files were
imported or modified in this round.
