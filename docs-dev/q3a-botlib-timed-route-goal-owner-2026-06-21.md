# Q3A BotLib Timed Route Goal Owner

Date: 2026-06-21

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass generalizes the brain-owned nuke retreat route into a reusable timed
route-goal owner. Nuke retreat remains the first consumer, but the command path
now has owner kind, activation, deferral, expiration, invalid-skip, source,
goal, and remaining-time status that future area-denial, tactical regroup, or
team-behavior slices can reuse.

The route behavior is unchanged for nuke use: after a submitted safe nuke
inventory request, the bot still receives a short-lived position goal away from
the remembered enemy or launch-direction fallback. The new work is the shared
owner state and telemetry around that behavior.

## Code Changes

- `bot_brain.cpp`
  - Adds `BotTimedRouteGoalKind` and `BotTimedRouteGoalState` to the brain
    command path.
  - Replaces the nuke-specific per-slot retreat fields with a generic timed
    route-goal state.
  - Applies active timed goals through `Bot_CommandApplyTimedRouteGoal(...)`
    before normal objective route construction.
  - Keeps nuke-specific counters while also emitting generic
    `timed_route_goal_*` and `last_timed_route_goal_*` frame-command fields.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the generic timed route-goal fields as a known optional
    frame-command status family.

## Follow-Up

This creates the owner surface for follow-on tactical consumers. Teleporter
escape and coop leader routing now use the same owner as additional concrete
kinds; future behavior can attach door/elevator waits, progression staging, or
area-denial avoidance to the same timed owner instead of creating another
bespoke route overlay.

## Validation

Commands run:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`
- `ninja -C builddir-win sgame_x86_64.dll`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `git diff --check`

Result: passed. Ninja printed the existing `premature end of file; recovering`
warning during build steps. The install refresh repacked `maps/mm-rage.aas` into
`.install/basew/pak0.pkz` with SHA-256
`6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
`git diff --check` reported only the existing CRLF normalization warnings.
