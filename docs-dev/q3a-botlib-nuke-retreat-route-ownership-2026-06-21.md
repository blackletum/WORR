# Q3A BotLib Nuke Retreat Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds command-owned retreat routing after a bot successfully submits a
safe nuke inventory request. The previous safe-nuke policy decided whether
`IT_AMMO_NUKE` was worth using; this round gives the brain a short-lived route
owner for the aftermath so the bot starts moving away from the area-denial
source on subsequent command frames.

The gameplay item callback remains authoritative. `Use_Nuke(...)` still spends
the inventory item and spawns the real `className="nuke"` projectile through
`fire_nuke(...)`; the bot brain only observes the submitted inventory command
request and reacts by owning a temporary position route goal.

## Behavior

- A successful submitted `UseInventoryIndex` request for `IT_AMMO_NUKE` arms a
  six-second retreat window on the bot's brain slot.
- The retreat source prefers the current blackboard enemy from the frame that
  selected the nuke. If no live remembered enemy is available, the route falls
  back to the launch direction and treats the point ahead of the bot as the
  pressure source.
- While active, and only when no explicit debug position goal or travel-type
  goal is already requested, the brain overlays a position route goal roughly
  1024 units away from the source.
- The route goal is recomputed from the bot's current origin on each active
  command frame, so the bot continues to bias away instead of running toward a
  single stale coordinate.
- Expiration clears the per-bot retreat owner without touching normal item
  reservations, objective helpers, or weapon/inventory scoring.

## Code Changes

- `bot_brain.cpp`
  - Adds per-bot nuke retreat state to the brain blackboard slot.
  - Arms retreat state after an accepted, submitted nuke inventory command.
  - Applies active retreat state as a position route goal during route request
    construction on later frames.
  - Emits frame-command status counters for retreat activations, fallback
    source use, route requests, debug/travel deferrals, expirations, invalid
    skips, and the last source/goal/remaining-time metadata.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers `nuke_retreat_*` and `last_nuke_retreat_*` fields as a known
    optional frame-command status family.

## Follow-Up

This is still a local command-owner, not a full tactical planner. Later work can
make timed goal ownership more generic, reserve area-denial lanes for teammates,
and coordinate nuke-like item use with live FFA/TDM/CTF role policy.

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
