# WORR Bot Surface Audit

`audit_bot_surface.py` checks the active bot cvar and command surface against
the Q3-style public contract. It scans source for bot cvar registrations/reads,
bot cvar writes, exported cvar declarations, and server command registrations.
It also scans `docs-user/` so legacy or smoke-prefixed public names do not creep
back into operator documentation. The audit also verifies
`docs-user/bot-cvars.md` documents every public bot cvar and its expected
default.

Run:

```powershell
python tools\bot_surface\audit_bot_surface.py
python tools\bot_surface\audit_bot_surface.py --format json --output .tmp\bot_surface\public_bot_surface_audit.json
python -m pytest tools\bot_surface\test_audit_bot_surface.py -q
```

The audit fails on active `sv_bot_*`, `sg_bot_*`, or `smoke_*` bot controls,
missing Q3-style commands (`addbot`, `removebot`, `kickbots`, `botlist`,
`bot_reload_profiles`), or public cvar default drift for the currently supported
operator controls. It also fails if a supported public bot cvar is absent from
user docs or missing from the public default table.
