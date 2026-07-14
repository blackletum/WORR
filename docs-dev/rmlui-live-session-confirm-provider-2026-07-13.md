# RmlUi Live Session Confirmation Provider

Date: 2026-07-13

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T02`, `DV-03-T07`,
`DV-04-T02`, and `DV-07-T04`

## Outcome

`forfeit_confirm` and `leave_match_confirm` now truthfully declare version 2
`live-provider` status and run through the existing native RmlUi popup and
command paths. Both documents retain the shared destructive-confirmation
visual language and expose the safe No action before the destructive Yes
action.

The central migration phase remains `controller_stub` until connected
destructive-action automation, broad input/navigation validation, and native
renderer parity are accepted.

## Action and ownership contract

Forfeit remains server-authoritative. Sgame publishes `pushmenu
forfeit_confirm`, handles `worr_forfeit_yes`, launches the validated forfeit
flow, reports feedback, and closes the active menu. RmlUi only presents the
confirmation and dispatches the existing command.

Leave Match remains a client session action. Yes preserves the established
`forcemenuoff; disconnect` order, so the RmlUi route closes before the client
disconnect is queued. No, Escape, Mouse2, and the popup close path use
`popmenu`, returning to the underlying match hub without disconnecting.

The focused provider checker also locks both compiled route registries, popup
classification, the native command listener's `popmenu` and `forcemenuoff`
handling, the bounded connection-state helper, sgame publisher and command
registration, route metadata, and guarded capture registration.

## UX corrections

- No is authored first so initial keyboard/gamepad focus lands on the safe
  action instead of the destructive action.
- Yes uses the shared red destructive treatment; No uses the shared secondary
  treatment.
- The forfeit prompt explicitly states that the match ends and counts as a
  loss.
- The Leave Match fallback copy now matches the localization source and no
  longer claims that disconnecting returns to a lobby.
- Both dialogs use the centered popup scrim, bounded 420px panel, 38px action
  targets, destructive intent edge, focus styles, high-visibility coverage,
  and reduced-motion coverage from the shared theme.

## Validation and evidence

Focused validation passes:

```text
python tools/ui_smoke/check_rmlui_session_confirm_provider.py
python -m pytest -q tools/ui_smoke/test_check_rmlui_session_confirm_provider.py
```

The eight focused regressions cover the two action commands, close-before-
disconnect ordering, safe action order, popup back/cancel behavior, native
`forcemenuoff` handling, metadata, and capture registration. The complete
`tools/ui_smoke` suite passes 308 tests.

A complete temporary distributable was refreshed with:

```text
python tools/refresh_install.py --build-dir builddir-win \
  --install-dir .tmp/install-session-confirm-20260713
```

It packaged 308 assets and validated 214 RmlUi plus 31 bot package/loose files.
Two clean 960x720 OpenGL captures are under
`.tmp/rmlui/runtime-capture/session-confirm/`:

- `rmlui_forfeit_confirm_live_provider_20260713`
- `rmlui_leave_match_confirm_live_provider_20260713`

Both pass route, font, geometry, synthetic-input, inactive-close, frame,
route-counter, input-counter, and screenshot gates. Manual review accepted the
centered layout, wrapping, contrast, focus order, and destructive/secondary
hierarchy. Final logs contain no warning, error, failed, or unknown-command
hit.

After the unrelated test process released the staged DLL, the canonical
`.install` distributable was refreshed successfully. It packaged 308 assets
and validated 214 RmlUi plus 31 bot package/loose files. Source and canonical
staged SHA-256 hashes match for both confirmation documents and their shared
metadata/theme assets.

## Remaining gates

- Automate connected cancel, forfeit, disconnect, and fixture restoration.
- Add broad keyboard/gamepad focus-order and navigation assertions.
- Complete localization and large-text review across all supported languages.
- Complete native Vulkan and RTX/vkpt RmlUi renderer parity without an OpenGL
  redirect.
