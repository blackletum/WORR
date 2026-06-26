# Q3A BotLib Bot Chat Live Objective Changed

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round completes live `objective_changed` bot chat coverage for the
default-off `sg_bot_chat_live_events` pipeline. Mode `86`
`bot_chat_live_objective_changed` reuses the CTF pickup/drop/return transition
proof and verifies that real CTF flag pickup, death-drop, and dropped-flag
return hooks drive event id `7` / `objective_changed` through the safe reply
and live dispatch path.

The focused scenario requires objective transition evidence, route ownership,
`reply_chat_objective_changed >= 4`, `live_chat_objective_changed >= 4`,
`live_chat_event_taxonomy=11`, and zero dispatch, reply, or live failures. It
intentionally allows natural `enemy_sighted` and `item_taken` co-events because
the CTF transition setup can legitimately expose visible enemy players or flag
pickup item observations after the objective callouts.

## Implementation

- Wired frame-command smoke mode `86` through
  `Bot_CommandPrepareCtfObjectiveTransitionsSmoke()` so the live chat objective
  row receives the same real CTF transition seeding as mode `76`.
- Hardened `bot_chat_live_objective_changed` scenario checks to gate the
  objective counters and zero-failure dispatch contract instead of assuming
  `objective_changed` must remain the final chat event after all live gameplay
  processing.
- Relaxed item-taken and enemy-sighted checks for this scenario to allow natural
  CTF co-events while still requiring objective-changed evidence.
- Updated scenario catalog unit coverage to match the final-event-ordering
  contract.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.

## Validation

- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`: 53 passed.
- `meson compile -C builddir-win sgame_x86_64 copy_sgame_dll`: passed.
- `python tools/refresh_install.py --build-dir builddir-win`: passed; `.install`
  was refreshed and `basew/pak0.pkz` was repacked.
- Focused `bot_chat_live_objective_changed`: passed from
  `.tmp\bot_scenarios\20260626T140601Z`.
- Full `implemented` suite: `.tmp\bot_scenarios\20260626T140621Z`, 94 passed,
  0 failed, 0 timeout, 0 error, 0 pending.

## Completion Impact

- Scenario catalog: 94 implemented rows, 0 pending rows.
- Highest bot frame-command smoke mode: `86`.
- M5 chat/personality now has live spawn, route-ready, enemy-sighted,
  low-health, item-taken, and objective-changed event coverage behind
  `sg_bot_chat_live_events`.
- Remaining chat breadth before user-facing support docs: match-result,
  flag-state, item-denied, and blocked events.
