# Q3A BotLib Natural Movement and Door/Trigger Retry Navigation

Date: 2026-06-18
Task: FR-04-T14

## Summary

This slice adds first-pass navigation-policy support for natural movement-state validation and map-backed interaction retry without changing the server smoke lifecycle.

The bot route layer now records whether a requested natural travel-type goal is supported by the loaded AAS before attempting to route it. The command layer also reports natural, non-forced crouch/swim/waterjump command counts separately from forced movement-state smokes.

The route layer also performs a one-time loaded-AAS support census for the three missing natural movement proof cases: `TRAVEL_CROUCH`, `TRAVEL_SWIM`, and `TRAVEL_WATERJUMP`. This lets any smoke summary report whether the current map package can possibly validate those natural states, even when the current server smoke modes cannot request them directly.

The route layer also gained a short wait/use interaction retry window. When a route reaches a deterministic mover case, currently proven by the packaged `mm-rage` elevator reachability, navigation scans nearby BSP movers, platforms, buttons, doors, triggers, trains, and water movers, records the best candidate, then asks command building to pause lateral movement and press use when the entity exposes a use action.

## Implementation

- `BotNavRouteStatus` now reports travel-type support probes:
  - `travel_type_goal_support_checks`
  - `travel_type_goal_supported`
  - `travel_type_goal_unsupported`
  - `last_travel_type_goal_support_type`
  - `last_travel_type_goal_support_area`
  - `last_travel_type_goal_support_goal_area`
- `BotNavRouteStatus` now reports interaction retry policy:
  - `nav_interaction_checks`
  - `nav_interaction_candidates`
  - `nav_interaction_activations`
  - `nav_interaction_stuck_activations`
  - `nav_interaction_elevator_activations`
  - `nav_interaction_wait_frames`
  - `nav_interaction_use_frames`
  - `nav_interaction_misses`
  - `last_nav_interaction_*`
- `BotNavRecoveryMove` can now represent wait/use intent in addition to the older side-step recovery.
- `Bot_CommandApplyRecoveryMove` converts interaction recovery into zero forward/side movement and `BUTTON_USE` where appropriate.
- `TRAVEL_WATERJUMP` is now counted separately from generic jump commands, so future forced or natural waterjump smokes can prove the exact state.
- New telemetry is emitted on `q3a_bot_nav_policy_status` instead of making the already-large `q3a_bot_frame_command_status` line longer.
- Natural movement support telemetry is emitted on `q3a_bot_nav_natural_support_status`:
  - `natural_movement_support_aas_loaded`
  - `natural_movement_support_checks`
  - `natural_movement_supported`
  - `natural_movement_unsupported`
  - `natural_crouch_supported`
  - `natural_crouch_unsupported`
  - `natural_crouch_area`
  - `natural_crouch_goal_area`
  - `natural_swim_supported`
  - `natural_swim_unsupported`
  - `natural_swim_area`
  - `natural_swim_goal_area`
  - `natural_waterjump_supported`
  - `natural_waterjump_unsupported`
  - `natural_waterjump_area`
  - `natural_waterjump_goal_area`

## Current Map Evidence

The current staged `mm-rage.aas` packaged by `refresh_install.py` is:

`sha256 6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`

`.tmp/q2aas/stage-report.json` reports:

- `crouch=0`
- `swim=0`
- `water jump=0`
- `elevator=1`
- `func_plat=2`
- `trigger_once=1`

This means the current package can prove the interaction retry path through elevator/platform behavior, but it cannot prove natural map-backed crouch, swim, or waterjump command emission. The code now reports those unsupported cases explicitly in `q3a_bot_nav_natural_support_status`, and a future reference map/AAS must contain nonzero `TRAVEL_CROUCH`, `TRAVEL_SWIM`, and `TRAVEL_WATERJUMP` reachabilities to close that proof.

Current runtime census on `mm-rage`:

- `natural_movement_support_aas_loaded=1`
- `natural_movement_support_checks=3`
- `natural_movement_supported=0`
- `natural_movement_unsupported=3`
- `natural_crouch_supported=0`
- `natural_crouch_unsupported=1`
- `natural_swim_supported=0`
- `natural_swim_unsupported=1`
- `natural_waterjump_supported=0`
- `natural_waterjump_unsupported=1`

## Reference Map/AAS Requirements

All three reference cases must satisfy both generator/report requirements and runtime adapter requirements:

- The staged report must show nonzero `travel_counts` for the target natural type.
- `BotLibAdapter_FindRouteStartForTravelType(type)` must find a start area and goal area, surfaced by `q3a_bot_nav_natural_support_status`.
- The natural route must be deterministic enough that a smoke/harness can set `sg_bot_nav_travel_type_goal` to the target type with `sg_bot_nav_travel_type_goal_warp=1` and receive a route whose `last_reachability_type` is the target type.
- The command summary must show the matching natural movement counter reaching the expected command threshold without using `sg_bot_frame_command_smoke_travel_type`.

`TRAVEL_CROUCH` reference requirement:

