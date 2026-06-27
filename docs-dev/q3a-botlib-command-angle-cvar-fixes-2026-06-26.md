# Q3A BotLib Command Angle, Cvar, and Command Surface Fixes

Date: 2026-06-26

Tasks: `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `DV-07-T06`

## Summary

This slice fixes the live bot command path after inspecting the Q3A bot implementation. WORR bot commands were writing absolute world-space angles straight into `usercmd_t::angles`, but the Q2/WORR pmove path computes final view angles as `cmd.angles + pmove.deltaAngles`. Spawn and teleport paths seed `deltaAngles`, so bots were effectively applying that offset twice. The visible result was erratic yaw/pitch model flipping; the movement result was forward movement in a direction that did not match the route steering target, which made bots scrape and stick to walls.

The cvar and command surface is also corrected to match the Q3/Q2R-style bot contract. Public bot cvars now use `bot_*` directly, not `sv_bot_*` or `sg_bot_*`, and the visible compatibility-alias layer was removed because it polluted console completion. The `smoke` suffix remains reserved for unattended validation paths rather than normal player configuration.

## Q3A Reference

Q3A converts bot input view angles into command angles by subtracting player-state delta angles before dispatching the user command. The relevant reference is `BotInputToUserCommand()` in `E:\_SOURCE\_CODE\baseq3a-master\code\game\ai_main.c`, which writes view angles and then subtracts `delta_angles`. Q3A bot view angles are also normalized before being passed through the elementary action input layer.

## Implementation Notes

- Added command-angle normalization in `src/game/sgame/bots/bot_brain.cpp`: pitch is normalized to signed degrees and clamped to `[-89, 89]`, yaw is wrapped with `anglemod`, and roll is cleared.
- Converted desired world view angles to WORR `usercmd_t` angles by subtracting `bot->client->ps.pmove.deltaAngles`, matching the Q3A command-frame convention.
- Updated bot entity snapshots in `src/game/sgame/bots/bot_runtime.cpp` to publish live `client->vAngle` for clients instead of stale `sv.viewAngles`.
- Registered bot runtime, min-player, profile, debug, and smoke cvars as plain `bot_*`.
- Removed the interim visible `sv_bot_*`/`sg_bot_*` alias layer; old config lines with those names are no longer treated as bot controls.
- Made `bot_min_players` an autofill request on its own. It now tops up bots without requiring a separate `bot_enable 1`, and the BotLib runtime treats `bot_min_players > 0` as an enable condition.
- Replaced the provisional server commands with `addbot`, `removebot`, `kickbots`, `botlist`, and `bot_reload_profiles`. Manual `addbot` enables the BotLib runtime, and accepts the Q3-shaped `[skill] [team] [delay] [altname]` argument positions while WORR currently consumes profile/name and team.
- Updated `tools/bot_scenarios/` to emit canonical `bot_frame_command_smoke`, `bot_profile_smoke`, and related smoke cvars.

## Behavior Notes

- `bot_min_players 5` should be enough to top a live server up to five total public clients, counting humans and manually added bots before auto-managed bots.
- `bot_enable 1` still explicitly enables BotLib for manual/diagnostic workflows, but it is no longer required before using `bot_min_players`.
- Smoke cvars remain developer validation switches, not end-user bot configuration. They are kept under `bot_*` because the code they exercise is the server-game bot path, even though the harness trigger lives in the engine executable.
- The old `sv_bot_*`/`sg_bot_*` names intentionally do not autocomplete from a fresh run. If a stale config defines them, they are inert custom cvars and should be removed or renamed to `bot_*`.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated `bot_min_players 2` run on `mm-rage` added `B|bot1` and `B|bot2` without `bot_enable 1`; `cvarlist sv_bot_*` and `cvarlist sg_bot_*` each reported `0 of 660 cvars`.
- Dedicated `addbot smoke` run resolved the staged `smoke` profile and spawned `B|Smoke`.
- Dedicated `bot_frame_command_smoke 2` run reported `route_failures=0` and `pass=1`.
