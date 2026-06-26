# Q3A BotLib FFA Spawn-Camp Combat Avoidance - 2026-06-21

Task IDs: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off FFA attack-veto proof behind
`sg_bot_ffa_spawn_camp_combat_avoidance`. The bridge composes the existing
FFA role-combat owner with the FFA spawn-camp avoidance source selector: when
role combat selects a visible, shootable opponent and anti-camp policy chooses
that same opponent as the nearby spawn-camp source, the final attack decision is
cleared before `BUTTON_ATTACK` is applied.

The promoted proof is `ffa_spawn_camp_combat_avoidance`, server smoke mode
`49`. It is a precedence and safety proof, not a full autonomous anti-camping
behavior pass.

## Implementation

- Added `sg_bot_ffa_spawn_camp_combat_avoidance` and reserved mode `49` as a
  four-bot FFA smoke that enables both `sg_bot_ffa_role_combat` and
  `sg_bot_ffa_spawn_camp_avoidance`.
- Added `Bot_CommandApplyFfaSpawnCampCombatAvoidance()` in the brain-owned
  frame-command path after FFA role-combat ownership and before team/CTF combat
  bridges. It only evaluates decisions that already press attack.
- Reused the FFA anti-camp source policy and the adopted role-combat target so
  the veto checks live policy output instead of a synthetic target.
- Added `ffa_spawn_camp_combat_avoidance_*` and
  `last_ffa_spawn_camp_combat_avoidance_*` frame-command status for evaluation,
  block, source-block, clear, invalid-skip, target, source, distance, policy,
  and final blocked-state evidence.
- Added the `ffa_spawn_camp_combat_avoidance` scenario catalog row, command
  builder cvars, marker gates, optional counter discovery, unit-test fixtures,
  README entries, and mode-reservation metadata.
- Added a compact `q3a_bot_objective_detail_status` row for the existing
  objective-detail fields that older CTF scenario gates require, preventing
  verbose status-line truncation from hiding those fields.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests.
- `meson compile -C builddir-win sgame_x86_64`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
- Focused `ffa_spawn_camp_combat_avoidance` passed after the final install
  refresh from `.tmp\bot_scenarios\20260621T195014Z`.
- Focused objective-detail truncation regression passed for `team_objective`,
  `ctf_dropped_flag_route`, `ctf_carrier_support_route`, and
  `ctf_base_return_route` from `.tmp\bot_scenarios\20260621T193430Z`.
- Full implemented scenario suite passed from
  `.tmp\bot_scenarios\20260621T193443Z` with 54 passed, 0 failed, 0 timed out,
  0 errored, and 0 pending.

The `.install` staging root was refreshed after the code rebuild. `q2proto/`
was not modified. No upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source
files were imported or modified for this update.
