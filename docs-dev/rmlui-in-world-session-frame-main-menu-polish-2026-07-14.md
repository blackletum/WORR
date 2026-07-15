# RmlUi In-World Session Frame and Main Menu Polish (2026-07-14)

Project tasks: `FR-09-T11`, `DV-03-T07`, `DV-07-T04`

## Objective

Close the presentation gap between the migrated RmlUi routes and the intended
WORR experience:

- keep the active level visible around every multiplayer/session menu;
- use a quick, smooth world-focus transition without blurring UI geometry;
- carry the in-game presentation into Settings and other child routes opened
  from the live match;
- make the main menu a fixed, non-scrolling hero with no redundant WORR title,
  corner mark, Close control, or duplicated Settings/Quit actions; and
- produce current screenshot evidence for every registered menu route.

## Presentation contract

### Main menu

`assets/ui/rml/shell/main.rml` now contains one centered WORR logo and exactly
two primary key-art choices: Single Player and Multiplayer. Settings and power
remain in the top-right utility cluster, while player identity and the build
string remain in the statusbar. The main stack and action row use fixed,
contained layout with `overflow: hidden`; there is no scrollable page around
the primary choices.

The choice captions use an opaque-enough inset backplate so text remains
legible over campaign/server artwork. Focus and hover retain the existing gold
and green semantics, and high-visibility mode removes decorators in favor of
black, white, and yellow states.

### Session routes

Every `session/*` document uses a low-opacity viewport scrim and a centered
partial-screen metal frame inset 40px horizontally and 32px vertically at the
canonical 960x720 canvas. The frame uses the session orange edge, translucent
surface, bounded content flow, and visible arena margins. The match hub keeps
its established internal brand/tab/content/tools/footer hierarchy inside an
848x608 shell.

Confirmation popups are intentional exceptions: they restore a full-screen
scrim so destructive actions remain unambiguous. When a standard shell or
settings document is opened while `ui_dm_menu_active=1`, the runtime applies
`ui-session-overlay` before first display, preventing an opaque first-frame
flash and preserving the same partial in-world presentation.

## Runtime and renderer integration

The client now classifies an active route as session-owned from its registered
`session/` document path. Session routes and live-match child routes report a
transparent menu surface, publish a full-view menu focus rectangle through the
same client state used by the cgame menu, and clear that state when a normal
front-end route becomes active.

The focus amount is still driven by `cl_menu_bokeh_blur` and the existing
`v_dof_menu_blend` easing in `V_RenderView`. This is the slowtime/weapon-wheel
`refdef_t::dof_strength` contract; menu activation changes only the visual
focus driver and never changes `timescale` or match simulation.

- OpenGL consumes the established DOF scene/blur/composite pass.
- RTX/vkpt consumes its existing native menu-mode bloom/focus path.
- Conventional Vulkan now consumes `dof_strength` natively. It renders the
  world and entities first, performs a strength-eased linear downsample and
  upsample through a device-local transfer image, restores the swapchain image,
  and then draws RmlUi through the load-preserving overlay render pass. The UI
  never enters the focus source image, so its text and controls remain sharp.
  Unsupported surface/format transfer capabilities safely leave focus disabled
  without redirecting Vulkan through OpenGL.

## Validation guardrails

`check_rmlui_design_compliance.py` now rejects main-menu regressions that add
actions beyond Single Player/Multiplayer, remove the logo or Settings/power
utilities, restore the redundant Close/corner-brand/title chrome, or lose the
fixed overflow and session-frame theme contracts.

`check_rmlui_runtime_ux_services.py` now requires the native Vulkan soft-focus
transfer and post-focus UI overlay sequence. Focused regression tests cover
both new contracts.

## Build and runtime evidence

- `meson compile -C builddir-win worr_engine_x86_64 cgame_x86_64`: passed.
- `meson compile -C builddir-win worr_vulkan_x86_64`: passed.
- Design compliance: 58 documents, 1,123/1,123 localization hooks, zero errors.
- Command inventory, navigation graph, route contracts, and live session-entry
  checks: passed.
- Complete OpenGL route capture: 58/58 passed at 960x720, including font,
  synthetic key/text/pointer/wheel input, open/close counters, and fresh image
  validation.
- The four contact sheets were visually reviewed for missing chrome, clipping,
  overlap, and empty-state containment.
- Live `q2dm1` captures verify the partial frame and focus ordering on OpenGL,
  conventional Vulkan, and RTX/vkpt.

Evidence roots:

- `.tmp/rmlui/runtime-capture/menu-all-20260714/README.md`
- `.tmp/rmlui/runtime-capture/menu-all-20260714/manifest.json`
- `.tmp/rmlui/runtime-capture/menu-frame-20260714/`

The final build workflow refreshed `.install/` after validation with 16 root
runtime files, one runtime dependency, the rebuilt `basew` runtime tree, and a
new `basew/pak0.pkz` containing 322 current assets. Staging validation passed
for Windows x86-64, including 31 botfile payloads and 214 RmlUi payloads.
