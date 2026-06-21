# Q3A BotLib Coop Progress Wait Command Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds a default-off command-owner proof for coop progression waits.
The existing coop objective policy already knows how to classify a
`WaitForLeader` intent when progression waiting is requested. The bot brain now
exposes that request through `sg_bot_coop_progress_wait` and consumes valid
WaitForLeader policy by holding the bot in place and facing the selected leader.

This is deliberately narrower than full campaign progression behavior. It
proves the command path can consume coop wait policy without changing normal
coop movement, while door, elevator, trigger, and map-authored wait-point
detection remain follow-up work.

## Behavior

- `sg_bot_coop_progress_wait` defaults to `0`.
- When enabled, frame policy evaluation builds coop context with progression
  waiting requested.
- Valid WaitForLeader policy applies a stop-and-face command:
  - `forwardMove` and `sideMove` are cleared.
  - Jump and crouch movement-state buttons are cleared.
  - The command angles face the selected leader when the leader is alive.
- Existing action buttons and pending weapon/inventory dispatch remain owned by
  the action layer.

## Code Changes

- `bot_brain.cpp`
  - Adds `BotFrameObjectivePolicyResult` so the frame command path can reuse the
    coop policy selected during policy evaluation.
  - Registers the default-off `sg_bot_coop_progress_wait` bridge cvar.
  - Applies coop WaitForLeader command ownership after movement-state and
    recovery command handling.
  - Emits `coop_progress_wait_*` and `last_coop_progress_wait_*`
    counters through the compact `q3a_bot_coop_command_status` row.
  - Emits compact frame-command and action/detail proof rows before the archival
    verbose diagnostics so smoke-gate counters are not hidden by console line
    length limits.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers coop progress-wait counters as a known optional status family.
  - Adds the promoted `coop_progress_wait` scenario, reusing frame-command smoke
    mode `3` under `deathmatch 0`, `coop 1`, and
    `sg_bot_coop_progress_wait 1`.
  - Makes frame-status parsing and marker checks tolerant of split proof rows by
    parsing embedded status markers and resolving each required marker metric
    from the newest row that contains that metric.

- `tools/bot_scenarios/test_run_bot_scenarios.py`
  - Covers the new catalog row, extra cvar ordering, required status metrics,
    required objective marker metrics, and optional-field discovery.
  - Adds parser regression coverage for embedded frame-command proof markers and
    field-wise marker metric lookup across split status rows.

- `tools/bot_scenarios/README.md`
  - Lists `coop_progress_wait` in the implemented short-smoke catalog.

## Follow-Up

This round does not infer progression waits from map triggers, doors, elevators,
monster waves, or player-blocking geometry. The next coop slices should connect
map-specific mover/trigger/progression evidence to the command owner instead of
requiring the proof cvar.

## Validation

Commands run:

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_policy_trace_and_readiness_promotions_use_expected_smoke_rows tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_optional_field_discovery_parses_new_status_families`
- `python tools/bot_scenarios/run_bot_scenarios.py --catalog --format text --scenario coop_progress_wait`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_status_parsing_splits_embedded_frame_command_proof_markers tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_marker_checks_use_latest_row_containing_metric tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_marker_metric_parsing_splits_embedded_status_markers tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_status_parsing_prefers_positive_command_proof_over_cleanup_status`
- In-memory Python syntax compile for `tools/bot_scenarios/run_bot_scenarios.py`
  and `tools/bot_scenarios/test_run_bot_scenarios.py`.
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `ninja -C builddir-win sgame_x86_64.dll`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_progress_wait --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 60 --format text --artifact-dir .tmp/bot_scenarios/coop-progress-wait-owner`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario map_change_repeat,engage_enemy,switch_weapons,health_armor_pickup,item_timer_fairness_signals --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 120 --format text --json-out .tmp/bot_scenarios/failing_round_report.json --artifact-dir .tmp/bot_scenarios/failing-round-check`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario implemented --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 120 --format text --json-out .tmp/bot_scenarios/latest_report.json --artifact-dir .tmp/bot_scenarios/implemented-latest`
- `python -B tools/bot_scenarios/test_run_bot_scenarios.py`
- `git diff --check`

Results:

- Focused parser and catalog tests passed.
- Object and DLL rebuilds passed; Ninja reported the pre-existing
  `premature end of file; recovering` warning.
- `.install` refresh passed and packaged `maps/mm-rage.aas` into
  `basew/pak0.pkz` with SHA-256
  `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
- `coop_progress_wait` passed with 17 progress-wait requests, 17 policy waits,
  17 wait commands, `last_coop_progress_wait_intent=2`, and
  `last_coop_progress_wait_intent_name=wait_for_leader`.
- The targeted five-scenario regression pass completed 5 passed, 0 failed.
- The implemented scenario suite completed 16 passed, 0 failed, 0 timeout, 0
  error, and 0 pending.
- The full harness unit suite completed 32 passed, 0 failed.
- `git diff --check` reported no whitespace errors; Git printed expected CRLF
  normalization warnings for touched text/source files.
