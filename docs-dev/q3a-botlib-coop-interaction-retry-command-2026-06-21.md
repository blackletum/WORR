# Q3A BotLib Coop Interaction Retry Command Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds a default-off coop command-owner proof for route-detected
interactions. The native navigation layer already detects door, button,
platform, train, trigger, and mover interaction candidates while building AAS
route steering. The bot brain can now opt into that evidence through
`sg_bot_coop_interaction_retry` and request the existing wait/use retry window
for cooperative frame commands.

This is intentionally a bridge rather than full campaign scripting. It proves
that route interaction detection can reach `usercmd_t` ownership in coop without
changing default bot movement or replacing later map-specific progression
logic.

## Behavior

- `sg_bot_coop_interaction_retry` defaults to `0`.
- When enabled in cooperative mode, the frame command path asks navigation to
  activate a retry window for the current route's detected interaction.
- Activated retry windows reuse the existing recovery command path:
  - wait actions clear forward and side movement;
  - use actions press `BUTTON_USE`;
  - wait/use actions can do both in the same command frame.
- Per-client ownership bits attribute command-owner counters only to the
  cvar-gated coop request path. Existing automatic route interaction retry
  behavior remains available and does not increment the new coop counters.

## Code Changes

- `bot_nav.hpp` / `bot_nav.cpp`
  - Exposes `BotNav_RequestInteractionRetry(...)` as a public wrapper around
    the existing route interaction retry activation path.
  - Carries the detected interaction kind through `BotNavRecoveryMove`.

- `bot_brain.cpp`
  - Registers the default-off `sg_bot_coop_interaction_retry` bridge cvar.
  - Requests route interaction retry after successful route steering when the
    current coop policy is valid and coop mode is active.
  - Tracks per-client coop interaction retry ownership so only cvar-gated
    retry windows increment `coop_interaction_retry_*` counters.
  - Emits `coop_interaction_retry_*` and `last_coop_interaction_retry_*`
    counters through the compact `q3a_bot_coop_command_status` row.

- `src/server/main.c`
  - Resets both coop command-owner proof cvars after smoke phases so repeated
    smoke runs stay isolated.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers coop interaction retry counters as a known optional field family.
  - Adds the promoted `coop_interaction_retry` scenario using smoke mode `12`
    under `deathmatch 0`, `coop 1`, and `sg_bot_coop_interaction_retry 1`.

- `tools/bot_scenarios/test_run_bot_scenarios.py`
  - Covers the catalog row, extra cvar ordering, required marker metrics,
    synthetic marker fixture checks, and optional-field discovery for the new
    counter family.

- `tools/bot_scenarios/README.md`
  - Lists the promoted scenario and optional counter family.

## Follow-Up

This round does not decide when a coop campaign bot should wait for a human,
which player owns a shared trigger, or how to coordinate multi-player door and
elevator timing. The next coop slices should consume map/objective state and
monster/progression evidence above this command-owner bridge.

## Validation

Commands run:

- `python -B -m unittest tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_policy_trace_and_readiness_promotions_use_expected_smoke_rows tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_trace_and_coop_marker_checks_accept_promoted_fixture_rows tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_optional_field_discovery_parses_new_status_families`
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_nav.cpp.obj`
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `ninja -C builddir-win sgame_x86_64.dll`
- `ninja -C builddir-win worr_ded_x86_64.exe`
- `ninja -C builddir-win worr_ded_engine_x86_64.dll`
- `python -B tools/bot_scenarios/run_bot_scenarios.py --catalog --format text --scenario coop_interaction_retry`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_interaction_retry --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 60 --format text --artifact-dir .tmp/bot_scenarios/coop-interaction-retry-owner`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario implemented --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 120 --format text --json-out .tmp/bot_scenarios/latest_report.json --artifact-dir .tmp/bot_scenarios/implemented-latest`
- `python -B tools/bot_scenarios/test_run_bot_scenarios.py`

Results:

- Focused catalog/marker/optional-field unit tests passed.
- Bot nav and brain object rebuilds passed.
- `sgame_x86_64.dll`, `worr_ded_x86_64.exe`, and
  `worr_ded_engine_x86_64.dll` rebuilt; Ninja reported the pre-existing
  `premature end of file; recovering` warning.
- `.install` refresh passed and packaged `maps/mm-rage.aas` into
  `basew/pak0.pkz` with SHA-256
  `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
- `coop_interaction_retry` passed with 8 retry requests, 1 ownership
  activation, 8 retry command frames, `last_coop_interaction_retry_action=3`,
  and `last_coop_interaction_retry_kind=3`.
- The implemented scenario suite completed 17 passed, 0 failed, 0 timeout, 0
  error, and 0 pending.
- The full harness unit suite completed 32 passed, 0 failed.
