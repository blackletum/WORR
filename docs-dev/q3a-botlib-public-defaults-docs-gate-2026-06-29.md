# Q3A BotLib Public Defaults Docs Gate

Date: 2026-06-29

Task IDs: `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

## Summary

This round implements the M8 public defaults/docs release pass as an executable
gate. The public bot surface audit now verifies every supported public bot cvar
has a source default and a matching user-facing default row, so release docs
cannot silently drift from the actual operator surface.

## Implementation

- Extended `tools/bot_surface/audit_bot_surface.py`.
  - Adds `PUBLIC_CVAR_DEFAULTS` for all public bot cvars.
  - Validates source defaults for the full public set, including chat controls
    and `bot_name_prefix`.
  - Scans `docs-user/` for public cvar mentions.
  - Requires `docs-user/bot-cvars.md`.
  - Requires a default table row for every public bot cvar.
  - Preserves the existing legacy-prefix and smoke-token user-doc leak checks.
- Extended `tools/bot_surface/test_audit_bot_surface.py`.
  - Adds complete public-surface fixtures.
  - Adds coverage for missing public default rows.
  - Keeps forbidden active-source and user-doc legacy-prefix regressions.
- Added `docs-user/bot-cvars.md`.
  - Lists every supported public bot cvar, default, and operator-facing use.
  - Includes a common practice-server setup.
  - Documents only public `bot_` controls.
- Linked the cvar reference from `docs-user/README.md`, `docs-user/bots.md`,
  and `docs-user/server-quickstart.md`.
- Extended `tools/bot_release/run_bot_acceptance.py` to require
  `docs-user/bot-cvars.md`.

## Public Defaults

The public default table now covers:

- `bot_enable`
- `bot_min_players`
- `bot_profile`
- `bot_skill`
- `bot_behavior_enable`
- `bot_allow_item_timers`
- `bot_item_timer_fuzz_ms`
- `bot_allow_rocketjump`
- `bot_allow_chat`
- `bot_chat_live_events`
- `bot_chat_min_interval_ms`
- `bot_chat_team_only`
- `bot_name_prefix`

## Validation

Passed:

```powershell
python -m pytest tools\bot_surface\test_audit_bot_surface.py -q
python tools\bot_surface\audit_bot_surface.py --format json --output .tmp\bot_surface\public_bot_surface_audit.json
python tools\bot_release\run_bot_acceptance.py --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --format json --output .tmp\bot_release\bot_release_acceptance.json
```

No native executable build is part of this documentation/tooling slice.
