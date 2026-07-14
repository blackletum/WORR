# RmlUi Live Two-Slot Keybind Provider (2026-07-13)

Task IDs: `FR-09-T05`, `FR-09-T07`, `FR-09-T09`, `FR-03-T08`,
`DV-03-T07`, `DV-04-T02`, `DV-07-T04`

## Summary

The RmlUi `keys`, `legacykeys`, and `weapons` routes are now recorded and
guarded as a live native keybind family. The earlier compiled controller could
capture a key, clear a command, and refresh a label, but it exposed only the
first key and always removed every existing binding before assignment. That
behavior was not compliant with the UX contract for paired Primary/Alternate
chips, conflict confirmation, capture timeout, or graphical device keys.

This slice completes the shared provider, replaces the three documents' mock
rows with live paired slots, accepts installed 960x720 OpenGL evidence, and
adds focused regression coverage. The central migration phase stays at
`controller_stub` because native Vulkan/RTX RmlUi rendering and broader
action-level navigation automation remain open.

## Native binding contract

Each authored command now owns two independently interactive slots:

- slot `0` is labelled Primary and slot `1` is labelled Alternate;
- `Key_EnumBindings()` hydrates both slots before the document is shown;
- selecting a slot starts capture without modifying the current bindings;
- Escape cancels capture and restores the original display;
- Backspace or Delete clears only the selected slot;
- assignment preserves the other displayed slot;
- assignment normalizes console-created extras back to the two-slot UI
  contract so a new binding cannot disappear behind an unrendered third key;
- capture times out after eight seconds without changing the configuration;
  and
- closing/back input cancels a pending conflict before it can leave the route.

The engine key table has no persistent Primary/Alternate ordering field, so
the chips reflect the first two bindings in key-number enumeration order. The
controller preserves the other visible key during replacement, which is the
strongest stable two-slot behavior the existing key subsystem can provide.

## Conflict confirmation

Before assignment, the controller reads the selected key's current command
with `Key_GetBindingForKey()`. A key bound to a different command does not
change either command immediately. Instead, the active binding row gains a
red conflict edge and expands an inline conflict row containing:

- the selected key and existing command;
- a Replace action that commits the overwrite while preserving the target
  command's other slot; and
- a Cancel action that dismisses the conflict without mutation.

Replace/Cancel use the same document-level event listener as the rest of the
compiled runtime. Keyboard focus moves to Replace when the conflict opens,
and Escape/Mouse2 dismisses the pending conflict rather than closing the menu.

## Device-aware key chips

The provider uses the established Q2R bind-icon mappings already used by
player hints:

- keyboard: `/gfx/controller/keyboard/<code>.png`;
- mouse: `/gfx/controller/mouse/f000X.png`; and
- gamepad: `/gfx/controller/generic/fXXXX.png`.

Every chip retains the engine key name as a compact text fallback. This keeps
unmapped keys readable and makes mapped keys identifiable even when an
external icon archive is unavailable. Capture temporarily replaces the chip
contents with `PRESS A KEY...` and a gold pulsing frame. Reduced-motion mode
inherits the shared accessibility rule that disables UI animations.

The reduced-motion evidence pass exposed a shared RmlUi timing defect: adding
the accessibility class after document parsing could cancel an entrance
opacity animation at its sampled invisible frame. Accessibility classes are
now inserted into the in-memory body markup before document construction and
kept synchronized on the document/body at runtime. A denser Address Book pass
proved cancellation itself remained unreliable, so decorative load-time fades
were removed while focus, progress, and active capture feedback remain. Routes
therefore load fully visible instead of intermittently losing chrome, headers,
content, or status bars. The shared correction is detailed in
`docs-dev/rmlui-deterministic-route-visibility-2026-07-13.md`.

## Route and layout completion

The three version-2 route documents now declare
`data-document-status="live-provider"` and
`data-controller="native-keybind-capture"`. Their complete source-menu
coverage is:

- Key Bindings: 19 commands / 38 slots across Combat, Inventory, Movement,
  and Interface;
- Legacy Key Bindings: 8 commands / 16 slots; and
- Weapon Bindings: 11 commands / 22 slots across Main Arsenal and Heavy
  Weapons.

The shared utility theme renders primary/alternate slab pairs right-aligned
inside each row, with gold capture state and red conflict state. The compact
37-pixel row rhythm keeps the largest Key Bindings page fully visible at
960x720, including the Legacy Keys action and status bar. Legacy and Weapon
Bindings no longer carry empty action footers.

## Validation and evidence

`tools/ui_smoke/check_rmlui_keybind_provider.py` validates:

- native two-slot enumeration, assignment, per-slot clear, and normalization;
- Escape cancellation, eight-second timeout, and conflict-before-replace;
- Replace/Cancel event handling and pending-conflict back handling;
- keyboard, mouse, and gamepad icon path construction;
- exact legacy command coverage across all 38 rows;
- paired `0`/`1` slots, icon plus text fallback, and an inline conflict row
  for every command;
- live-provider route identity and controller ownership;
- capture/conflict/layout state styling;
- pre-load reduced-motion/high-visibility state plus completed visual opacity;
  and
- guarded runtime-capture registry coverage for all three routes.

Nine focused positive/negative tests reject a single-slot regression,
destructive replacement, missing timeout, missing conflict cancellation, and
lost gamepad artwork mapping, interrupted reduced-motion opacity, and late
accessibility-class application, including restoration of unreliable route
entrance animation declarations.

Installed guarded OpenGL evidence at 960x720 is:

- `rmlui_keys_live_provider_20260713`;
- `rmlui_legacykeys_live_provider_20260713`; and
- `rmlui_weapons_live_provider_20260713`.

The frames visibly confirm paired populated/empty chips, Q2R keyboard/mouse/
gamepad artwork, complete route content, shared WORR focus/chrome, Q2R TTF
typography, and unclipped footer/status controls. All three logs are free of
RmlUi parser, property, missing-media, warning, and error lines. The capture
harness also confirms route open, exact geometry, text generation, synthetic
keyboard/text/pointer/button/wheel input, and clean back-close behavior.

Final automated verification for this slice is:

- `9 passed` in the focused keybind-provider suite;
- `267 passed` across `tools/ui_smoke` after the immediately following
  Address Book/visibility closure;
- 58/58 required route documents present;
- passing metadata sync, metadata shape, phase consistency, manifest, runtime
  asset, and keybind-provider checks;
- a successful RmlUi-enabled Windows engine build; and
- a refreshed `.install/` containing the current binary and 308 packaged
  assets, including 214 RmlUi and 31 bot files.

## Remaining migration work

- Add non-destructive action automation that captures, clears, conflicts,
  cancels, replaces, and restores real bindings through the rendered controls.
- Run final large-text, localization, controller-navigation, viewport, and
  native cross-renderer parity matrices.
- Implement native Vulkan and RTX/vkpt RmlUi bridges without redirecting them
  to OpenGL before advancing the central migration phase.

No separate user guide is required: this completes the existing keybinding
workflow and does not introduce a new command, cvar, or player-facing concept.
