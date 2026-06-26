# Q3A BotLib Team Fire Avoidance Bridge

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off live TDM friendly-fire avoidance bridge behind
`sg_bot_team_fire_avoidance`. The existing objective-side friendly-fire policy
already knew how to classify teammate targets and friendly lines of fire; the
new brain-side bridge consumes that policy immediately before action
application so an attack decision can be suppressed before `BUTTON_ATTACK` is
written into the frame `usercmd_t`.

No upstream Q3A, Gladiator, BSPC, idTech3, or `q2proto/` source files were
imported or modified. The implementation is WORR-native and uses the existing
`bot_objectives.*`, `bot_brain.*`, and scenario harness surfaces.

## Implementation

- Added `sg_bot_team_fire_avoidance`, defaulting to `0`.
- Added a small geometric friendly-line check in `bot_brain.cpp`:
  - finds live same-team clients between shooter and target
  - projects teammate position onto the shooter-to-target segment
  - treats teammates inside a 56-unit corridor as line-of-fire blockers
  - confirms the teammate is visible or damage-reachable from the shooter
- Added `Bot_CommandApplyTeamFireAvoidance()` between action decision sampling
  and `BotActions_ApplyDecisionDetailed()`.
- Preserved normal combat selection, weapon scoring, and aim behavior. The
  bridge only rewrites the command-bound action when an enabled policy reports
  `shouldAvoidFire` or disallows the target.
- Added frame-command counters:
  - `team_fire_avoidance_evaluations`
  - `team_fire_avoidance_blocks`
  - `team_fire_avoidance_target_blocks`
  - `team_fire_avoidance_line_blocks`
  - `team_fire_avoidance_clears`
  - `team_fire_avoidance_invalid_skips`
  - `last_team_fire_avoidance_*`
- Added deterministic smoke placement for the proof lane: mode `34` assigns
  four bots to TDM teams, grants combat weapons, and places a friendly between
  bot `0` and an enemy target so the live command bridge has a stable
  line-of-fire case to consume.

## Scenario Promotion

`team_fire_avoidance` is now an implemented scenario using smoke mode `34`.
The promoted checks validate:

- TDM readiness through `q3a_bot_match_readiness_status`.
- Objective match-policy and friendly-fire policy evaluations through
  `q3a_bot_objective_status`.
- Live combat attack decisions through `q3a_bot_action_status`.
- Brain-side attack suppression through `q3a_bot_frame_command_status`
  `team_fire_avoidance_*` counters.
- Aggregate blocked attack evidence, including friendly-line block counters.

The scenario proves policy consumption at the command boundary. It does not yet
claim broad tactical fire discipline, target repositioning, or autonomous
formation behavior in arbitrary TDM/CTF matches.

## Validation

Validation completed for the round:

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed
  with 32 tests.
- `meson compile -C builddir-win` passed. Ninja emitted the recurring
  `premature end of file; recovering` warning after generation.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
  passed, refreshed `.install/`, packed 93 assets, and completed the q2aas
  package audit.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario team_fire_avoidance --timeout 90 --base-port 28000 --format text --json-out .tmp\bot_scenarios\team_fire_avoidance_report.json`
  passed. The focused run observed 246 frames, 187 command frames, 187 route
  commands, zero route failures, live attack decisions, and nonzero
  `team_fire_avoidance_*` suppression counters.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md`
  passed with 26 scenarios passed, 0 failed, 0 timed out, 0 errored, and 0
  pending.
