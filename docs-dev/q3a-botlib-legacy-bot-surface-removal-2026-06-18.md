# Q3A BotLib Legacy Bot Surface Removal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

The inherited Quake II Rerelease `src/game/sgame/bots/` debug/export/entity-state helper layer has been removed from the active server-game bot implementation. That layer was built around a different engine bot system and should not be used as the foundation for WORR's Q3A BotLib/AAS port.

The active WORR bot surface is now the replacement path:

- `src/game/sgame/bots/bot_runtime.*` for BotLib/AAS lifecycle, public `sg_bot_*` cvars, map load/unload, and frame-level entity sync.
- `src/game/sgame/bots/botlib_adapter.*` for the narrow bridge into imported Q3A BotLib/AAS code.
- `src/game/sgame/bots/bot_nav.*` for AAS route lookup, route cache state, persistent route goals, command steering, and native debug overlays.
- `src/game/sgame/bots/bot_think.*` for game-side bot frame hooks and `Bot_BuildFrameCommand()`.
- `src/game/sgame/bots/q3a/` for quarantined imported Q3A BotLib/AAS runtime code and WORR-native import glue.

## Code Changes

- Removed `src/game/sgame/bots/bot_debug.cpp` and `bot_debug.hpp`.
- Removed `src/game/sgame/bots/bot_exports.cpp` and `bot_exports.hpp`.
- Removed `src/game/sgame/bots/bot_utils.cpp` and `bot_utils.hpp`.
- Removed those files from the Meson `sgame` source list.
- Removed `bot_debug_follow_actor` and `bot_debug_move_to_point` cvar registration.
- Removed the per-frame `Bot_UpdateDebug()` call from `G_RunFrame_()`.
- Removed sgame-side calls into the old engine bot entity registrar/unregistrar path.
- Removed the legacy `info_nav_lock` spawn registration because its implementation lived in the deleted Q2R bot utility layer.
- Updated `bot_includes.hpp` so it includes only the replacement BotLib runtime, adapter, and thinking headers.
- Left the old Q2R bot action/export callbacks unavailable from `G_GetGameAPI()`.
- Retained `Entity_ForceLookAtPoint` as a standalone helper in `g_main.cpp`, outside the removed bot export file.

## Replacement Boundary

Weapon switching, item use, trigger interaction, pickup checks, and bot debug behavior should be reintroduced as WORR-native bot action/brain modules above `bot_nav` and `botlib_adapter`. They should not revive `Bot_SetWeapon`, `Bot_UseItem`, `Bot_TriggerEntity`, `Bot_GetItemID`, `Bot_PickedUpItem`, `Bot_MoveToPoint`, `Bot_FollowActor`, or the old bot entity registration flow.

The shared game export/import structs still contain legacy bot fields for now, but `sgame` no longer implements the removed Q2R bot behavior. A later API cleanup can remove or quarantine those fields once the server side no longer needs to expose compatibility slots.

## Validation

- `meson compile -C builddir-win` passed after removing the final deleted-symbol residues (`Entity_UpdateState` and `SP_info_nav_lock`).
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas` passed, rebuilt `.install`, packaged `maps/mm-rage.aas`, and validated the `windows-x86_64` staged payload.
- Dedicated server smoke on `mm-rage` passed with `sg_bot_enable 1`, `sg_bot_debug_route 1`, `sg_bot_debug_goal 1`, and `sv_bot_frame_command_smoke 2`.
- Smoke status: `frames=8`, `commands=8`, `route_requests=8`, `route_queries=2`, `route_reuses=6`, `route_commands=8`, `route_failures=0`, `route_goal_requests=1`, `route_goal_assignments=1`, `route_goal_cache_reuses=6`, `last_persistent_goal_area=227`, and `pass=1`.
