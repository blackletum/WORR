# Q3A BotLib Natural Movement and Interaction Diagnostics

Date: 2026-06-18
Task: FR-04-T14
Lane: Halley, natural movement and world interaction diagnostics

## Scope

This note records the nav-layer diagnostics added for natural crouch, swim,
waterjump, and world interaction retry context. The change is intentionally
limited to bot navigation policy/status and bot brain debug output. It does not
change server smoke lifecycle, scenario tooling, perf tooling, AAS generation,
or canonical project tracking documents.

## Diagnostic Status Fields

`BotNavRouteStatus` now exposes reason-coded natural movement support for the
loaded AAS:

- `naturalMovementUnsupportedMask`: bit 1 crouch, bit 2 swim, bit 4 waterjump.
- Per movement type reason: `0` unknown, `1` supported, `2` AAS not loaded,
  `3` no route start for the travel type, `4` invalid route areas.
- Per movement type route evidence: start area, goal area, and rounded start
  origin when a support probe succeeds.

The bot brain debug output reports these values through
`q3a_bot_nav_natural_support_status`. Unsupported movement now fails
informatively: a smoke can distinguish an unloaded AAS from a loaded map that
has no deterministic route start for a natural travel type.

World interaction context is also reported through
`q3a_bot_nav_interaction_context_status`:

- map entity counts for doors, buttons, platforms, trains, waters, triggers,
  movers, use-capable entities, and touch-capable entities.
- last activated interaction entity spawn count, origin, absolute bounds,
  use/touch flags, solid, and move type.

## Current mm-rage Evidence

The packaged `mm-rage` AAS loads successfully, but it still does not expose
natural crouch, swim, or waterjump route starts through the current route-start
probe:

- `natural_movement_support_aas_loaded=1`
- `natural_movement_support_checks=3`
- `natural_movement_support_supported=0`
- `natural_movement_support_unsupported=3`
- `natural_movement_unsupported_mask=7`
- crouch, swim, and waterjump each report reason `3` (`NoRouteStart`)
- start areas, goal areas, and source origins remain zero for all three types

The same map does expose deterministic interaction context for the elevator
case:

- `interaction_world_entities=17`
- `interaction_world_doors=0`
- `interaction_world_buttons=0`
- `interaction_world_platforms=2`
- `interaction_world_trains=0`
- `interaction_world_waters=0`
- `interaction_world_triggers=1`
- `interaction_world_movers=14`
- `interaction_world_use_entities=3`
- `interaction_world_touch_entities=3`
- elevator retry activated entity `18` with origin `(0, 0, -192)`, bounds
  `(-330, -586, -170)` to `(-190, -438, 66)`, `use=1`, `touch=0`,
  `solid=3`, and `movetype=2`

## Reference Map Requirements

Natural crouch validation needs a reference map and packaged AAS where:

- at least one bot-reachable route requires `TRAVEL_CROUCH`.
- both start and goal areas are valid through the AAS route-start probe.
- the crouch passage is not solvable as ordinary walking, jumping, ladder,
  elevator, or walkoffledge travel.
- smoke output reports crouch reason `1`, non-zero crouch start and goal
  areas, and a stable source origin inside or adjacent to the crouch-only
  route start.

Natural swim validation needs:

- an AAS with water volume areas and at least one route requiring
  `TRAVEL_SWIM`.
- a start and goal area pair where the route cannot be completed without water
  movement.
- entity/world context that makes the water source identifiable. If the water is
  only worldspawn brush content, interaction entity counts may remain zero; the
  natural movement status is the authoritative proof.
- smoke output reports swim reason `1`, non-zero swim start and goal areas, and
  a stable source origin inside the water route.

Natural waterjump validation needs:

- a water exit ledge or equivalent AAS reachability requiring
  `TRAVEL_WATERJUMP`.
- valid start and goal areas spanning the water-to-ledge transition.
- geometry that cannot be traversed as ordinary swim plus jump, ladder, or
  elevator travel.
- smoke output reports waterjump reason `1`, non-zero start and goal areas, and
  a stable source origin at the waterjump approach.

Door or trigger retry validation needs:

- a deterministic map case with a bot-reachable blocking door, button, trigger,
  platform, or train.
- a nearby use or touch interaction entity that can be associated with the route
  failure/retry.
- smoke output where `q3a_bot_nav_interaction_context_status` reports non-zero
  matching entity counts and `last_nav_interaction_entity` includes origin,
  bounds, use/touch, solid, and move type after activation.

## Validation Run

Build:

```text
meson compile -C builddir-win
```

Result: pass. The build refreshed `sgame_x86_64.dll`; Ninja emitted the
pre-existing recovery warning but completed successfully.

Install refresh:

```text
python .\tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
```

Result: pass. Packaged AAS audit passed and `maps/mm-rage.aas` retained SHA256
`6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.

Elevator interaction smoke:

```text
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27968 +set logfile 1 +set logfile_name q3a_bot_nav_elevator_diagnostics_halley +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 12 +map mm-rage | Select-String -Pattern 'q3a_bot_frame_command_smoke_travel_type_goal|q3a_bot_frame_command_status|q3a_bot_nav_policy_status|q3a_bot_nav_natural_support_status|q3a_bot_nav_interaction_context_status|commandMsec underflow'
```

Result: pass. `q3a_bot_frame_command_status pass=1`; elevator policy remained
supported and activated entity `18`; no `commandMsec underflow` match.

Forced crouch smoke:

```text
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27969 +set logfile 1 +set logfile_name q3a_bot_nav_forced_crouch_diagnostics_halley +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 6 +map mm-rage | Select-String -Pattern 'q3a_bot_frame_command_status|q3a_bot_nav_natural_support_status|q3a_bot_nav_interaction_context_status|commandMsec underflow'
```

Result: pass. `q3a_bot_frame_command_status pass=1`;
`movement_state_crouch_commands=17`; no `commandMsec underflow` match.

Forced swim smoke:

```text
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27970 +set logfile 1 +set logfile_name q3a_bot_nav_forced_swim_diagnostics_halley +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 7 +map mm-rage | Select-String -Pattern 'q3a_bot_frame_command_status|q3a_bot_nav_natural_support_status|q3a_bot_nav_interaction_context_status|commandMsec underflow'
```

Result: pass. `q3a_bot_frame_command_status pass=1`;
`movement_state_swim_commands=17`; no `commandMsec underflow` match.

## Remaining Risks

- `mm-rage` cannot prove natural crouch, swim, or waterjump behavior because the
  loaded AAS returns no route start for those travel types.
- The world interaction summary is entity based. Water represented only as
  worldspawn brush contents may not appear in interaction entity counts.
- Last interaction entity context is only meaningful after a retry activation;
  forced movement smokes correctly report no last interaction entity.
- Dedicated reference maps and packaged AAS data are still required for true
  natural crouch, swim, waterjump, door, and trigger behavior-policy proof.
