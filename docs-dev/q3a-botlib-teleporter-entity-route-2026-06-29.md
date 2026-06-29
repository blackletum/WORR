# Q3A BotLib Teleporter Entity Route

Date: 2026-06-29

Tasks: `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`

## Summary

Mode `95` is no longer an expected-blocked teleporter gap. The scenario is now
`movement_teleporter_entity_route` on packaged map `train`.

The exact Q3A AAS `TRAVEL_TELEPORT` route remains unsupported and visible in
telemetry. The runtime fallback now scans live map entities for touch-capable
teleporters, persists the selected entity as a position goal, and asks the Q3A
import layer for a first-reachability route toward that exact entity-backed
goal. This gives bots a real route command toward a teleporter without claiming
that generated Q2 AAS currently supports full `TRAVEL_TELEPORT` reachability.

## Implementation

- Added `Q3A_BotLibImport_BuildRouteSteerTowardGoal()` and
  `BotLibAdapter_BuildRouteSteerTowardGoal()`.
- The new import path first accepts a complete exact preferred-goal route when
  Q3A can predict one. If full prediction stops short but
  `AAS_AreaReachabilityToGoalArea()` still provides a next reachability toward
  the preferred entity area, it returns a direct first-step route while keeping
  the requested entity area as `goalArea` / `goalOrigin`.
- `bot_nav.*` now recognizes `teleporter_touch` alongside the existing
  teleporter classes, selects the nearest valid teleporter entity with a
  resolved AAS area, and uses the new route-toward-goal fallback only when the
  requested travel type is `TRAVEL_TELEPORT`.
- `bot_brain.cpp` treats this as a passing fallback only when exact travel-type
  resolution stays at zero, the teleporter entity fallback resolves and assigns,
  and route commands are emitted.
- `tools/bot_scenarios/run_bot_scenarios.py` now promotes mode `95` to
  `movement_teleporter_entity_route` and requires entity fallback counters,
  no bot-nav route failures, no smoke start warp, and an unsupported exact
  `TRAVEL_TELEPORT` support signal.

## Validation

- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py -q`
  - Result: 58 passed.
- `meson compile -C builddir-win sgame_x86_64`
  - Result: passed.
- `python tools/refresh_install.py --build-dir builddir-win --package-q2aas-aas`
  - Result: passed; refreshed `.install/` and packaged validated AAS files.
- `python tools/bot_scenarios/run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --scenario movement_teleporter_entity_route --artifact-dir .tmp\bot_scenarios\teleporter_entity_route_final --format text --json-out .tmp\bot_scenarios\teleporter_entity_route_final.json`
  - Result: passed from
    `.tmp\bot_scenarios\teleporter_entity_route_final\20260629T191851Z`.
- `python tools/bot_release/run_bot_acceptance.py --install-dir .install --allow-missing-scenario-report --format text --output .tmp\bot_release\bot_release_acceptance_teleporter_entity_route.txt`
  - Result: passed, `11/11` checks, reusing
    `.tmp\bot_scenarios\implemented_hazard_context.json` as the existing
    full-suite scenario sidecar.

Focused scenario metrics:

- `pass=1`
- `frames=8`
- `commands=8`
- `route_commands=8`
- `route_failures=0`
- `travel_type_goal_requests=2`
- `travel_type_goal_resolved=0`
- `travel_type_goal_assignments=0`
- `travel_type_goal_start_warps=0`
- `teleporter_entity_goal_requests=8`
- `teleporter_entity_goal_candidates=32`
- `teleporter_entity_goal_resolved=8`
- `teleporter_entity_goal_assignments=1`
- `teleporter_entity_goal_fallbacks=2`
- `position_goal_assignments=1`
- `last_teleporter_entity_goal_entity=88`
- `last_teleporter_entity_goal_area=502`
- `last_teleporter_entity_goal_action=1`
- `last_route_end_area=849`
- `last_reachability_type=2`

Source counters show the honest split: two exact travel-type route attempts
failed, while two entity-toward fallback route builds succeeded. The bot-nav
surface therefore passes without hiding the underlying `TRAVEL_TELEPORT` AAS
support gap.

## Follow-Up

- Natural crouch remains expected-blocked until a suitable reference map or
  generated proof map exposes accepted crouch travel.
- Hazard traversal/avoidance remains expected-blocked until a reference BSP/AAS
  pair exposes slime/lava/hurt content.
- Broader teleporter polish should eventually prefer actual teleporter
  destination utility and post-touch continuation routes instead of only
  routing toward the trigger entity.
