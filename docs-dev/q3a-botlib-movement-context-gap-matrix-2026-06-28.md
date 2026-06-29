# Q3A BotLib Movement Context Gap Matrix

Date: 2026-06-28

Tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T04`, `FR-04-T05`,
`FR-04-T11`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`,
and `DV-07-T06`.

## Purpose

Continue the post-checklist bot implementation roadmap by tightening live
roaming, retreat, attack ownership, map movement coverage, profile-backed bot
selection, and scenario isolation. This round follows the earlier profile,
roam, and movement matrix work, but focuses on gaps that made bots look stuck,
over-fixated on visible players, or too eager to fight while weak.

## Implementation

- Live FFA roam now yields to item goals outside smoke mode. If an item route
  goal is available, the generic FFA roam timed route clears or defers instead
  of refreshing from the bot's current view direction. This keeps item pickup
  pressure from being overwritten by a generic roaming owner.
- Role combat now defers unless the base action layer is actually attacking,
  and still defers for weak/underpowered combat states. This prevents
  team/FFA/CTF role-combat helpers from fabricating attack ownership when the
  bot should be routing, switching, recovering, or withholding fire.
- Bot command angles now face the selected route target unless the accepted
  decision is a firing attack. Visible enemies still control aim while firing,
  but non-attack movement no longer keeps snapping back to the closest target.
- Runtime nav interaction context now classifies teleporters and hazards
  separately, prints those counters in the early compact interaction status,
  and keeps that status ahead of the verbose frame-command diagnostic so the
  scenario harness can always see the compact proof fields.
- Movement diagnostics now include waterjump command counters, and the scenario
  matrix adds gap rows for crouch, swim, waterjump, teleporter, and door
  context behavior.
- The frame-command status capture buffer grew from 32 KiB to 256 KiB. The
  previous limit could truncate late compact status markers in long aggregate
  runs, which made healthy rows look like missing-metric failures.
- Min-player autofill now rotates through loaded first-party profiles rather
  than repeatedly choosing the smoke profile, continues processing while local
  simulation is paused, and honors the public `bot_min_players` naming.
- The scenario command builder now starts every isolated dedicated run with
  `bot_min_players 0`. That prevents local config or prior operator defaults
  from adding an extra autofill bot on top of staged smoke rows, while
  scenario-specific cvars can still override the baseline when needed.

## Scenario Coverage

The catalog now has `113` implemented rows, `0` pending rows, and highest
reserved bot frame-command mode `95`.

New or updated movement/context rows:

| Scenario | Mode | Map | Contract |
|---|---:|---|---|
| `movement_crouch_gap` | `92` | default | Keeps natural crouch as an explicit expected-blocked gap until an accepted route/map proves it. |
| `movement_swim_route` | `93` | `q2dm2` | Proves swim route ownership and swim movement command evidence on a water-backed reference map. |
| `movement_waterjump_route` | `94` | `q2dm2` | Proves waterjump route/movement diagnostics on the same liquid reference family. |
| `movement_teleporter_entity_route` | `95` | `train` | Follow-up 2026-06-29 promotion: exact Q3A `TRAVEL_TELEPORT` support stays unsupported, while runtime nav routes toward a touch-capable teleporter entity fallback. |
| `movement_door_context` | `91` | `base1` | Reuses the campaign interaction matrix to hard-gate door context, route-interaction candidates, and coop door/elevator command ownership. |

Hazard entities are classified and counted in runtime nav context, but a
dedicated accepted live hazard scenario remains a follow-up. Natural crouch is
also still an expected-blocked gap until a staged route proves the behavior.
Teleporter context is no longer an expected-blocked row after
`docs-dev/q3a-botlib-teleporter-entity-route-2026-06-29.md`.

## Validation

- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -q`
  passed `55` tests.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64`
  passed.
- `python tools\refresh_install.py --build-dir builddir-win --platform-id windows-x86_64 --package-q2aas-aas`
  refreshed `.install/`, packaged eight AAS maps, and passed staged payload
  audits.
- Focused movement/context validation passed `5/5` rows from
  `.tmp\bot_scenarios\movement_context_gap_rerun2\20260628T080154Z`.
- After the status-capture increase, the earlier eight aggregate failures were
  re-run and passed `8/8` from
  `.tmp\bot_scenarios\failed_rows_rerun_after_capture\20260628T080911Z`.
- The isolated chat team-policy regression caused by unpinned min-player
  autofill passed from
  `.tmp\bot_scenarios\bot_chat_team_policy_minplayers_isolation\20260628T081637Z`.
- The full implemented suite passed `113/113` rows with `0` failures,
  `0` timeouts, `0` errors, and `0` pending rows from
  `.tmp\bot_scenarios\implemented_movement_context_gap_rerun3\20260628T081648Z`.

## Follow-Up

- Add a staged hazard row once a slime/lava/hurt reference route has clear
  expectations and safe fallback behavior.
- Replace the expected-blocked natural crouch proof with an accepted route row
  when a suitable reference map or generated test map exists.
- Continue live play-depth passes for FFA, Duel, TDM, CTF, and coop to check
  that item/route/retreat ownership feels coherent outside short smoke runs.
