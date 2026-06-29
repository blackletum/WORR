# Q3A BotLib Public Bot Surface Audit

Date: 2026-06-29

Tasks: `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

## Summary

This round adds a repeatable WORR-native audit for the bot-facing cvar and
command contract. The immediate goal is to prevent the old prefixed bot
controls from returning and to keep smoke-only developer hooks out of public
operator documentation.

The current public command surface is Q3-style:

- `addbot`
- `removebot`
- `kickbots`
- `botlist`
- `bot_reload_profiles`

The current required public cvar defaults are:

| Cvar | Default |
|---|---|
| `bot_enable` | `1` |
| `bot_min_players` | `0` |
| `bot_profile` | empty |
| `bot_skill` | `3` |
| `bot_behavior_enable` | `1` |
| `bot_allow_item_timers` | `1` |
| `bot_item_timer_fuzz_ms` | `0` |
| `bot_allow_rocketjump` | `0` |
| `bot_allow_chat` | `0` |

The audit deliberately allows active source to retain `bot_*_smoke` developer
hooks, but it fails if those names appear in `docs-user/`. It also fails on
active source or user docs that reintroduce `sv_bot_*`, `sg_bot_*`, `smoke_*`,
or `smoke_bot_*` bot controls.

Follow-up update: `docs-dev/q3a-botlib-public-defaults-docs-gate-2026-06-29.md`
extends this audit to validate all 13 public bot cvar source defaults and the
matching `docs-user/bot-cvars.md` default rows.

## Implementation

- Added `tools/bot_surface/audit_bot_surface.py`.
  - Scans active server and sgame bot source for `Cvar_Get`, `gi.cvar`,
    `Cvar_Set`, exported cvar declarations, `Cmd_AddCommand`, and bot command
    table registrations.
  - Classifies bot cvars as public, smoke-only, debug, experimental, forbidden,
    or other.
  - Validates required public cvars, their expected defaults, required Q3-style
    commands, forbidden source prefixes, and forbidden `docs-user/` tokens.
  - Supports text and JSON output.
- Added `tools/bot_surface/test_audit_bot_surface.py`.
  - Covers the current repository surface.
  - Verifies active-source `sv_bot_*` regressions fail.
  - Verifies user-doc legacy and smoke-only cvar leaks fail.
  - Locks the classification buckets and JSON report shape.
- Added `tools/bot_surface/README.md`.
- Wrote the current JSON report to
  `.tmp\bot_surface\public_bot_surface_audit.json`.

## Result

The current source audit reports:

- 94 bot cvars scanned.
- 5 bot commands registered.
- Classification counts: 13 public, 37 smoke-only, 5 debug, 39 experimental.
- 0 violations.
- 0 warnings.

This confirms the active implementation exposes the canonical `bot_*` public
namespace and the Q3-style command names. The follow-up public defaults/docs
gate now covers the release default table; the remaining M8 work is a
post-build acceptance run and manual playtest evidence, not another prefix
migration.

## Validation

Passed:

```powershell
python -m py_compile tools\bot_surface\audit_bot_surface.py tools\bot_surface\test_audit_bot_surface.py
python -m pytest tools\bot_surface\test_audit_bot_surface.py -q
python tools\bot_surface\audit_bot_surface.py --format json --output .tmp\bot_surface\public_bot_surface_audit.json
```
