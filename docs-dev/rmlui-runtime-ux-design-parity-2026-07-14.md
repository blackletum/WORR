# RmlUi Runtime UX and Design Parity (2026-07-14)

Tasks: `FR-09-T04`, `FR-09-T09`

## Outcome

All 58 registered WORR menus are accepted as `parity_ready`. They open through
the installed RmlUi runtime, use the live command/cvar/provider bridge, follow
the canonical WORR UX/UI design language, render natively in OpenGL, Vulkan,
and RTX/vkpt, and retain predictable mouse, keyboard, controller, text-entry,
Escape, and Back behavior.

The acceptance baseline is defined by:

- `docs-dev/worr-ux-ui-design-language-2026-07-12.md`
- `docs-dev/rmlui-grimy-metal-theme-2026-07-11.md`
- `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Design-language compliance

The route set was checked against the canonical page archetypes, persistent
chrome, 960x720 logical canvas, spacing rhythm, typography hierarchy, semantic
color roles, metal-skin assets, interaction states, modal intent, responsive
containment, and graceful-degradation requirements.

Accepted behavior includes:

- the same top bar, title/backplate position, tab treatment, and status bar on
  standard pages;
- authored Hero, Card select, Hub, Form, Table, Editor, Dialog, and Session hub
  structures without content-flow overlays;
- slime green for primary action, gold for focus/selection, orange for live
  match state, red for destructive action, and teal for system feedback;
- rerelease-derived display/UI/mono font services, bounded long-string layout,
  and localizable whole-string elements;
- minimum readable controls, explicit focus treatments, menu feedback audio,
  and input that remains live during visual transitions;
- flat-color and high-visibility fallbacks beneath textured chrome;
- responsive reference-canvas scaling for 4:3, widescreen, and ultrawide
  output, with bounded scroll regions on dense pages;
- deterministic dialogs with a safe first action and Escape/Back parity.

The final three-renderer contact-sheet review covered all 58 routes, not a
representative subset. It specifically caught and drove fixes for the RTX
backdrop sampler/color defects and the native Vulkan/RTX Player Setup preview.

## Functional runtime services

`src/client/ui_rml/ui_rml_runtime.cpp` provides the shared live behavior used
by the documents:

- typed cvar controls and live cvar-to-text/image/progress/meter bindings;
- command, command-cvar, route, popup, and close dispatch;
- visibility/enabled conditions and dynamic data-model refresh;
- live server, demo, save/load, keybind, address-book, list, player-preview,
  and multiplayer/session providers;
- first-focus selection, directional focus traversal, activation, text input,
  pointer capture, wheel input, and Escape/Back handling;
- menu music plus open, move, change, alert, and cancel feedback;
- localization refresh and accessibility class application without re-opening
  a route.

The 58 routes contain 1,123 localizable leaf strings and 1,123 localization
hooks. Those hooks resolve to 691 unique RML keys, while the English catalog
contains 3,444 keys. Static inventory and design-compliance checks report zero
missing or malformed hooks.

Accessibility services are live rather than metadata-only:

- `ui_rml_high_visibility` applies the white-on-black/yellow-focus treatment;
- `ui_rml_large_text` increases text and target sizes;
- `ui_rml_reduced_motion` removes authored animations and transitions;
- `loc_language` changes are consumed by the open document;
- keyboard and gamepad users receive a deterministic first focus target and
  can traverse every route without pointer movement stealing focus.

## Parity and cutover records

`tools/ui_smoke/rmlui_manifest.json` and every feature `routes.json` now mark
all 58 routes `parity_ready`. `tools/ui_smoke/rmlui_parity_manifest.json`
records complete evidence for all nine required categories on every route:

1. document load;
2. navigation;
3. controller bindings;
4. OpenGL rendering;
5. native Vulkan rendering;
6. native RTX/vkpt rendering;
7. screenshot/layout review;
8. Escape/Back input;
9. guarded legacy fallback.

The legacy-removal gate is open. The old JSON/menu sources are intentionally
retained as an archived compatibility and recovery reference for now. They are
not the normal presentation runtime, and their presence does not create a
second active visual design. Deleting those sources is optional cleanup under
`FR-09-T10`; retaining the guarded fallback reduces recovery risk while the
project is under active development and satisfies Gate G4's documented-archive
alternative.

## Validation

Key accepted checks include:

- 58/58 routes in each final OpenGL, Vulkan, and RTX installed-tree sweep;
- 14/14 runtime UX services;
- 58/58 parity-ready routes with 522/522 category-route evidence entries
  complete;
- exact central/feature metadata sync for all 58 routes;
- an open legacy-removal gate with no blocked inventory items;
- the complete `tools/ui_smoke` pytest suite (`362 passed`);
- successful OpenGL, Vulkan, RTX shader, and RTX renderer builds;
- refreshed and validated `.install/` runtime/assets.

Runtime capture manifests and contact sheets are scratch artifacts under
`.tmp/rmlui/runtime-capture/`; this document and the parity manifest provide
the durable acceptance record.

The full engine DLL link is presently blocked by unrelated networking symbols
from concurrent work. That does not affect the successfully rebuilt renderer
DLLs or the installed runtime used for the three final route sweeps.
