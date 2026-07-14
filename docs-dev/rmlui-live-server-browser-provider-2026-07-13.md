# RmlUi Live Server Browser Provider (2026-07-13)

Task IDs: `FR-09-T05`, `FR-09-T07`, `FR-09-T09`, `FR-03-T08`,
`DV-03-T07`, `DV-04-T02`, `DV-07-T04`

## Summary

The RmlUi `servers` route now owns a live Quake II server-browser provider.
It replaces the disabled placeholder table while retaining the established
WORR/Q2PRO source, refresh, ping, sort, selection, and connect behavior.

This slice covers both the truthful empty state and a real populated status
reply from a local dedicated server. It does not claim that the remaining
generic/session list providers or the native Vulkan/RTX RmlUi lanes are
complete.

## Routing and ownership

`pushmenu servers` can now carry the legacy source argument string through the
cgame command bridge, the client route fallback, and the RmlUi runtime. The
arguments are copied before provider parsing because the command tokenizer
uses shared argv storage. Failed route opens clear pending arguments so a
later route cannot inherit stale source state.

The client runtime remains the presentation and network-status consumer:

- `UI_StatusEvent()` and `UI_ErrorEvent()` first offer replies to the active
  RmlUi runtime and retain the legacy handler when RmlUi does not consume them;
- the optional runtime callbacks are appended to the existing runtime
  interface, keeping the bridge narrow;
- no `q2proto/` code is modified; and
- the existing cgame JSON route remains the guarded fallback when RmlUi cannot
  open.

The Play hub now passes the same public-source arguments as the existing Game
and Multiplayer entry points.

## Source and refresh behavior

The provider preserves the two useful source groups:

- Public reads the existing q2servers raw binary and text endpoints.
- Saved + LAN reads favorites, `servers.lst`, and the broadcast source.

Binary and text lists are parsed with format detection, address resolution,
duplicate suppression, privileged-port rejection, and a hard 1024-entry
limit. A successful raw endpoint does not depend on both endpoints being
available, and text content returned from the binary endpoint is handled
safely.

Refresh uses the existing `ui_pingrate` pacing contract and three status-query
stages. All scheduling now uses `Sys_Milliseconds()` so elapsed comparisons
and next-send timestamps share one clock. Replies hydrate hostname, mod, map,
population, round-trip time, and restriction state. Errors remain visible as
down/unreachable rows instead of silently disappearing.

`ui_server_source` records Public versus Saved + LAN, while `ui_sortservers`
retains the signed legacy column encoding. The five sortable columns are
Server, Mod, Map, Players, and RTT. Status sorts ahead of field values;
positive Players sorting keeps populated servers first, matching the useful
legacy behavior.

## Interaction and command safety

The document dispatches provider-owned `data-server-action` values for
refresh, source toggle, connect, paging, sorting, and row activation. Dynamic
rows contain only a numeric provider index.

Selection is explicit and visually persistent. A second activation or the
Connect toolbar action joins the selected server. Connect commands use the
resolved numeric address, not untrusted display text, and reject quotes,
semicolons, carriage returns, and newlines before command creation.

The provider hydrates before `ElementDocument::Show()` and updates only when
refresh state changes. Closing the route releases its live entry state.

## Layout and accessibility

The 960x720 contract uses:

- five focusable header sort buttons;
- eight bounded rows per page;
- visible Previous/Page/Next state;
- distinct pending, error, restricted, and selected treatments;
- a disabled Connect state until a row is selected; and
- truthful empty and live-count status copy.

The first installed capture revealed that the CTA padding left too little text
width and wrapped `CONNECT`. A route-specific 128px no-wrap rule corrects the
control without widening the complete toolbar beyond the 960px viewport.

## Capture harness hardening

The guarded capture tool now knows the `servers` document, forces the local
source after config loading, and accepts `--server-address` to seed `adr0` for
repeatable localhost evidence. It also requires the capture marker to name the
requested route instead of accepting any generic route marker.

Opening the provider can tokenize preserved source arguments, so
`ui_rml_runtime_capture_route` now copies its route id before the open call.
This prevents diagnostic text from reflecting overwritten shared argv data.

## Validation and evidence

The focused checker
`tools/ui_smoke/check_rmlui_server_browser_provider.py` validates the runtime
bridge, source formats, ping pacing, status/error callbacks, sorting, paging,
safe connect behavior, authored controls, ordered columns, and theme states.
Its four focused pytest cases include negative source, command-safety, and
placeholder/layout regressions.

The RmlUi-enabled Windows engine and cgame link successfully. The final
`.install/` refresh packages 308 asset files and validates 214 RmlUi files and
31 bot files.

Installed OpenGL evidence at 960x720 includes:

- `rmlui_server_local_final_20260713`: a complete, truthful Saved + LAN empty
  state with functional refresh/source/paging chrome; and
- `rmlui_server_populated_clean_20260713`: a real dedicated-server status
  reply showing `WORR RmlUi Evidence`, `basew`, `q2dm1`, `0/8`, and a 5ms RTT.

The populated run used a temporary private localhost server on port 27919,
which was stopped after capture. The final log names the `servers` route,
reports 132 update/render frames, validates the Q2R TTF path plus synthetic
keyboard/text/pointer/wheel input and back-close behavior, and contains no
unknown-command, RmlUi parse/runtime, assertion, or invalid-address errors.

Final automated verification for this slice is:

- `4 passed` in the focused server-provider suite;
- `22 passed` across the server-provider and runtime-capture suites;
- `239 passed` across `tools/ui_smoke`;
- 58/58 required route documents present with metadata, phase, and parity
  consistency checks passing;
- successful RmlUi-enabled Windows engine and cgame builds;
- a refreshed `.install/` with 308 assets, including 214 RmlUi files and 31
  bot files; and
- clean targeted `git diff --check` output apart from expected line-ending
  notices.

## Remaining work

- Add runtime interaction automation for row focus, second-activation,
  connect, each sort direction, paging, and source switching.
- Exercise public HTTP discovery in an opt-in network lane without making
  normal smoke tests internet-dependent.
- Implement the remaining generic and session list providers.
- Complete native Vulkan and RTX/vkpt RmlUi rendering and evidence without
  redirecting either path to OpenGL.
