# RmlUi Live MyMap Provider

Date: 2026-07-13

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T01`, `FR-07-T02`,
`DV-03-T07`, `DV-04-T02`, and `DV-07-T04`

## Outcome

`mymap_main` and `mymap_flags` now truthfully declare `live-provider` status
and consume the existing sgame-owned MyMap state through the native RmlUi cvar,
condition, label, and command bridge. The generic live `ui_list` provider
continues to present map selection and dispatch queue commands.

The central migration phase remains `controller_stub` until connected action
automation and native renderer parity are accepted.

## Provider and ownership contract

The focused provider check locks fifteen published values:

- MyMap availability/status text;
- the current map-flag summary;
- Select Map availability;
- Flags availability;
- Clear Flags availability; and
- ten tri-state flag labels.

Sgame remains authoritative for tournament restrictions, server enablement,
login requirements, map pool contents, flag state, queue policy, success or
failure, and refreshes. RmlUi only presents the published values and dispatches
the six existing `worr_mymap_*` commands. Successful queueing still clears the
temporary flags and closes the active menu through the sgame command flow.

## UX corrections

- Both routes use one canonical backplate; duplicate footer Back actions were
  removed.
- Select Map is the canonical primary action.
- Select Map, Flags, and Clear Flags use live enabled conditions. Disabled
  controls leave focus navigation through the shared condition bridge.
- The main route uses a compact bounded status slab instead of stretching an
  empty panel through the viewport.
- The flag editor reuses the accessible two-column 36px choice grid and shows
  all ten Default/Enabled/Disabled values without scrolling at 960x720.

## Validation and evidence

`tools/ui_smoke/check_rmlui_mymap_provider.py` passes for two routes and fifteen
published values. Its eight focused regression tests cover publication,
enabled state, duplicate-back prevention, tri-state labels, the two-column
layout, metadata, and capture registration. The complete `tools/ui_smoke`
suite passes 300 tests.

Canonical staging was refreshed with:

```text
python tools/refresh_install.py --build-dir builddir-win
```

The final refresh packaged 308 assets and validated 214 RmlUi plus 31 bot
package/loose files. Source and `.install` hashes match for both documents,
`session.rcss`, and session route metadata.

Three clean canonical `.install` 960x720 OpenGL captures are under
`.tmp/rmlui/runtime-capture/mymap/`:

- `rmlui_mymap_main_live_provider_20260713`
- `rmlui_mymap_main_unavailable_live_provider_20260713`
- `rmlui_mymap_flags_live_provider_20260713`

The montage is
`.tmp/rmlui/runtime-capture/mymap/rmlui_mymap_live_provider_20260713.png`.
The frames cover an enabled request with flags, a login-gated request with all
actions disabled, and all ten tri-state flag labels. Logs contain no warning,
error, failed, or unknown-command hit.

## Remaining gates

- Automate connected Select Map, flag mutation/clear, map queue, and fixture
  restoration.
- Add broad keyboard/gamepad focus-order and navigation assertions.
- Complete large-text and localization review for server-published status.
- Complete native Vulkan and RTX/vkpt RmlUi renderer parity without an OpenGL
  redirect.

