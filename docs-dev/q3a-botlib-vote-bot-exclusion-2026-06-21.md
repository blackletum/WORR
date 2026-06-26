# Q3A BotLib Vote Bot-Exclusion Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

## Summary

This round adds a WORR-owned vote match-flow proof for bot participants. Bots
can now be present as live FFA players without contributing to
`level.pop.num_voting_clients`, and bot-origin vote launch/cast paths are
explicitly rejected at the game command layer.

The promoted scenario is `vote_bot_exclusion`. It runs from the refreshed
install through `sv_bot_vote_smoke 2`, spawns two bot-only FFA participants,
prints `q3a_bot_vote_status`, attempts a harmless bot-origin `random 2` vote
through the same `Commands::TryLaunchVote()` helper used by UI vote launch
paths, requires `q3a_bot_vote_launch reason=bot_blocked`, and verifies cleanup
leaves zero bots and no active vote.

## Implementation

- Added `BOT_VOTE_STATUS_API_V1` under `inc/shared/` and exposed it through
  `G_GetExtension()`.
- Added `BotVote_PrintStatus()`, `BotVote_TryLaunchFirstBotVote()`, and
  `BotVote_ResetStatus()` in `g_svcmds.cpp`.
- Added bot-origin guards to `TryLaunchVote()`, `CallVote()`, and `Vote()` so
  future bot client-command dispatch work cannot accidentally make bots call
  or cast votes.
- Added `sv_bot_vote_smoke` in the dedicated server. The smoke configures
  deathmatch FFA, leaves voting enabled, spawns two bots, captures vote status,
  attempts the bot-origin vote launch, captures post-rejection status, removes
  all bots, and prints final cleanup status.
- Added `vote_bot_exclusion` to `tools/bot_scenarios/run_bot_scenarios.py`
  with hard marker gates for bot population, zero voting clients, bot-blocked
  launch rejection, and final no-active-vote cleanup.
- Updated scenario tests, scenario README, the Phase 7 plan checklist, the
  roadmap, and the credits/provenance ledger.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 36
  tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  passed and refreshed the staged runtime.
- Focused `vote_bot_exclusion` passed from `.install`.
- Full implemented scenario suite passed from `.install`: 41 passed, 0 failed,
  0 timed out, 0 errored, and 0 pending from
  `.tmp\bot_scenarios\20260621T123039Z`.

## Remaining Work

`FR-07-T01` remains open for MyMap queue and nextmap transition scenarios.
Scoreboard classification plus intermission/reconnect cleanup also remain open
under the Phase 7 match-tools checklist.
