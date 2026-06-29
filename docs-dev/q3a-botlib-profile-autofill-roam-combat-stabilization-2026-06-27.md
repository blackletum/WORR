# Q3A Bot Profile Autofill, Roam, And Combat Stabilization

Date: 2026-06-27

Task IDs: FR-04-T02, FR-04-T03, FR-04-T04, FR-04-T06, FR-04-T15,
FR-04-T16, DV-03-T05, DV-07-T06

## Summary

This round addresses live bot play symptoms found during local inspection:
min-player bots used synthetic names instead of the first-party bot
characters, `bot_min_players` appeared inert while the local game was paused,
and live bots over-focused nearby visible players instead of committing to
route, item, retreat, or combat states.

The source tree now includes a Q3-style `botfiles/bots.txt` manifest for the
first-party profile pack. The profile loader still uses the existing
`botfiles/bots/*_c.c` scan path, but packaging now validates and ships the
manifest alongside the character files so the release payload matches the
expected Q3 botfile shape. Min-player autofill now rotates through loaded
profiles, preferring non-`smoke` profiles such as `bulwark`, `relay`, and
`vanguard`; `bot_profile` remains an explicit override when it names a valid
profile.

The public bot control surface is Q3-style `bot_*`: `bot_enable`,
`bot_min_players`, and `bot_profile`. The remaining `*_smoke` cvars are
developer validation hooks, not operator controls. There is no live
`sv_bot_min_players_smoke` or `sg_bot_min_players_smoke` registration in the
current source; the supported min-player cvar is `bot_min_players`.

The server command surface now includes the Q3-style operational commands
`addbot`, `removebot`, `kickbots`, `botlist`, and `bot_reload_profiles`.

## Root Causes

Min-player profile selection was falling back to generated bot names because
`SV_BotAddAutofill()` only passed a profile id when `bot_profile` was set.
With no explicit cvar value, the add path received `NULL`, built a default
`botN` name, and never attached profile userinfo.

`bot_min_players` could also appear to do nothing on a local paused game. The
autofill maintenance and queued add processing were inside the non-paused
server simulation block, and the add throttle used `sv.framenum`, which does
not advance while paused. The cvar had changed, but the lifecycle pass was not
advancing.

Live roaming had three interacting failures. Active FFA roam goals recomputed a
new route direction from the current view every frame, so a bot without a
stronger goal could spin or churn in place. Non-attack decisions still faced
any known visible enemy, so forward-relative movement walked bots toward other
players and walls even when the decision owner was a route or item. Finally,
role-combat helpers could manufacture an attack from a visible target even
when the base combat/action layer was switching weapons, avoiding a weak
fight, withholding fire, or trying to move to an item.

## Implementation

- Added `assets/botfiles/bots.txt` with `vanguard`, `vector`, `bulwark`,
  `relay`, and `smoke` entries.
- Updated package validation so `botfiles/bots.txt` is a required botfile
  support member.
- Added rotating min-player profile selection in `src/server/main.c`, with a
  non-`smoke` preference and `bot_profile` override support.
- Moved bot add-queue processing and min-player maintenance outside the paused
  simulation gate, and changed the one-add-per-frame throttle to use realtime
  frames while paused.
- Changed active FFA roam route handling to persist the existing timed route
  instead of recalculating from the bot's current view every frame.
- Let live pickup decisions defer and clear generic FFA roam goals so route
  steering can commit to useful items.
- Limited enemy-facing view overrides to actual attack decisions that press
  attack; route, item, retreat, and other movement decisions now face their
  route target.
- Made role-combat defer instead of override when the base action layer is not
  truly attacking, is waiting on a weapon switch, or determines the bot is weak
  and underpowered.
- Reconciled scenario contracts to treat role combat as a booster for a valid
  base attack, not a separate source that can force attack input.

## Validation

- `python -m pytest tools/test_package_assets.py tools/bot_scenarios/test_run_bot_scenarios.py -q`
  passed with 65 tests.
- `python -B tools\bot_profiles\validate_bot_profiles.py assets\botfiles\bots --format json`
  passed with 5 profiles, 0 errors, and 0 warnings.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64`
  passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  refreshed the staged install, packed 94 assets, validated the botfile
  release payload, mirrored loose botfiles, and validated the staged install.
- Focused live behavior validation passed for
  `profile_backed_spawn,ffa_roam_route,ffa_live_pacing,duel_live_pacing,combat_survival_regression,threat_retreat_avoidance,weapon_scoring_arsenal,aim_fire_policy_depth,behavior_arbitration`
  from `.tmp\bot_scenarios\bot-profile-roam-state-fixes-rerun3`.
- Legacy role-combat compatibility validation passed for
  `behavior_policy_umbrella,team_role_combat,team_role_combat_avoidance,ctf_role_combat,ffa_role_combat,ffa_spawn_camp_combat_avoidance,tdm_role_spawn_stability`
  from `.tmp\bot_scenarios\bot-role-combat-compat-check3`.
- Direct min-player/profile smoke from the refreshed `.install` payload loaded
  5 active profiles and added `B|Bulwark`, `B|Relay`, and `B|Vanguard` before
  trimming and disabling autofill. The smoke marker reported
  `profiled=3 first_profile=bulwark`.

## Source Boundaries

No Q3A, Quake3e, BSPC, Gladiator, or `q2proto/` source was imported or modified
in this round. The changes are WORR-native server lifecycle, bot brain
arbitration, scenario contract, packaging, botfile manifest, and documentation
work.
