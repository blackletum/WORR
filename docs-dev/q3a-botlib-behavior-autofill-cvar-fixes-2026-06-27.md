# Q3A BotLib Behavior, Autofill, And Cvar Fixes - 2026-06-27

Task context: Q3A BotLib parity follow-up under the broader bot feature roadmap.

## Issues Resolved

- Public bot cvars had drifted back to mixed server/game prefixes in active
  server/sgame code and the bot scenario harness. The active surface is now
  `bot_*`, matching Q3/Q2R-style operator expectations. The Q3-style commands
  already existed (`addbot`, `removebot`, `kickbots`, `botlist`,
  `bot_reload_profiles`) and the user docs now describe those names.
- `bot_min_players` did not top up unless the old global bot-enable cvar was
  set. Auto-fill now depends on `bot_min_players` directly: humans and manual
  bots count toward the target, auto-fill bots fill the remaining slots, and
  setting the target to `0` removes only auto-fill bots.
- The BotLib runtime and integrated behavior umbrella defaulted off, so manually
  added bots could exist without receiving route-backed behavior. `bot_enable`
  and `bot_behavior_enable` now default to `1`.
- FFA item-role pickup scoring and threat retreat were not fully owned by the
  behavior umbrella. `bot_behavior_enable` now activates FFA item-role routing
  and threat-retreat/close-threat spacing along with the existing role and match
  policy owners.
- Low-health pickup weighting is now strong enough to beat nearby non-survival
  goals, and smoke mode `72` seeds the staged enemy plus health route so the
  regression proof exercises the same survival route that live bots use.
- Close enemies in front of a bot now create a short away-from-source spacing
  route. Low-health retreats still suppress attack input; close-spacing routes
  keep attack arbitration available so a healthy bot can backpedal or sidestep
  while fighting instead of freezing or pushing into the opponent.

## Notes

The `*_smoke` cvars remain validation hooks, but they now live under the
`bot_*` namespace. They are intentionally not normal player configuration; they
drive unattended proof scenarios. Live behavior no longer derives smoke setup
from `bot_behavior_enable`; smoke-only setup checks the raw smoke cvar so normal
servers do not allocate proof slots.

Mode `72` uses a shortened threat-retreat proof window for deterministic
validation. Live threat retreat keeps the longer gameplay window.

The yaw/pitch flipping regression from the previous pass remains covered by the
command-angle correction: bot desired view angles are normalized/pitch-clamped
and converted relative to `pmove.deltaAngles` before being placed in the
server-authored command.
