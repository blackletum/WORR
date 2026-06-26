# Q3A BotLib Bot Chat Live Match Result Event - 2026-06-26

Task IDs: `FR-04-T07`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promoted the final reserved live chat event family into the WORR bot
chat pipeline: match-result chat. The existing live event taxonomy already
reserved event id `11` as `victory_defeat`; this slice added the runtime
trigger, phrase bank, status counters, smoke mode, and scenario validation that
prove an actual match/intermission state can emit that event through the same
safe reply path as the earlier live chat events.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` now exposes
  `BotChatPolicy_MatchResultEventId()` for event id `11`, records
  `reply_chat_match_result` and `live_chat_match_result`, and adds a
  personality-aware reply phrase bank for `victory_defeat`.
- The live match-result trigger observes real match-flow state:
  `level.intermission.time`, `level.intermission.queued`, or
  `level.matchState == MatchState::Ended`. It then dispatches through
  `Bot_CommandMaybeDispatchChatReplyEvent()` with the existing cooldown,
  duplicate suppression, phrase selection, and public/team audience controls.
- `src/game/sgame/gameplay/g_svcmds.cpp` and `src/game/sgame/g_local.hpp`
  expose the new reply/live counters in `q3a_bot_chat_policy_status`.
- `src/server/main.c` adds reserved smoke mode `90`
  `bot_chat_live_match_result`. The mode enables `sg_bot_allow_chat 1` and
  `sg_bot_chat_live_events 1`, stages four profile-backed TDM bots, starts the
  native intermission path through the existing bot intermission extension API,
  waits a short post-intermission window, and then captures frame-command,
  intermission, and chat-policy markers before cleanup.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` now reserve and document mode `90`.

## Validation

- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  - `53 passed`
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64 copy_sgame_dll`
  - Passed
- `python tools/refresh_install.py --build-dir builddir-win`
  - Passed and refreshed `.install/`
- Focused scenario:
  - Command: `python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --scenario bot_chat_live_match_result --artifact-dir .tmp\bot_scenarios\20260626Tmatch-result --format both`
  - Artifact: `.tmp\bot_scenarios\20260626Tmatch-result\20260626T182046Z`
  - Result: `1 passed`, `0 failed`, `0 timeout`, `0 error`, `0 pending`
  - Key markers: `reply_chat_match_result=4`,
    `live_chat_match_result=4`, `last_reply_chat_event=11`,
    `last_live_chat_event=11`, `last_live_chat_event_name=victory_defeat`,
    `intermission=1`, `intermission_bots=4`, `pm_freeze_bots=4`, and zero
    dispatch, reply, or live failures.
- Full implemented suite:
  - Command: `python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --scenario implemented --artifact-dir .tmp\bot_scenarios\20260626Timplemented-match-result --format both`
  - Artifact: `.tmp\bot_scenarios\20260626Timplemented-match-result\20260626T182111Z`
  - Result: `98 passed`, `0 failed`, `0 timeout`, `0 error`, `0 pending`

## Follow-Up

- Add outcome-aware phrasing once the chat layer needs to distinguish winner,
  loser, team result, and tied/aborted match states.
- Keep the public user-facing chat docs pending until the live event families
  and default operator cvar story are stable enough to document as supported
  behavior.
