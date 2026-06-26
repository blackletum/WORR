# Q3A BotLib Scoreboard Bot Classification Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

## Summary

This round promotes the Phase 7 scoreboard classification slice from a pending
match-flow gap into an implemented smoke proof. The new
`scoreboard_bot_classification` scenario drives two bot-only FFA participants
through the same `CalculateRanks()` and `level.sortedClients` standings path
used by the server-side scoreboard, stamps deterministic proof scores, verifies
the leader and runner-up rows remain bot-classified, and then proves cleanup
returns the match to zero clients.

## Implementation

- Added `BOT_SCOREBOARD_STATUS_API_V1` in
  `inc/shared/bot_scoreboard_status.h` and exported it through
  `G_GetExtension`, giving the dedicated server smoke a narrow game-side
  boundary for status printing, proof score application, and proof-state reset.
- Added game-side `q3a_bot_scoreboard_status` and
  `q3a_bot_scoreboard_scores` markers. The status line records bot/human/player
  counts, voting-client counts, sorted-client classification, leader and
  runner-up row metadata, FFA rank ordering, score ordering, and the latest
  score-application outcome.
- Added `sv_bot_scoreboard_smoke 2` in the dedicated server. The smoke
  configures a bot-only FFA setup, queues `ScoreOne` and `ScoreTwo`, waits until
  both fake clients have materialized, records the initial scoreboard status,
  applies deterministic scores of 7 and 3 through the game-side proof hook, and
  removes all bots before quitting.
- Added `scoreboard_bot_classification` to
  `tools/bot_scenarios/run_bot_scenarios.py` with semantic marker checks for
  accepted bot add requests, bot-only sorted standings, no bot voting clients,
  bot-owned leader and runner-up rows, descending score order, ordered FFA
  ranks, proof-score persistence, and final zero-bot cleanup. The harness also
  reports optional `scoreboard_match_flow_signals` fields for future
  intermission and nextmap transition work.

## Score Hook Note

The score helper is intentionally a diagnostic hook, not a gameplay scoring
path. It writes the two bot `resp.score` values directly, then calls
`CalculateRanks()`, because the smoke may run near warmup/countdown boundaries
where normal score setters correctly reject gameplay score changes. That keeps
the proof focused on scoreboard classification and sorted-standings ordering
without weakening the runtime scoring guards.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py` passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 38 tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed.
- Focused `scoreboard_bot_classification` passed from `.tmp\bot_scenarios\20260621T132803Z`.
- Full implemented scenario suite passed from `.tmp\bot_scenarios\20260621T132811Z` with 43 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

No new upstream Q3A, BSPC, Gladiator, idTech3, or q2proto source files were
imported or modified in this round.
