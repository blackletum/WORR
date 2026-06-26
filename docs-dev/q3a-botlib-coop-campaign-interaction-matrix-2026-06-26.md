# Q3A BotLib Coop Campaign Interaction Matrix

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Purpose

Promote the next coop/campaign validation slice from the completion roadmap by
adding a map-backed interaction matrix row beyond the existing `coop_live_loop`
and `coop_share_loop` aggregate proofs.

The new row keeps the same coop live-loop behavior family but runs it on the
packaged campaign map `base1`, so route-interaction retry, campaign mover
source ownership, teammate hold behavior, coop wait/follow policy, and
interaction telemetry are proven on a second generated AAS map instead of only
the default `mm-rage` setup.

## Implementation

- `src/server/main.c` now treats smoke mode `91` as a coop live-loop smoke and
  lets it use the travel-type goal setup needed by the interaction/mover proof.
- `tools/bot_scenarios/run_bot_scenarios.py` reserves mode `91` as
  `coop_campaign_interaction_matrix` and adds the implemented scenario row on
  `base1` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_live_loop 1`.
- The scenario hard-gates the begin marker, coop and match readiness, route
  commands with zero route failures, coop leader-route/progress-wait evidence,
  route-interaction retry, door/elevator source commands, teammate hold
  commands, live nav interaction candidates, and coop wait-policy evidence.
- `tools/bot_scenarios/test_run_bot_scenarios.py` adds synthetic mode `91`
  raw-marker coverage, catalog assertions, marker-contract assertions, and
  command-construction coverage for the `base1` map override.
- `tools/bot_scenarios/README.md` documents the new row and reserved mode.

No Q3A, BSPC, Gladiator, Quake3e, baseq3a, or `q2proto/` source files were
imported or modified for this round.

## Validation

- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  - Passed: 54 tests.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64 copy_sgame_dll`
  - Passed.
- `python tools/refresh_install.py --build-dir builddir-win`
  - Passed; refreshed `.install/` with current Windows binaries and packaged
    assets.
- Focused scenario:
  - `python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --scenario coop_campaign_interaction_matrix --artifact-dir .tmp\bot_scenarios\20260626Tcoop-campaign-interaction-final --format text`
  - Passed from `.tmp\bot_scenarios\20260626Tcoop-campaign-interaction-final\20260626T185108Z`.
  - Key evidence: `mode=91`, `map=base1`, `coop_live_loop=1`, `frames=121`,
    `commands=121`, `route_commands=61`, `route_failures=0`,
    `coop_leader_route_activations=60`, `coop_progress_wait_commands=60`,
    `coop_interaction_retry_commands=3`,
    `coop_door_elevator_source_commands=3`,
    `coop_door_elevator_hold_commands=60`,
    `last_coop_door_elevator_entity=360`,
    `nav_interaction_activations=3`, `nav_interaction_candidates=21`, and
    `team_objective_coop_policy_wait=60`.
- Full implemented catalog:
  - `python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --scenario implemented --artifact-dir .tmp\bot_scenarios\20260626Timplemented-coop-campaign-interaction-json --json-out .tmp\bot_scenarios\20260626Timplemented-coop-campaign-interaction-report.json --format text`
  - Passed from `.tmp\bot_scenarios\20260626Timplemented-coop-campaign-interaction-json\20260626T185549Z`.
  - Result: 99 passed, 0 failed, 0 timeout, 0 error, 0 pending.

## Follow-Up

- Expand the movement/hazard matrix with water, crouch, doors, elevators,
  teleporters, and hazard diagnostics on representative maps.
- Add another campaign interaction row once key/button/trigger objective
  progression has a stronger map-backed status surface.
- Keep the coop matrix rows focused on map evidence instead of raw catalog
  growth.
