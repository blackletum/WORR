# Q3A BotLib Coop Leader Route Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds the first coop-flavored consumer for the generic timed route-goal
owner. The existing coop objective policy already classifies follow, regroup,
and support-combat intents around a selected leader. The bot brain now consumes
those policy results by arming a short `coop_leader` timed route owner when a
valid coop leader route is useful.

The bridge stays conservative: it does not replace objective selection, door
cooperation, progression waiting, or resource sharing. It turns one clear coop
policy result into temporary route ownership with telemetry, while leaving nuke
retreat and teleporter escape as higher-priority emergency owners.

## Behavior

- Valid coop follow/regroup/support policy can arm a 2.5-second timed route
  owner of kind `coop_leader`.
- Follow and regroup synthesize a source behind the bot on the leader vector so
  the shared timed owner moves the bot toward the leader.
- Support-combat intent uses the leader as the source and routes a short spacing
  move away from the leader.
- Active nuke retreat or teleporter escape owners are not overwritten by coop
  leader routing.

## Code Changes

- `bot_brain.cpp`
  - Adds `CoopLeader` as a `BotTimedRouteGoalKind`.
  - Adds coop leader route activation, refresh, source-selection, owner-deferral,
    invalid-skip, and last-leader metadata counters.
  - Consumes `BotObjectives_EvaluateCoopPolicy(...)` during frame policy
    evaluation and arms the timed owner before route selection.
  - Emits `coop_leader_route_*` and `last_coop_leader_route_*` frame-command
    status fields.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the coop leader route counters as a known optional frame-command
    status family.

## Follow-Up

Door/elevator cooperation and trigger-detected progression are still separate
coop behavior slices. The follow-on progress-wait command proof covers a
default-off WaitForLeader stop-and-face command, while mover, trigger, or
wait-point ownership still needs map-specific route policy.

## Validation

Commands run:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`
- `ninja -C builddir-win sgame_x86_64.dll`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_match_readiness --binary .install/worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --timeout 60 --format text --artifact-dir .tmp/bot_scenarios/coop-leader-route-owner`
- `git diff --check`

Result: passed. Ninja printed the existing `premature end of file; recovering`
warning during build steps. The install refresh repacked `maps/mm-rage.aas` into
`.install/basew/pak0.pkz` with SHA-256
`6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
`git diff --check` reported only the existing CRLF normalization warnings.
The refreshed-install `coop_match_readiness` smoke passed with
`route_failures=0`, `coop_leader_route_activations=16`,
`coop_leader_route_refreshes=14`, `last_timed_route_goal_kind=3`, and
`last_coop_leader_route_intent_name=support_combat`.
