# RmlUi Live Demo Browser Provider (2026-07-13)

Task IDs: `FR-09-T07`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Summary

The RmlUi `demos` route now presents live filesystem data and actionable
controls. It no longer advertises a future browser through disabled toolbar
buttons and a static empty row.

This slice migrates the established demo discovery and playback behavior. It
does not claim that the server/session providers or the complete renderer
matrix are finished.

## Provider behavior

- Discovery uses the legacy `.dm2`, `.dm2.gz`, `.mvd2`, and `.mvd2.gz`
  extension set.
- Directory discovery uses `FS_SEARCH_DIRSONLY`; demo rows use
  `FS_SEARCH_EXTRAINFO` so date and size are based on engine filesystem
  metadata.
- `ui_listalldemos=0` keeps discovery on real files in the active game path.
  The toolbar can switch to the existing all-location behavior without
  replacing or renaming the compatibility cvar.
- `CL_GetDemoInfo()` remains the authority for map, POV, and MVD metadata.
- Parsed metadata is cached for the RmlUi runtime session by normalized path,
  file size, and modification time. Refreshes and directory revisits reuse
  unchanged metadata while changed files are reparsed.
- The current directory persists across route reopens, matching the legacy
  browser's useful navigation behavior.
- A bounded 4096-entry hydration limit prevents an unbounded static DOM. The
  status row explicitly reports truncation, while eight-row paging keeps the
  960x720 table stable and keyboard-traversable.

## Interaction and command safety

The runtime owns all demo actions through `data-demo-action`. Dynamic rows put
only a validated numeric provider index in markup; filenames never enter a
`data-command` attribute.

Activation resolves the current provider entry and then:

- leaves or enters a directory;
- refreshes the live listing;
- moves between pages;
- toggles the source cvar;
- changes `ui_sortdemos`, preserving the legacy signed column encoding; or
- queues `demo "<path>"` for a validated file.

Names containing a quote, semicolon, carriage return, or newline are rejected
before path navigation or command creation. Path length is checked against
`MAX_OSPATH`. This preserves the legacy injection guard while also rejecting
carriage returns.

## Table and accessibility contract

The document now exposes the legacy useful columns: Name, Date, Size, Map,
and POV. Each header is a real focusable sort button. Each visible entry has
one focusable name action rather than five redundant cell controls.

The shared utility theme provides:

- the design-language 36px row slab;
- 12px uppercase sort labels;
- brass hover/focus treatment;
- the existing slime-green selected edge contract;
- five-column widths appropriate to metadata; and
- visible page state between Previous and Next controls.

Provider hydration runs before `ElementDocument::Show()`, so the first visible
frame contains the final rows, cvar state, sort state, status totals, and
disabled paging/up states.

## Validation

`tools/ui_smoke/check_rmlui_demo_browser_provider.py` and its four focused
pytest cases validate:

- legacy extensions, filesystem flags, metadata parsing, cache use, paging,
  sorting, source toggling, safe activation, and pre-show hydration;
- live toolbar actions and the five ordered columns;
- rejection of placeholder/disabled regressions and command-safety drift; and
- table action, page-label, row-height, and selected-edge theme contracts.

The changed RmlUi runtime translation unit compiles and the RmlUi-enabled
Windows engine links successfully. The shared tree changed underneath the
first build attempt while adaptive-input work was being integrated; after its
Meson source wiring completed, a regenerated build consumed that work and
linked the current demo-provider engine normally.

The populated route also has current installed-runtime evidence. Two
consecutive guarded OpenGL captures at 960x720 produced byte-identical TGA
files (`rmlui_demo_motionproof_a_20260713` and
`rmlui_demo_motionproof_b_20260713`). The captured local provider contained 38
demos across five pages and reported 143 MB total. Visual inspection confirms
that the full shared chrome, one-line source toggle, five headers, eight complete
rows, paging controls, footer, and status line fit without clipping. Both runs
exited normally and reported route, frame, TTF/Q2R font, synthetic keyboard,
text, pointer, wheel, and back-close markers without RmlUi syntax or runtime
errors.

The capture harness now reapplies `ui_rml_reduced_motion=1` through a direct
console command immediately before opening the route. This is deliberately
after startup/config loading: archived user config can otherwise overwrite the
early command-line cvar and leave the screenshot timing dependent on the
shared-chrome entrance animation.

Verification for this slice is:

- `21 passed` in the focused provider and runtime-capture harness suites;
- `234 passed` across `tools/ui_smoke`;
- a successful RmlUi-enabled Windows engine link;
- refreshed `.install/` contents with 308 files, including 214 RmlUi assets and
  31 bot files; and
- the two byte-identical populated-route captures described above.

## Remaining work

- Capture and inspect a deterministic empty-state route with seeded fixtures.
- Exercise directory, sort, paging, source, and playback actions with seeded
  demo fixtures in a runtime automation lane.
- Implement the server and remaining session list providers.
- Close native Vulkan/RTX RmlUi renderer evidence without redirecting either
  path to OpenGL.
