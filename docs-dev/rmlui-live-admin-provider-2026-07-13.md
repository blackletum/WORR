# RmlUi Live Admin Provider

Date: 2026-07-13

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T02`, `DV-03-T07`,
`DV-04-T02`, and `DV-07-T04`

## Outcome

`admin_menu` and `admin_commands` now truthfully declare version 2
`live-provider` status. The menu consumes the existing sgame-published
tournament Replay availability, opens the compiled command-reference route,
and dispatches the existing admin-only replay command. The command-reference
page remains intentionally read-only because the commands accept heterogeneous
console arguments.

The central migration phase remains `controller_stub` until connected replay
automation, localization/navigation review, and native renderer parity are
accepted.

## Provider and ownership contract

Sgame remains authoritative for admin authentication and tournament state.
`worr_admin_menu` and `worr_admin_commands` are registered `AdminOnly`
commands. The admin publisher sets `ui_admin_show_replay` from
`Tournament_IsActive()` before opening the menu, and the reference publisher
opens `admin_commands` through the normal route bridge.

The focused provider checker extracts the `AdminOnly` registrations from
`command_admin.cpp` and compares them with the RML `data-admin-command` rows.
The current catalog is exact: all 28 registered commands have one label,
summary, and matching `usage: <command>` line, with no missing or extra entry.
This prevents the static operator reference from silently drifting as the
server command surface changes.

## UX corrections

- Both routes now use their one canonical top-left backplate; duplicate footer
  Back buttons were removed.
- The Admin Menu copy explains the two available workflows rather than
  repeating the legacy server-start limitation.
- Replay appears only when sgame publishes tournament availability.
- The Admin Menu action block is compact instead of stretching through the
  full viewport.
- The command page explicitly tells operators to run commands from the console
  and supply the listed arguments.
- All 28 rows use a 632px bounded scroll region, 44px minimum row height,
  monospace command/usage text, wrapping descriptions, high-visibility
  coverage, and a permanently visible title/backplate at 960x720.

## Validation and evidence

Focused validation passes:

```text
python tools/ui_smoke/check_rmlui_admin_provider.py
python -m pytest -q tools/ui_smoke/test_check_rmlui_admin_provider.py
```

The eight focused regressions cover server registration drift, missing
reference rows, usage drift, Replay conditions, duplicate Back prevention,
metadata, and capture registration. The complete `tools/ui_smoke` suite passes
316 tests.

After the unrelated test process released the staged DLL, the canonical
`.install` distributable was refreshed successfully. It packaged 308 assets
and validated 214 RmlUi plus 31 bot package/loose files. Source and canonical
staged SHA-256 hashes match for both documents, `session.rcss`, and
`session/routes.json`.

Three clean 960x720 OpenGL captures are under
`.tmp/rmlui/runtime-capture/admin/`:

- `rmlui_admin_menu_live_provider_20260713`
- `rmlui_admin_menu_no_replay_live_provider_20260713`
- `rmlui_admin_commands_live_provider_20260713`

They cover tournament Replay shown, Replay unavailable, and the complete
scrollable reference. The final no-Replay frame uses a 240-frame settle after
manual QA rejected one transient early frame. All accepted frames preserve the
header/backplate and pass route, font, geometry, synthetic-input,
inactive-close, frame, route-counter, input-counter, and screenshot gates.
Final logs contain no warning, error, failed, or unknown-command hit.

## Remaining gates

- Automate connected Replay open, cancel, confirmation, and fixture restore.
- Add broad keyboard/gamepad focus, scrolling, and back-navigation assertions.
- Complete localization and large-text review for the long command catalog.
- Complete native Vulkan and RTX/vkpt RmlUi renderer parity without an OpenGL
  redirect.
