# Q3A BotLib Bot Chat Live Blocked

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds live `blocked` bot chat coverage for the default-off
`sg_bot_chat_live_events` pipeline. Mode `88` `bot_chat_live_blocked` reuses
the existing blocked rocketjump travel-type goal proof, keeps rocketjumping
disabled, and verifies that a real route failure drives event id `10` /
`blocked` through the safe reply and live dispatch path.

The focused scenario requires one FFA profile bot, a blocked travel-type goal
request, zero successful commands, route failures, `reply_chat_blocked >= 1`,
`live_chat_blocked >= 1`, `live_chat_event_taxonomy=11`, and zero dispatch,
reply, or live failures. The frame-command captured status line currently
truncates before some late travel-type tail fields, so the scenario hard-gates
the durable request/resolution/assignment/failure counters and the chat status
surface rather than the truncated tail.

## Implementation

- Added `blocked` phrase selection and reply/live accounting to the bot chat
  policy status surface.
- Added `Bot_CommandMaybeDispatchLiveBlockedChat()` so live route failures can
  submit event id `10` when `sg_bot_chat_live_events` is enabled.
- Added `reply_chat_blocked` and `live_chat_blocked` status fields to
  `q3a_bot_chat_policy_status`.
- Wired frame-command smoke mode `88` through the blocked travel-type goal path
  and exposed the `bot_chat_live_blocked` begin marker.
- Added the `bot_chat_live_blocked` scenario, reserved-mode metadata, README
  entry, and scenario harness unit coverage.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.

## Validation

- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`: 53 passed.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64 copy_sgame_dll`: passed.
- `python tools/refresh_install.py --build-dir builddir-win`: passed; `.install`
  was refreshed and `basew/pak0.pkz` was repacked.
- Focused `bot_chat_live_blocked`: passed from
  `.tmp\bot_scenarios\20260626Tblocked-fixed\20260626T151437Z`.
- Full `implemented` suite: `.tmp\bot_scenarios\20260626Timplemented-blocked\20260626T151446Z`,
  96 passed, 0 failed, 0 timeout, 0 error, 0 pending.

## Completion Impact

- Scenario catalog: 96 implemented rows, 0 pending rows.
- Highest bot frame-command smoke mode: `88`.
- M5 chat/personality now has live spawn, route-ready, enemy-sighted,
  low-health, item-taken, objective-changed, flag-state, and blocked event
  coverage behind `sg_bot_chat_live_events`.
- Remaining chat breadth before user-facing support docs: match-result and
  item-denied events.
