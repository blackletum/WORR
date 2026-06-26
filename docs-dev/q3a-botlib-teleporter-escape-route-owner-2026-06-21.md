# Q3A BotLib Teleporter Escape Route Owner

Date: 2026-06-21

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds a second consumer for the generic timed route-goal owner:
last-resort personal teleporter escape. When the action layer submits a
validated `IT_TELEPORTER` inventory request, the gameplay item still performs
the authoritative random-spawn teleport. The bot brain then arms a short timed
route goal so the bot keeps moving away from the pressure source on subsequent
command frames.

This makes the timed route owner more than a nuke-specific wrapper. Nuke retreat
and teleporter escape now share the same owner state, route overlay, deferral,
expiration, and last-owner status path.

## Behavior

- A submitted teleporter inventory request arms a 3.5-second timed route owner
  of kind `teleporter_escape`.
- The escape source prefers the remembered combat enemy, then a recent damage
  origin, then the launch/view-direction fallback.
- Active escape state overlays a temporary position goal through the generic
  timed route owner unless debug position/travel-type goals already own the
  frame request.
- Nuke retreat remains on its existing six-second owner and continues to emit
  nuke-specific counters.

## Code Changes

- `bot_brain.cpp`
  - Adds `TeleporterEscape` as a `BotTimedRouteGoalKind`.
  - Generalizes timed owner distance/min-direction metadata.
  - Adds recent damage-origin selection for escape-route sources.
  - Arms `teleporter_escape` after submitted `IT_TELEPORTER` inventory use.
  - Emits `teleporter_escape_*` frame-command status counters alongside the
    generic `timed_route_goal_*` owner fields.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the teleporter escape route counters as a known optional
    frame-command status family.

## Follow-Up

The next useful timed-owner consumers are coop interaction flavored:
door/elevator wait behavior, progression staging, and anti-blocking routes that
need a short command-owned route goal without becoming a permanent item or
objective reservation.

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
