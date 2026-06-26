# Q3A BotLib Coop Door/Elevator Cooperation

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds the first smoke-backed cooperative door/elevator command proof
above the existing Q3A AAS `TRAVEL_ELEVATOR` route and WORR interaction retry
telemetry. The behavior is default-off behind `sg_bot_coop_door_elevator` and is
reserved for server smoke mode `31`.

The proof elects one coop bot as the interaction source, lets that bot own the
route-detected wait/use interaction window, and keeps the teammate in a non-route
hold command so it does not crowd the mover or add route failures while the source
handles the platform/elevator.

## Implementation

- `bot_brain.cpp` now tracks `coop_door_elevator_*` compact coop-command status
  counters for requests, source activations, source wait/use commands, support
  hold commands, invalid skips, and last interaction metadata.
- `sg_bot_coop_door_elevator` selects smoke mode `31` and gates all new behavior.
- Source ownership is deterministic for the proof lane: the lowest alive coop
  client owns the mover interaction; non-source coop bots become supporters.
- The source path reuses `BotNav_RequestInteractionRetry(...)` so door,
  platform, train, trigger, and generic mover interaction windows come from the
  existing nav interaction detector rather than a separate string or entity hack.
- The support path has an early non-route command owner. When an active source
  interaction is available, the supporter stops and faces the interacted entity
  or source bot; otherwise it idles while facing the leader. That prevents the
  second bot from attempting an unsupported duplicate elevator route.
- The generic coop leader-route timed owner is suppressed while
  `sg_bot_coop_door_elevator` is active so the proof has one clear command owner
  stack.
- `src/server/main.c` reserves smoke mode `31`, enables
  `sg_bot_coop_door_elevator` during that scenario, targets two bots, and uses
  `TRAVEL_ELEVATOR`.
- `tools/bot_scenarios/run_bot_scenarios.py` promotes `coop_door_elevator` as an
  implemented scenario with marker gates for coop readiness, match setup,
  elevator interaction activation, source ownership, support hold ownership, and
  last mover metadata.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas`
- Focused scenario:
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario coop_door_elevator --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 90 --format text --artifact-dir .tmp\bot_scenarios\coop-door-elevator --json-out .tmp\bot_scenarios\coop-door-elevator.json --markdown-out .tmp\bot_scenarios\coop-door-elevator.md`

Focused `coop_door_elevator` result:

- `pass=1`
- `frames=121`
- `commands=121`
- `route_commands=61`
- `route_failures=0`
- `nav_interaction_elevator_activations=3`
- `coop_door_elevator_requests=61`
- `coop_door_elevator_source_activations=3`
- `coop_door_elevator_source_commands=36`
- `coop_door_elevator_hold_commands=60`
- `last_coop_door_elevator_action=3`
- `last_coop_door_elevator_kind=3`
- `last_coop_door_elevator_entity=18`
- `last_coop_door_elevator_source_client=0`

## Notes

This is deliberately a proof lane, not full campaign AI. It proves that coop bots
can split mover/elevator responsibilities through a cvar-gated command-owner path.
Broader campaign-specific trigger sequencing, keys, scripted objectives, and
multi-entity coordination remain follow-up work.

No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
imported or modified for this update.
