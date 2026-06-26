# Q3A BotLib Escape and Deployable Inventory Policy

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass extends the carried inventory policy into two previously deferred
high-risk cases: doppelganger placement and personal teleporter escape use. The
action layer still routes all uses through the normal `UseInventory` decision
and validated `use_index_only` dispatch path; it does not add any legacy Q2R bot
callback or bypass the gameplay item callbacks.

The policy remains intentionally conservative. It proves space before asking for
doppelganger, treats personal teleporter as a last-resort deathmatch escape, and
left nuke use for the follow-on friendly-fire and blast-radius policy round.

## Policy

Doppelganger:

- Requires actionable enemy or survival pressure.
- Rejects ordinary combat use if the enemy is already point blank.
- Reuses the same `FindSpawnPoint(...)` and `CheckGroundSpawnPoint(...)`
  helpers that `Use_Doppelganger(...)` trusts, with the same forward placement
  shape.
- Scores survival placement above ordinary combat placement.

Personal teleporter:

- Requires deathmatch inventory semantics.
- Defers while carrying a CTF objective item.
- Requires critical health plus either shootable/close visible enemy pressure or
  immediate lava/slime hazard pressure.
- Scores below direct environment protection and major protection powerups, so
  it behaves as an escape rather than a routine combat buff.

Nuke:

- This escape/deployable round explicitly deferred nuke as
  `nuke_safe_policy_pending`.
- The follow-on safe-nuke round replaces that unconditional deferral with a
  conservative safety-gated combat utility policy.

## Code Changes

- `bot_actions.*`
  - Adds placement preflight helpers for doppelganger using the gameplay spawn
    search and ground-support checks.
  - Adds personal teleporter mode/objective/pressure gates.
  - Adds status counters for escape uses, placement checks, placement
    deferrals, and nuke deferrals.

- `bot_brain.cpp`
  - Emits the new inventory-policy counters in compact and detailed
    `q3a_bot_action_status` output.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the new counters in the optional inventory-policy field family.

## Follow-Up

The follow-on safe-nuke round accounts for blast radius, teammate exposure,
objective carriers, self-pressure, launch clearance, and enemy value before the
action layer requests the item. Broader command ownership, timed-goal route
ownership, and autonomous team/coop role consumption remain outside this slice.

## Validation

Commands run:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`
- `ninja -C builddir-win sgame_x86_64.dll`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- `git diff --check`

Result: passed. Ninja printed the existing `premature end of file; recovering`
warning during build steps, and `git diff --check` only reported the existing
CRLF normalization warnings.
