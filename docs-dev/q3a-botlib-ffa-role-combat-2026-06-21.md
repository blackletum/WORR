# Q3A BotLib FFA Role Combat - 2026-06-21

Task IDs: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off Free For All combat owner behind
`sg_bot_ffa_role_combat`. The bridge consumes the existing WORR-native FFA
match role, lane, and engage policy metadata and lets that policy own a live
attack decision when the bot has visible, shootable enemy facts.

The promoted proof is `ffa_role_combat`, server smoke mode `48`. It is scoped
as a smoke-level ownership proof, not a broad autonomous FFA combat AI pass.

## Implementation

- Added `sg_bot_ffa_role_combat` and a new mode `48` server smoke path for a
  four-bot FFA proof.
- Added `Bot_CommandApplyFfaRoleCombat()` in the brain-owned frame-command
  path. It requires FFA mode, scoring participation, a selected role/lane,
  engage intent, and a visible shootable target before returning an attack
  decision with the `ffa_role_combat_engage` reason.
- Reused the same target-fact and target-adoption path as the existing TDM and
  CTF role-combat bridges so the new FFA owner feeds normal attack-button
  application rather than a separate command path.
- Added `ffa_role_combat_*` and `last_ffa_role_combat_*` fields to
  `q3a_bot_frame_command_status`, including request, target-selection,
  attack-decision, deferral, visible/shootable, role, lane, and target-client
  evidence.
- Added a `ffa_role_combat` harness scenario, mode reservation, marker gates,
  optional status family, unit-test fixtures, and README catalog entries.
- Added compact early proof rows for nav-policy and role-combat evidence so
  promoted scenario checks do not depend on later verbose status text that can
  be truncated by console capture limits.
- Restored the shared smoke peer placement helper to the previous stable offset
  placement behavior after a broader placement experiment destabilized older
  focused smokes.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests.
- `meson compile -C builddir-win sgame_x86_64`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
- Focused `ffa_role_combat` passed from `.tmp\bot_scenarios\20260621T184033Z`.
- Focused regression batch passed for `engage_enemy`, `team_role_combat`,
  `team_role_combat_avoidance`, `ffa_item_roles`, `ctf_item_roles`,
  `team_item_roles`, `team_fire_avoidance`, and `coop_door_elevator` from
  `.tmp\bot_scenarios\20260621T184941Z`.
- Focused `trace_checked_corner_cutting` passed from
  `.tmp\bot_scenarios\20260621T185247Z`.
- Full implemented scenario suite passed from
  `.tmp\bot_scenarios\20260621T185255Z` with 53 passed, 0 failed, 0 timed out,
  0 errored, and 0 pending.

The full build and `.install` refresh were rerun after the full scenario suite.
`q2proto/` was not modified. No upstream Q3A, Gladiator, BSPC, idTech3, or
q2proto source files were imported or modified for this update.
