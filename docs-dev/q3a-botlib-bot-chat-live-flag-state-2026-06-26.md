# Q3A BotLib Bot Chat Live Flag State

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds live `flag_state` bot chat coverage for the default-off
`sg_bot_chat_live_events` pipeline. Mode `87` `bot_chat_live_flag_state`
reuses the CTF pickup/drop/return transition proof and verifies that real CTF
flag state observations drive event id `8` / `flag_state` through the safe
reply and live dispatch path.

The focused scenario requires CTF objective transition evidence, objective
route ownership, `reply_chat_flag_state >= 4`, `live_chat_flag_state >= 4`,
`live_chat_event_taxonomy=11`, and zero dispatch, reply, or live failures. It
intentionally gates aggregate flag-state counters instead of final-event
ordering because the same CTF setup can legitimately produce later
`item_taken` or `enemy_sighted` co-events.

## Implementation

- Added `flag_state` phrase selection and reply/live accounting to the bot chat
  policy status surface.
- Added `Bot_CommandMaybeDispatchLiveFlagStateChat()` so live CTF pickup,
  capture, drop, return, carrier, or dropped-flag observations can submit event
  id `8` when `sg_bot_chat_live_events` is enabled.
- Added `reply_chat_flag_state` and `live_chat_flag_state` status fields to
  `q3a_bot_chat_policy_status`.
- Wired frame-command smoke mode `87` through the CTF objective transition
  seeding path and exposed the `bot_chat_live_flag_state` begin marker.
- Added the `bot_chat_live_flag_state` scenario, reserved-mode metadata,
  README entry, and scenario harness unit coverage.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.

## Validation

- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`: 53 passed.
- `meson compile -C builddir-win sgame_x86_64 copy_sgame_dll`: passed.
- `meson compile -C builddir-win worr_ded_x86_64 sgame_x86_64 copy_sgame_dll`:
  passed.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64 copy_sgame_dll`:
  passed; this rebuilt the server-frame smoke marker owner after the launcher
  and game DLL build left the new mode `87` begin marker stale.
- `python tools/refresh_install.py --build-dir builddir-win`: passed; `.install`
  was refreshed and `basew/pak0.pkz` was repacked.
- Focused `bot_chat_live_flag_state`: passed from
  `.tmp\bot_scenarios\20260626Tflagstate3\20260626T144136Z`.
- Full `implemented` suite: `.tmp\bot_scenarios\20260626Timplemented-flagstate-fixed\20260626T144511Z`,
  95 passed, 0 failed, 0 timeout, 0 error, 0 pending.

## Completion Impact

- Scenario catalog: 95 implemented rows, 0 pending rows.
- Highest bot frame-command smoke mode: `87`.
- M5 chat/personality now has live spawn, route-ready, enemy-sighted,
  low-health, item-taken, objective-changed, and flag-state event coverage
  behind `sg_bot_chat_live_events`.
- Remaining chat breadth before user-facing support docs: match-result,
  item-denied, and blocked events.
