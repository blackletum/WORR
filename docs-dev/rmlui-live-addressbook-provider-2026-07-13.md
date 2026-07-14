# RmlUi Live Address Book Provider (2026-07-13)

Task IDs: `FR-09-T05`, `FR-09-T07`, `FR-09-T09`, `FR-03-T08`,
`DV-03-T07`, `DV-04-T02`, `DV-07-T04`

## Summary

The RmlUi `addressbook` route is now recorded and guarded as a live native
archived-cvar provider instead of a controller scaffold. The compiled generic
form bridge already provided the correct narrow ownership boundary, but route
metadata still claimed future cvar, command, and navigation wiring.

This slice verifies all 16 address fields end to end, makes provider ownership
truthful, clarifies immediate persistence and the Browse Favorites action,
adds a focused checker, and accepts current installed reduced-motion evidence.
The central migration phase remains `controller_stub` until native Vulkan/RTX
and broad action-level parity evidence is complete.

## Archived address contract

Client initialization creates `adr0` through `adr15` with `CVAR_ARCHIVE` and
the existing address completer. Each version-2 Address Book field:

- is a text input bound to its matching `adrN` cvar;
- hydrates through `UI_Rml_ApplyDocumentCvarBindings()` before first display;
- writes through the live form change listener with `FROM_MENU` ownership;
- refreshes any other presentation bound to the same cvar;
- preserves the legacy 32-character limit; and
- retains change feedback and ordinary keyboard/controller focus behavior.

Edits are immediate, matching the existing archived-cvar workflow, so no
Apply/Save action is needed. Empty fields remain valid. The layout keeps all
16 entries visible as four rows of four at 960x720. Address values use WORR's
monospace face at 13px, allowing common hostname/port values to remain readable
within the compact four-column contract.

## Favorites browsing

Browse Favorites preserves the complete legacy source set:

`pushmenu servers "favorites://" "file:///servers.lst" "broadcast://"`

The guarded route stack passes those arguments to the live server provider.
That provider identifies the route as Saved + LAN, reads the current archived
address book, parses `servers.lst`, and enables LAN broadcast discovery. Back
continues to use the shared guarded route stack.

## Validation and evidence

`tools/ui_smoke/check_rmlui_addressbook_provider.py` validates:

- creation of all address cvars as archived values with address completion;
- pre-show form hydration and live change-listener attachment;
- generic string writeback through the compiled cvar bridge;
- all 16 exact field IDs/cvars, text type, 32-character limits, and feedback;
- exact favorites/file/broadcast browse arguments and servers target;
- live server-provider recognition and address-book parsing;
- version-2 live-provider/controller identity;
- the four-column monospace visual contract; and
- guarded capture registration.

Six focused positive/negative tests reject a missing final address, lost
archive flag, shortened field limit, incomplete browse source set, and
scaffold identity regression.

Installed guarded OpenGL evidence is
`rmlui_addressbook_live_provider_20260713` at 960x720. The harness seeds:

- `adr0=127.0.0.1:27910`;
- `adr1=quake.example.org:27910`; and
- `adr15=[::1]:27910`.

The accepted reduced-motion frame visibly confirms all 16 labelled fields,
IPv4/hostname/IPv6 hydration, complete WORR chrome and Q2R TTF typography,
the Browse Favorites action, and an unclipped status bar. The log contains no
RmlUi parser, property, missing-media, warning, or error line and passes exact
route, geometry, font, synthetic input, and back-close gates.

The shared deterministic route-visibility correction discovered during this
evidence pass is documented separately in
`docs-dev/rmlui-deterministic-route-visibility-2026-07-13.md`.

Final automated verification for this slice is:

- `6 passed` in the focused Address Book provider suite;
- `267 passed` across `tools/ui_smoke`;
- 58/58 required route documents present;
- passing metadata sync, metadata shape, phase consistency, manifest, runtime
  asset, Address Book, and keybind/accessibility contract checks;
- a successful RmlUi-enabled Windows engine build; and
- a refreshed `.install/` containing the current binary and 308 packaged
  assets, including 214 RmlUi and 31 bot files.

## Remaining migration work

- Add non-destructive action automation that edits an address, verifies the
  cvar/config write, opens Browse Favorites, observes the row, and restores the
  prior value.
- Run final large-text, localization, controller-navigation, viewport, and
  native cross-renderer parity matrices.

No separate user guide is required: this completes and clarifies the existing
Address Book workflow without introducing a new command, cvar, or concept.
