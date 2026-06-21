# Q3A BotLib Tournament Replay Reset Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T02`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

## Summary

This round adds a deterministic tournament replay reset proof. The server smoke
can now seed a completed best-of-three tournament history, prove an invalid
replay request is rejected without mutating completed-series state, then replay
game 2 and verify the tournament history, wins, series-complete flag, and target
map are rewound coherently.

The promoted scenario is `tournament_replay_reset`. It runs through
`sv_bot_tournament_smoke 3`, prints replay setup/status markers, attempts an
out-of-range game `99` replay, requires `reason=range_error` with
`preserved=1`, then replays game `2` and requires `reset_applied=1`,
`games_played=1`, one retained winner/map/id, and an open series.

## Implementation

- Hardened `Tournament_ReplayGame()` so replaying a completed series no longer
  refreshes tournament state through the generic match setup path before replay
  history is inspected. This preserves the completed history long enough for the
  replay rewind to operate on it.
- Added a pre-mutation replay-map guard. Missing replay targets now report
  `Replay map is missing.` before wins, match history, or map-change state are
  touched.
- Extended `BOT_TOURNAMENT_STATUS_API_V1` with replay setup and replay attempt
  helpers, exposed them through `G_GetExtension()`, and added game-side replay
  markers for seeded history, invalid-request preservation, valid-request
  rewind results, target map, win totals, and retained match history counts.
- Added `sv_bot_tournament_smoke 3` in the dedicated server. Mode `3` does not
  need bot participants; it sets up the deterministic tournament state, runs the
  invalid replay and valid replay probes, prints status after each step, resets
  tournament cvars, and exits when complete.
- Added `tournament_replay_reset` to the scenario harness with hard marker gates
  for completed best-of-three setup, invalid range rejection with state
  preservation, game-2 replay success, truncated match history, reopened series
  state, and final cleanup.

## Validation

- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -k
  "tournament_replay_reset or tournament_bot_veto_exclusion"` passed 2 selected
  tests.
- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py` passed 44
  tests.
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  passed and refreshed `.install/`.
- Focused `tournament_replay_reset` passed from
  `.tmp\bot_scenarios\20260621T154924Z`.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md`
  passed with 49 passed, 0 failed, 0 timed out, 0 errored, and 0 pending from
  `.tmp\bot_scenarios\20260621T155255Z`.

No upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
imported or modified in this round.
