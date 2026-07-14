# RmlUi Live Player Setup Provider (2026-07-13)

Task IDs: `FR-09-T05`, `FR-09-T07`, `FR-09-T09`, `FR-03-T05`,
`FR-03-T08`, `DV-03-T07`, `DV-04-T02`, `DV-07-T04`

## Summary

The RmlUi `players` route is now recorded and guarded as a live native Player
Setup provider instead of a controller scaffold. The client already contained
model/skin enumeration, immediate userinfo updates, dogtag discovery, and a
single-entity 3D preview, but its route document and project metadata still
claimed mock bindings and a future preview controller.

This slice reconciles that ownership, completes the established preview
behavior, adds truthful provider states and live appearance thumbnails, fixes
the OpenGL RmlUi bridge for legacy scrap-atlas images, and accepts current
installed evidence. It does not promote the central migration phase because
native Vulkan/RTX RmlUi rendering and the broad navigation matrix remain open.

## Live configuration contract

Player Setup hydrates before the document is shown:

- `players/*/tris.md2` directories are enumerated through the engine
  filesystem;
- only skins with a matching `_i.pcx` icon are offered;
- `male` and `female` remain first, followed by case-insensitive alphabetical
  order;
- the composite `skin` userinfo value is split into model and skin selects;
- changing a model rebuilds the skin list, preserving the skin name when the
  new model provides it;
- every model/skin change immediately writes the composite `model/skin` value;
- Name, Dogtag, and Weapon Hand use the generic live cvar bridge; and
- the 15-character player-name limit and hand values `0`/`1`/`2` remain
  compatible with the existing client contract.

Immediate persistence means the route does not need an Apply button. The old
empty actions footer was removed, and the document-aware top backplate remains
the single visible back action.

The Dogtag select enumerates file stems from `tags/`. A compact appearance
strip follows the live composite `skin` and `dogtag` cvars through
`data-src-cvar`, showing the selected skin portrait and dogtag image without a
route-specific image controller.

## Preview completion

The native client preview still renders after the RmlUi document into the
authored `players-preview-surface` box through `R_RenderFrame()` and
`RDF_NOWORLDMODEL`. It now matches the established player-page behavior more
closely:

- stand, run, pain, attack, crouch stand, crouch attack, and death stages;
- per-stage frame rates, loop counts, and the death hold;
- discovery of `weapon.md2` and sorted `w_*.md2` attachments;
- weapon cycling at the configured transition stage;
- synchronized player and weapon frames, origin, scale, yaw, and interpolation;
- short-lived muzzle dynamic lights on firing frames; and
- reduced-motion behavior that freezes animation, rotation, and muzzle flash.

The first installed capture exposed clipping when larger weapon attachments
were viewed through the previous 40-degree camera. The final preview uses a
55-degree horizontal field of view, which keeps the complete side-on
player/weapon silhouette inside the wide panel while retaining a large useful
model view.

Authored loading, empty, and error copy now covers synchronous hydration,
missing compatible models, and media registration failure. Model and Skin are
disabled in the empty case, while Name, Dogtag, and Hand remain ordinary live
controls.

## OpenGL cached-image bridge

Legacy player icons and dogtags can already be cached as small `IT_PIC`
images in the OpenGL scrap atlas before RmlUi requests them. `IF_NOSCRAP`
prevents new atlas placement but cannot relocate an existing cached image.
The bridge previously rejected that situation, which produced blank white
thumbnail rectangles.

`R_RmlUiTexture` now retains `sl`/`sh`/`tl`/`th`. `LoadTexture()` records the
source image's atlas rectangle, and `RenderGeometry()` remaps RmlUi's normalized
texture coordinates into that rectangle while copying vertices to the 2D
tessellator. Standalone and generated textures keep the default `0..1`
rectangle. Shared atlas textures are not owned or deleted by RmlUi.

This is an OpenGL-native correction to the active bridge. It does not redirect
or claim implementation of the inactive Vulkan or RTX/vkpt RmlUi paths.

## Validation and evidence

`tools/ui_smoke/check_rmlui_player_setup_provider.py` validates:

- player directory, icon-paired skin, and legacy ordering logic;
- composite selection initialization and immediate writeback;
- generic Name, Dogtag, and Hand cvar binding;
- pre-show provider hydration and event-listener attachment;
- all seven animation stages, weapon discovery, attached entity count, muzzle
  dlight, reduced-motion checks, and post-document preview rendering;
- authored live-provider identity and loading/empty/error states;
- the skin/dogtag thumbnail source contract;
- absence of a scalar fake model cvar and empty actions footer;
- bounded form/preview layout; and
- cached scrap-atlas UV preservation in the OpenGL RmlUi bridge.

Five focused negative/positive tests cover the accepted provider and reject a
scalar model binding, missing empty state, lost reduced-motion behavior, and
an empty footer regression. The existing runtime adapter check also accepts
the updated OpenGL bridge boundary.

Installed guarded OpenGL evidence is
`rmlui_players_live_provider_final2_20260713` at 960x720. The harness seeds
`name=Ranger`, `skin=female/athena`, and `hand=1` immediately before route
open. The final frame visibly confirms:

- Ranger, Female, Athena, Default dogtag, and Left control hydration;
- the Athena portrait and Default dogtag loaded from their original PCX data;
- a complete animated player plus attached weapon inside the preview frame;
- shared WORR chrome, Q2R TTF typography, focus treatment, and status bar;
- no form, thumbnail, weapon, or footer clipping; and
- a clean route close after synthetic keyboard, text, pointer, mouse-button,
  wheel, and back input.

The log contains no missing-texture, invalid-path, scrap-atlas, parser, or
RmlUi warning/error line. The engine, OpenGL renderer, and focused checks build
and pass, and `.install/` was refreshed with 308 packaged assets, including
214 RmlUi and 31 bot files.

Final automated verification for this slice is:

- `5 passed` in the focused Player Setup provider suite;
- `27 passed` across the Player Setup provider and runtime-capture suites;
- `252 passed` across `tools/ui_smoke`;
- 58/58 required route documents present, with metadata sync, metadata shape,
  phase consistency, runtime asset/import, and Player Setup contract checks
  passing;
- successful RmlUi-enabled Windows engine and OpenGL renderer builds; and
- a refreshed `.install/` containing the current binaries and 308 assets.

## Remaining migration work

- Add action-level automation that changes model, skin, dogtag, hand, and name
  through the controls and verifies the resulting cvars and preview reload.
- Run the final large-text, localization, controller-navigation, viewport,
  and native cross-renderer parity matrices.
- Implement native Vulkan and RTX/vkpt RmlUi bridges without redirecting them
  to OpenGL before advancing the central migration phase.

No separate user guide is needed for this slice: it restores and completes the
existing Player Setup workflow without introducing a new player-facing cvar or
concept.
