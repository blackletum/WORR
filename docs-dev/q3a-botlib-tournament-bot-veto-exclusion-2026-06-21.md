# Q3A BotLib Tournament Bot Veto Exclusion Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T02`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

## Summary

This round adds a tournament veto proof for bot clients. A deterministic smoke
helper can now configure a minimal tournament veto state, assign the first
playing bot the active home-side identity, attempt a veto pick, and prove the
bot is rejected without mutating pick or ban state.

The promoted scenario is `tournament_bot_veto_exclusion`. It runs through
`sv_bot_tournament_smoke 2`, spawns one bot-only FFA participant, configures a
best-of-three tournament map pool, prints `q3a_bot_tournament_status`, attempts
the active-side pick through `Tournament_HandleVetoAction()`, requires
`q3a_bot_tournament_veto reason=bot_blocked`, and verifies `picks=0` and
`bans=0` after cleanup.

## Implementation

- Added `BOT_TOURNAMENT_STATUS_API_V1` under `inc/shared/` and exposed it
  through `G_GetExtension()`.
- Hardened `CanActorVeto()` so bot clients are rejected before social-ID,
  captain, or active-side checks. This prevents a fake client from performing a
  tournament veto even if its session identity matches the active side.
- Added `BotTournament_SetupBotVetoState()`,
  `BotTournament_TryFirstBotVetoPick()`, `BotTournament_PrintStatus()`, and
  `BotTournament_ResetStatus()`. The markers record tournament activation,
  active-side identity, map pool size, pick/ban counts before and after the
  attempted veto, and the bot-specific rejection reason.
- Added `sv_bot_tournament_smoke` in the dedicated server. The smoke configures
  FFA/tournament cvars, spawns one bot, asks the game module to prepare the
  tournament proof state, captures pre-attempt status, runs the guarded pick
  attempt, captures post-attempt and cleanup status, and exits in mode `2`.
- Added `tournament_bot_veto_exclusion` to the scenario harness with hard marker
  gates for bot-only setup, active tournament/veto state, bot active-side
  identity, `bot_blocked` denial, zero pick/ban mutation, final cleanup, and
  optional `tournament_match_flow_signals`.

## Validation

- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -k
  "tournament_bot_veto_exclusion or admin_bot_privilege_audit"` passed 2
  selected tests.
- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -k
  "tournament_bot_veto_exclusion"` passed 1 selected test after the scenario
  task IDs were aligned to `FR-07-T02`.
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  passed and refreshed `.install/`.
- Focused `tournament_bot_veto_exclusion` passed from
  `.tmp\bot_scenarios\20260621T152536Z`.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md`
  passed with 48 passed, 0 failed, 0 timed out, 0 errored, and 0 pending from
  `.tmp\bot_scenarios\20260621T153725Z`.

No upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
imported or modified in this round.
