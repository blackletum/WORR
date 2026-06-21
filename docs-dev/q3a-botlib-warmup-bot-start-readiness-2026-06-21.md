# Q3A BotLib Warmup Bot-Start Readiness

Date: 2026-06-21

Tasks: `FR-04-T06`, `DV-03-T05`, `DV-07-T06`

## Summary

This round closes the first Phase 7 warmup behavior proof by adding a WORR-owned
server-game warmup status API, a deterministic server smoke, and a promoted
`warmup_bot_start_readiness` scenario row.

The proof focuses on the existing ready-up contract in `CheckReady()`: bots do
not count as ready humans, but a bot-only match may start when
`match_start_no_humans` is enabled and `minplayers` is satisfied. The smoke
configures a two-bot FFA warmup, validates the bot-only ready-up path through
`q3a_bot_warmup_status`, removes all bots, and validates cleanup.

## Implementation Notes

- Added `BOT_WARMUP_STATUS_API_V1` in `inc/shared/bot_warmup_status.h` and
  exposed it through the existing game extension path.
- Added `BotWarmup_PrintStatus()` in sgame. It reports bot/human/playing counts,
  ready-human and ready-bot counts, `minplayers` state, warmup cvar state,
  `bot_only_start`, `can_start`, and current match/warmup state names.
- Added `sv_bot_warmup_smoke`. Mode `2` auto-quits after validation, mirroring
  the existing deterministic smoke style.
- Added the marker-only `warmup_bot_start_readiness` scenario. It gates the
  begin cvars, accepted bot add requests, live two-bot warmup status, bot-only
  start eligibility, cleanup status, and final zero-bot count.
- Extended scenario optional-field discovery so `q3a_bot_warmup_status` appears
  under the team/match readiness signal family in reports.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 35 tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64` passed.
- Focused `warmup_bot_start_readiness` passed from `.install`, reporting live
  `bots=2`, `playing=2`, `minplayers_met=1`, `bot_only_start=1`,
  `can_start=1`, `pass=1`, followed by cleanup `bots=0`, `playing=0`,
  `can_start=1`, `pass=1`, and `final_count=0`.
- Full implemented scenario suite passed from `.install`: 40 passed, 0 failed,
  0 timed out, 0 errored, and 0 pending.

## Source / Provenance

No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
imported or modified. This is WORR-native status API, server smoke, scenario
harness, and documentation work.