- Travel type id: `3`.
- The map must include at least one reachable crouch-only transition: normal presence cannot traverse it, crouch presence can.
- The staged report must include `travel_counts["crouch"] >= 1`.
- Runtime support must show `natural_crouch_supported=1`, `natural_crouch_unsupported=0`, and nonzero `natural_crouch_area` plus `natural_crouch_goal_area`.
- Natural validation should request travel type `3` and expect `natural_movement_state_crouch_commands >= expected_min_commands`, with `last_movement_state_forced_travel_type=0`.

`TRAVEL_SWIM` reference requirement:

- Travel type id: `8`.
- The map must include adjacent or route-connected liquid AAS areas where the generator can create swim reachability through water/slime/lava contents. The relevant generator path creates `TRAVEL_SWIM` when a shared face center is inside liquid contents.
- The staged report must include `travel_counts["swim"] >= 1`.
- Runtime support must show `natural_swim_supported=1`, `natural_swim_unsupported=0`, and nonzero `natural_swim_area` plus `natural_swim_goal_area`.
- Natural validation should request travel type `8` and expect `natural_movement_state_swim_commands >= expected_min_commands`, with `last_movement_state_forced_travel_type=0`.

`TRAVEL_WATERJUMP` reference requirement:

- Travel type id: `9`.
- The map must include a water-to-ledge transition where water is present below the ledge test point, the vertical relationship is within the generator's waterjump allowance, and both source and destination are normal-presence areas. The current Q3A reachability code explicitly rejects waterjumping from or toward crouch-only areas.
- The staged report must include `travel_counts["water jump"] >= 1`.
- Runtime support must show `natural_waterjump_supported=1`, `natural_waterjump_unsupported=0`, and nonzero `natural_waterjump_area` plus `natural_waterjump_goal_area`.
- Natural validation should request travel type `9` and expect `movement_state_waterjump_commands >= expected_min_commands` plus `natural_movement_state_waterjump_commands >= expected_min_commands`, with `last_movement_state_forced_travel_type=0`.

The current server smoke mode table does not expose natural crouch, swim, or waterjump route-goal modes. Closing the behavioral proof therefore requires a parent-owned smoke mode or another deterministic harness path that sets the existing `sg_bot_nav_travel_type_goal` cvar to `3`, `8`, or `9`.

## Validation

Build:

```powershell
meson compile -C builddir-win
```

Result: passed. Existing warnings remained in q2aas/C dependency code.

Install refresh:

```powershell
python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
```

Result: passed, including packaged AAS archive audit.

Elevator interaction smoke:

```powershell
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27965 +set logfile 1 +set logfile_name q3a_bot_nav_elevator_interaction_natural_support +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 12 +map mm-rage
```

Result: passed with `pass=1`.

Key policy metrics:

- `travel_type_goal_support_checks=2`
- `travel_type_goal_supported=2`
- `travel_type_goal_unsupported=0`
- `last_travel_type_goal_support_type=11`
- `last_travel_type_goal_support_area=241`
- `last_travel_type_goal_support_goal_area=261`
- `nav_interaction_checks=1`
- `nav_interaction_candidates=1`
- `nav_interaction_activations=1`
- `nav_interaction_elevator_activations=1`
- `nav_interaction_wait_frames=8`
- `nav_interaction_use_frames=8`
- `last_nav_interaction_action=3`
- `last_nav_interaction_kind=3`
- `last_nav_interaction_entity=18`
- `last_nav_interaction_distance_sq=4932`
- `last_nav_interaction_travel_type=11`
- `interaction_wait_command_uses=8`
- `interaction_use_command_uses=8`
- `natural_movement_support_aas_loaded=1`
- `natural_movement_support_checks=3`
- `natural_movement_supported=0`
- `natural_movement_unsupported=3`

Forced crouch regression:

```powershell
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27966 +set logfile 1 +set logfile_name q3a_bot_nav_forced_crouch_natural_support +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 6 +map mm-rage
```

Result: passed with `pass=1`, `movement_state_crouch_commands=17`, no interaction activations, and `natural_movement_unsupported=3`.

Forced swim regression:

```powershell
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27967 +set logfile 1 +set logfile_name q3a_bot_nav_forced_swim_natural_support +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 7 +map mm-rage
```

Result: passed with `pass=1`, `movement_state_swim_commands=17`, no interaction activations, and `natural_movement_unsupported=3`.

## Residual Work

- Add or stage a reference map/AAS satisfying the crouch, swim, and waterjump requirements above.
- Add parent-owned natural travel-type smoke modes, or another deterministic harness path, for `TRAVEL_CROUCH`, `TRAVEL_SWIM`, and `TRAVEL_WATERJUMP`.
- Expand door/trigger validation once a deterministic packaged map exposes an actual door/button/trigger navigation case. The current implementation scans and reports those classes, but the current runtime proof is the `func_plat` elevator case.
- Interaction retry is command-level wait/use behavior. It does not directly invoke entity callbacks, preserving normal player-like navigation semantics.
