# RmlUi Dependency Decision Record

Date: 2026-07-02

Task IDs: `FR-09-T02`, `FR-09-T03`, `DV-06-T01`, `DV-03-T07`, `DV-07-T04`

Decision status: active; native renderer/runtime route ownership not implemented beyond guarded OpenGL smoke/menu samples and renderer-family matrix guardrails; default route ownership, Vulkan/RTX bridges, full font/text services, responsive widescreen parity, broad input/navigation parity, runtime navigation, and parity remain pending.

## Summary

WORR should integrate RmlUi as a first-class dependency only after a license,
provenance, build, renderer, input, font, filesystem, staging, and validation
audit is complete. The current RmlUi migration remains scaffolded by authored
route documents, static metadata, packaging checks, runtime probes, and progress
reports. Round 17 updated the original decision record by proving a
default-disabled, optional RmlUi Core compile/link path and a guarded runtime
adapter. Round 18 adds WORR-backed RmlUi Core system/file interfaces and a
runtime-facing file probe. Round 19 adds a renderer-family registration
contract for future native OpenGL, Vulkan, and RTX/vkpt RmlUi render bridges.
Round 20 added an OpenGL renderer-owned `Rml::RenderInterface` scaffold. Round
21 turns that OpenGL scaffold into a primitive bridge with geometry caching,
tessellator rendering, texture handling, and scissor state; normal route opening
still remains blocked. Round 22 adds a guarded `core.runtime_smoke` runtime
path that creates a RmlUi context, loads one document, updates/renders it
through the client UI draw loop, and closes it with Escape or
`ui_rml_runtime_close`. Round 23 adds guarded key/text/mouse event delivery
into that sample context plus status/capture commands for manual evidence
collection. Round 24 adds automated guarded OpenGL TGA screenshot evidence,
a local screenshot output override, a styled smoke route, and a temporary
layout-only font engine that lets RmlUi initialize under
`RMLUI_FONT_ENGINE=none`. Round 25 replaces that layout-only adapter with a
minimal smoke bitmap glyph path and requires glyph-generation evidence in the
runtime capture harness. Round 26 adds route-specific TGA layout assertions to
that harness. Round 27 adds guarded synthetic input/back-close evidence to the
same sample capture. Round 28 broadens the capture into a two-viewport OpenGL
matrix with exact geometry/dimension validation. Round 29 adds guarded OpenGL
menu-route matrix evidence for `main`, `game`, and `download_status` through
`UI_OpenMenu`. Round 30 adds a focused renderer-family matrix guardrail:
OpenGL is the only current guarded native lane, Vulkan and RTX/vkpt remain
explicitly blocked until native bridges exist, and Vulkan/RTX-to-OpenGL
shortcut wiring fails validation. No default route ownership,
Vulkan/RTX-vkpt bridge, full font/text service, responsive widescreen parity,
broad input/navigation parity, runtime navigation, or parity claim is made.

Legacy UI remains authoritative until Gate G1 proves the vertical runtime path
and later gates prove route parity. Any RmlUi runtime switch must remain opt-in
or guarded until the validation checklist in this record passes.

## Current Baseline

The accepted Round 15 baseline is:

- `57` authored route documents are tracked in the smoke manifest.
- Migration phases are `starter=12`, `controller_stub=42`, and
  `runtime_stub=3`, with `45/57` advanced routes (`78.9%`).
- `subprojects/rmlui.wrap` records the selected upstream RmlUi source:
  release `6.2`, archive URL
  `https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz`, and
  SHA-256 `814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b`.
- `meson_options.txt` exposes a default-disabled `rmlui` feature option.
  The current Meson wiring probes optional `RmlUi` CMake and `rmlui`
  pkg-config dependencies only when the option is allowed.
- `tools/ui_smoke/check_rmlui_dependency_integration.py` reports the current
  dependency/build state as `optional`: `4/4` integration components present,
  `1` wrap file, `2` optional Meson dependency declarations, `1`
  default-disabled Meson option, `1` optional `UI_RML_HAS_RUNTIME` compile
  define, and `runtime_compiled=false`.
- `src/client/ui_rml/` now exposes dependency-free runtime availability,
  file-interface, and runtime-hook boundaries for future implementation while
  preserving legacy fallback behavior.
- `runtime_stub` means guarded document probing plus opt-in OpenGL
  load/render/input/close evidence for `main`, `game`, and `download_status`;
  it does not mean RmlUi owns presentation by default.
- `controller_stub` means route/controller metadata readiness; it does not mean
  live C++ controllers exist. The current `controller_stub` route set includes
  shell/settings routes plus utility/list routes such as `addressbook`, `keys`,
  `legacykeys`, `weapons`, `servers`, `demos`, `players`, and `ui_list`.
- Parity checklist coverage exists for all routes, but no route is
  `parity_ready`.
- Round 17 adds a guarded compiled RmlUi Core adapter that reports
  `renderer_unavailable` and refuses route opening until a renderer-capable
  runtime is registered.
- Round 18 adds WORR-backed RmlUi `SystemInterface` and `FileInterface`
  implementations plus `ui_rml_runtime_probe` for runtime-facing file-load
  checks.
- Round 19 adds explicit OpenGL, Vulkan, and RTX/vkpt renderer-family lanes,
  an opaque native render-interface hook, and route-availability validation
  that still refuses ownership until a native renderer is available.
- Round 20 adds an OpenGL-owned render-interface scaffold, client lifecycle
  registration, adapter `Rml::SetRenderInterface` installation, and validation
  that the scaffold remains `CanRender=false`.
- Round 21 replaces the OpenGL no-op scaffold with geometry, texture, draw,
  and scissor primitives and validates `CanRender=true` for the OpenGL bridge
  while the runtime still refuses normal menu route ownership.
- Round 22 creates a guarded `core.runtime_smoke` context route for OpenGL
  through `ui_rml_runtime_open`, validates the context/document update-render
  lifecycle, and keeps normal menu routes and non-OpenGL renderers guarded.
- Round 23 adds guarded key/text/mouse-button/mouse-wheel/pointer delivery
  into the `core.runtime_smoke` context plus `ui_rml_runtime_status` and
  `ui_rml_runtime_capture` for repeatable manual evidence collection.
- Round 24 adds `tools/ui_smoke/check_rmlui_runtime_capture.py`, which
  records and validates a guarded OpenGL TGA screenshot/log pair for
  `core.runtime_smoke`, plus a temporary layout-only RmlUi font engine. The
  accepted capture shows nonblank styled geometry, not real glyph rendering.
- Round 25 replaces the layout-only font adapter with a smoke bitmap glyph
  path and requires the glyph-generation marker in runtime capture evidence.
  The accepted capture shows sample text geometry, but not final font/text
  service behavior.
- Round 26 adds TGA layout assertions to the same runtime capture harness,
  validating smoke-route colors, bounding boxes, and panel/text/button
  placement relationships for the guarded OpenGL sample.
- Round 27 adds synthetic input/back-close validation to that same harness,
  requiring pointer, text, wheel, mouse-button, close-request, close-counter,
  and inactive-status evidence after the screenshot is written.
- Round 28 adds geometry-driven viewport matrix validation to that same
  harness, requiring exact screenshot dimensions and the full glyph/layout/
  input/close assertion set for `960x720` and `1280x960`.
- Round 29 adds guarded menu-route matrix validation for `main`, `game`, and
  `download_status`, requiring `960x720` screenshots, active OpenGL route
  status, glyph text evidence, synthetic input, close counters, and inactive
  final status for each route.
- Round 30 adds `tools/ui_smoke/check_rmlui_renderer_matrix.py`, which records
  OpenGL as the only current guarded native lane, requires Vulkan and RTX/vkpt
  to stay unavailable until native bridges exist, and rejects Vulkan/RTX-to-
  OpenGL routing.
- No default normal-route ownership, native Vulkan/RTX renderer backend, live
  controller execution, runtime navigation, broad route/input evidence,
  responsive widescreen parity, full font/text service, or legacy JSON removal
  is accepted yet.

## Proposed Decision

Use a Meson-managed integration as the preferred path:

- Preferred: add RmlUi through a Meson subproject or wrap so the dependency is
  reproducible, version-pinned, and visible to normal WORR build diagnostics.
- Acceptable fallback: vendor a reviewed RmlUi source snapshot under the
  existing third-party dependency structure only if the wrap path cannot satisfy
  WORR's Windows/Linux build requirements.
- Required before either path: complete `DV-06-T01` provenance and license audit
  that records upstream source, exact version or commit, local patches, license
  obligations, transitive dependency expectations, and update policy.

This record started as a decision direction. Round 15 selected and pinned the
source acquisition record plus a default-disabled Meson gate. Round 17 proves a
compile/link slice through RmlUi Core in an enabled scratch build, but
`FR-09-T02` remains open until the supported build matrix and install-refresh
path are accepted. Round 18 proves that the compiled Core path uses WORR-owned
system/file interfaces for runtime probes rather than RmlUi's default file
backend. Round 19 proves that the dependency boundary has explicit native
renderer-family lanes and no accepted Vulkan-to-OpenGL fallback. Round 20
proves that renderer-side C++/RmlUi dependency wiring can stay scoped to the
OpenGL renderer scaffold while non-OpenGL renderer exports remain unavailable.
Round 21 proves that the same scoped OpenGL dependency path can own RmlUi
geometry, texture, draw, and scissor primitives without making the runtime
route-ready or touching Vulkan/RTX-vkpt paths. Round 22 proves the same path
can load and draw one guarded RmlUi document from a context, while normal menu
routes and Vulkan/RTX-vkpt remain blocked pending native proof. Round 23 proves
that the guarded context can receive a first key/text/mouse event bridge and
emit manual capture/status counters without broad route ownership or parity
claims. Round 24 proves that the guarded OpenGL path can emit automated local
TGA screenshot/log evidence for that sample route, with a null font engine
used only as a temporary layout bootstrap. Round 25 proves that the same
sample route can generate visible RmlUi glyph meshes through a temporary smoke
bitmap font path. Round 26 proves that the same capture can enforce a first
visual layout contract for the smoke route. Round 27 proves that the same
capture can drive synthetic pointer/text/wheel/back-close input and retain
route teardown counters after the visual screenshot. Round 28 proves that the
same harness can run a two-viewport OpenGL matrix with exact screenshot
dimension validation. Round 29 proves that the same opt-in OpenGL runtime path
can open the three guarded menu entrypoint routes through `UI_OpenMenu` and
capture route-specific load/render/input/close evidence without making RmlUi
the default menu owner.

## Round 23 Implementation Update

The dependency boundary now includes the first guarded RmlUi input/capture
proof. `ui_rml_runtime_interface_t` carries key, char, and mouse hooks without
leaking RmlUi types into the public scaffold. The compiled adapter translates
WORR key, modifier, text, mouse button, mouse wheel, and pointer movement events
into the active RmlUi context. The scaffold records frame/input counters,
closes the guarded sample route on Escape or mouse button 2, and exposes
`ui_rml_runtime_status` plus `ui_rml_runtime_capture` for manual OpenGL
evidence collection.

This does not claim route ownership for normal menus. The accepted route is
still only `core.runtime_smoke`, the accepted renderer path is still OpenGL
only, and no automated screenshot, controller execution, Vulkan/RTX-vkpt
renderer bridge, full input/font service, parity proof, or legacy JSON removal
is accepted.

## Round 24 Implementation Update

The dependency boundary now includes the first automated guarded screenshot
proof. `tools/ui_smoke/check_rmlui_runtime_capture.py` can launch the enabled
scratch engine, open `core.runtime_smoke`, capture a TGA screenshot through
the OpenGL renderer, validate the flushed log/status/frame/input markers,
verify `960x720` dimensions, enforce a nonblank TGA payload, copy evidence to
`.tmp/rmlui/runtime-capture`, and write a JSON manifest.

The renderer screenshot code now exposes `r_screenshot_dir` as an
empty-by-default, non-archived cvar so the harness can keep evidence in
`.install/basew/screenshots` without changing normal screenshot behavior. The
same output-root override exists in the OpenGL and RTX screenshot
implementations; it does not redirect Vulkan or RTX/vkpt RmlUi rendering.

The compiled RmlUi adapter also installs `UI_Rml_NullFontEngineInterface`
before `Rml::Initialise`. This is a temporary dependency/bootstrap decision
for the current `RMLUI_FONT_ENGINE=none` build: it reports metrics and string
widths so layout can run, but it emits no glyph mesh. Final font source,
fallback, glyph coverage, localization, scaling, and text overflow behavior
remain under Gate G1 font/text work.

This does not claim route ownership for normal menus. The accepted route is
still only `core.runtime_smoke`, the accepted renderer path is still OpenGL
only, and no PNG requirement, full font service, controller execution,
runtime navigation, Vulkan/RTX-vkpt renderer bridge, parity proof, or legacy
JSON removal is accepted.

## Round 25 Implementation Update

The dependency boundary now includes a first guarded text-geometry proof.
`UI_Rml_SmokeFontEngineInterface` replaces the Round 24 null font adapter with
a minimal ASCII bitmap glyph path that emits untextured colored 5x7 glyph
quads through RmlUi's `TexturedMeshList`. The OpenGL bridge already maps
untextured geometry to its white texture fallback, so the smoke glyph meshes
reuse the Round 21 primitive path without adding font assets or renderer
special cases.

`tools/ui_smoke/check_rmlui_runtime_adapter.py` now requires the
glyph-generating smoke font tokens, and
`tools/ui_smoke/check_rmlui_runtime_capture.py` now requires the
`RmlUi smoke font engine generated glyph geometry` log marker. The accepted
Round 25 capture wrote `rmlui_runtime_smoke_round25.tga`, saw
`font_geometry_marker_seen=true`, recorded `24` updates/renders, and retained
the guarded OpenGL status and nonblank `960x720` TGA payload.

This does not claim final font/text ownership. The smoke glyph path has no
text shaping, kerning, Unicode coverage, localization fallback, final scaling,
or overflow parity. The accepted route is still only `core.runtime_smoke`, the
accepted renderer path is still OpenGL only, and no PNG requirement,
controller execution, runtime navigation, Vulkan/RTX-vkpt renderer bridge,
parity proof, or legacy JSON removal is accepted.

## Round 26 Implementation Update

The dependency boundary now includes first guarded visual layout assertions for
the automated screenshot evidence. `tools/ui_smoke/check_rmlui_runtime_capture.py`
parses uncompressed 24/32-bit true-color TGA files, counts the expected
`core.runtime_smoke` colors, records color bounding boxes, and validates that
the panel background has route-scale extent, the panel border wraps it, body
text spans summary/panel regions, accent text sits above the buttons, and
button fill appears below the panel.

The accepted Round 26 capture wrote `rmlui_runtime_smoke_round26.tga`, saw
`layout_checked=true`, `layout_ok=true`, `font_geometry_marker_seen=true`,
recorded `24` updates/renders, and retained the guarded OpenGL status and
nonblank `960x720` TGA payload. The manifest records panel, border, button,
body-text, and accent-text bounding boxes plus `12` true layout assertions.

This does not claim broad screenshot/layout evidence. The accepted layout
contract is route-specific to the guarded OpenGL `core.runtime_smoke` sample
and currently requires TGA evidence. It does not prove normal route ownership,
PNG output, renderer parity, text shaping, localization, accessibility
scaling, controller execution, runtime navigation, Vulkan/RTX-vkpt renderer
bridges, parity proof, or legacy JSON removal.

## Round 27 Implementation Update

The dependency boundary now includes first guarded synthetic input/back-close
evidence for the automated capture path. `ui_rml_runtime_synthetic_input`
drives pointer motion, text input, mouse-wheel input, and mouse-button-2
back-close through the same public RmlUi input bridge used by live input. The
runtime status path now records route opens, closes, close requests, and
synthetic input attempts so teardown evidence survives after the route is
closed.

The accepted Round 27 capture wrote `rmlui_runtime_smoke_round27.tga`, saw
`synthetic_input_marker_seen=true`, `inactive_status_seen=true`,
`input_keys=2`, `input_chars=1`, `input_mouse_moves=1`,
`input_mouse_buttons=1`, `input_mouse_wheels=1`, `route_opens=1`,
`route_closes=1`, `route_close_requests=1`, `route_synthetic_inputs=1`,
and retained `layout_ok=true`, `font_geometry_marker_seen=true`, `24`
updates/renders, and the nonblank `960x720` TGA payload.

This does not claim final input ownership. The accepted input path is
developer-command-driven, route-specific, and OpenGL sample-only. It does not
prove gamepad navigation, focus cycling, pointer capture edge cases, normal
route ownership, live controller behavior, Vulkan/RTX-vkpt renderer bridges,
parity proof, or legacy JSON removal.

## Round 28 Implementation Update

The dependency boundary now includes first guarded viewport-matrix evidence for
the automated OpenGL capture path. `tools/ui_smoke/check_rmlui_runtime_capture.py`
accepts `--geometry WIDTHxHEIGHT`, passes that geometry through `r_geometry`,
requires the screenshot dimensions to match, and exposes `--matrix` for the
default `960x720` plus `1280x960` viewport pair. Matrix output writes one
aggregate manifest with per-viewport commands, paths, facts, copied evidence,
and errors.

The accepted Round 28 matrix wrote
`rmlui_runtime_smoke_round28_default_960x720.tga` and
`rmlui_runtime_smoke_round28_large_1280x960.tga`, saw `viewports=2`,
`passed=2`, `failed=0`, `errors=[]`, and retained
`layout_ok=true`, `font_geometry_marker_seen=true`,
`synthetic_input_marker_seen=true`, `inactive_status_seen=true`,
`route_closes=1`, and `route_close_requests=1` for both viewports.

This does not claim responsive layout parity. A `1280x720` experiment exposed
fixed smoke-layout clipping, so widescreen-responsive behavior remains a
future theme/route requirement. The accepted matrix is still guarded,
sample-only, and OpenGL-only; it does not prove normal route ownership,
Vulkan/RTX-vkpt renderer bridges, live controller behavior, broad input/
navigation parity, final font/text behavior, parity proof, or legacy JSON
removal.

## Round 29 Implementation Update

The dependency boundary now includes the first guarded normal-menu-entrypoint
OpenGL route matrix. `src/client/ui_rml/ui_rml.cpp` accepts `main`, `game`,
and `download_status` in the guarded runtime route set and adds
`ui_rml_runtime_capture_menu <route>`, which opens those routes through
`UI_OpenMenu`. `src/client/ui_rml/ui_rml_runtime.cpp` mirrors that
allow-list in the compiled adapter while keeping non-OpenGL renderers native
pending and unavailable.

`tools/ui_smoke/check_rmlui_runtime_capture.py --route-matrix` writes one
aggregate route manifest for `main`, `game`, and `download_status`, each at
`r_geometry=960x720`. The accepted Round 29 matrix wrote
`rmlui_runtime_smoke_round29_main.tga`,
`rmlui_runtime_smoke_round29_game.tga`, and
`rmlui_runtime_smoke_round29_download_status.tga`, saw `routes=3`,
`passed=3`, `failed=0`, and `errors=0`, and required active OpenGL route
status, glyph text evidence, synthetic input, close counters, inactive final
status, and exact `960x720` screenshots for all three routes.

The route matrix intentionally records `layout_required=false`; the final
theme/layout parity and route-specific visual assertions are still future work.
Round 29 is still opt-in through `ui_rml_enable=1`, OpenGL-only, and guarded.
It does not prove default route ownership, Vulkan/RTX-vkpt renderer bridges,
live controller behavior, route navigation, broad input/focus/gamepad parity,
final font/text behavior, parity proof, or legacy JSON removal.

## Round 30 Implementation Update

The dependency boundary now has a named renderer-family matrix guardrail.
`tools/ui_smoke/check_rmlui_renderer_matrix.py` validates the current supported
state as one guarded native lane plus two native-pending lanes:

- OpenGL must retain OpenGL-scoped RmlUi Meson dependency wiring, export the
  native renderer hooks from the `REF_GL` renderer API lane, own
  `R_RmlUiOpenGLRenderInterface`, report `CanRender=true`, return the native
  `Rml::RenderInterface`, and stay the renderer used by guarded runtime
  capture automation.
- Vulkan must remain a distinct family lane, map to the Vulkan UI family, and
  keep the non-OpenGL renderer API exports unavailable (`family=NONE`,
  `CanRender=false`, `NativeRenderInterface=NULL`) until a native Vulkan RmlUi
  render bridge exists.
- RTX/vkpt must remain a distinct family lane, map to the RTX/vkpt UI family,
  and keep the same unavailable non-OpenGL renderer API exports until a native
  RTX/vkpt RmlUi render bridge exists.

The accepted Round 30 report recorded `native_guarded_lanes=1`,
`blocked_lanes=2`, `errors=0`, and
`no_vulkan_or_rtx_to_opengl_redirect=true`. Focused pytest coverage now fails
OpenGL `CanRender=false`, Vulkan mapped to OpenGL, premature Vulkan runtime
dependency enablement, and non-OpenGL capture harness wiring.

Round 30 still does not satisfy Gate G1. It adds regression protection around
the renderer matrix, but the Vulkan and RTX/vkpt native RmlUi render bridges,
default route ownership, final font/text services, controller behavior,
runtime navigation, and parity proof remain pending.

## Required Gate G1 Interfaces

Before Gate G1 can pass, `FR-09-T03` must provide native runtime interfaces
that do not bypass WORR subsystems:

| Interface | Required proof before Gate G1 |
| --- | --- |
| System | RmlUi time, logging, allocation/error reporting, and shutdown order are routed through WORR-owned engine services or documented wrappers. |
| File | RmlUi document, style, image, and font loads resolve through WORR filesystem/package paths, including staged `.install/<base-game>/ui/rml/` assets. |
| Input | Keyboard, mouse, text entry, focus, escape/back, gamepad navigation, pointer capture, and menu open/close state are translated through WORR input ownership. |
| Font/text | Font source, fallback policy, glyph coverage, localization strings, scaling, and long-string overflow behavior are documented and validated against current UI expectations. |
| Runtime route | At least one route opens from a normal WORR menu entry point, handles one command button, one cvar-backed control, and one conditional element, then returns safely to the legacy path when disabled or failed. |

## Native Renderer Requirement

Gate G1 requires native RmlUi renderer proof for all active renderer families:

- OpenGL: sample route opens and draws through an OpenGL-native RmlUi render
  bridge.
- Vulkan: sample route opens and draws through a Vulkan-native RmlUi render
  bridge.
- RTX/vkpt: sample route opens and draws through the Vulkan RTX/path-tracing
  renderer path with any composition rules documented.

There must be no Vulkan-to-OpenGL fallback. Vulkan renderer work must stay
native in `rend_vk`/`vk_`/`pt_` paths, including RTX/vkpt behavior.

Round 19 adds the registration contract for these renderer families. Round 20
adds the first OpenGL `Rml::RenderInterface` scaffold. Round 21 implements the
OpenGL bridge's geometry, texture, draw, and scissor primitives and reports
`CanRender=true` for that renderer family. This still does not count as Gate G1
renderer proof because only the explicit `core.runtime_smoke` command route
creates a RmlUi context and draws from the client UI frame loop; no normal menu
entry point owns RmlUi yet, and Vulkan/RTX-vkpt bridges are still
native-pending.

Round 23 adds input delivery and manual status/capture counters to that same
guarded command route. This improves the OpenGL sample proof path, but it
still does not satisfy Gate G1 because no normal menu entry point, Vulkan
bridge, RTX/vkpt bridge, automated screenshot harness, or full input/font
service has been accepted.

Round 24 adds automated screenshot evidence for that same guarded OpenGL route.
It still does not satisfy Gate G1 because the screenshot is sample-only, the
font engine is layout-only, no normal menu entry point owns RmlUi, and
Vulkan/RTX-vkpt bridges remain native-pending.

Round 25 adds smoke glyph geometry to that same guarded OpenGL route. It still
does not satisfy Gate G1 because the font path is temporary and sample-only,
no normal menu entry point owns RmlUi, and Vulkan/RTX-vkpt bridges remain
native-pending.

Round 26 adds visual layout assertions to that same guarded OpenGL route. It
still does not satisfy Gate G1 because the route is sample-only, no normal menu
entry point owns RmlUi, and Vulkan/RTX-vkpt bridges remain native-pending.

Round 27 adds synthetic input/back-close validation to that same guarded
OpenGL route. It still does not satisfy Gate G1 because the input proof is
developer-command-driven and sample-only; no normal menu entry point owns
RmlUi, broad input/navigation parity is not proven, and Vulkan/RTX-vkpt
bridges remain native-pending.

Round 28 adds viewport-matrix validation to that same guarded OpenGL route. It
still does not satisfy Gate G1 because the matrix is sample-only, no normal
menu entry point owns RmlUi, responsive widescreen parity is not proven, and
Vulkan/RTX-vkpt bridges remain native-pending.

Round 29 adds guarded OpenGL route-matrix validation for the normal menu
entrypoint documents `main`, `game`, and `download_status`. It still does not
satisfy Gate G1 because ownership is opt-in rather than default, the proof is
OpenGL-only, final font/text and controller behavior are not implemented,
runtime navigation is not proven, and Vulkan/RTX-vkpt bridges remain
native-pending.

Round 30 adds renderer-family matrix guardrail validation. It records the
accepted current state as `OpenGL=native_guarded`,
`Vulkan=blocked_until_native`, and `RTX/vkpt=blocked_until_native`, and it
rejects Vulkan/RTX-to-OpenGL mapping or premature non-OpenGL RmlUi runtime
dependency enablement. It still does not satisfy Gate G1 because it proves
guardrails rather than native Vulkan/RTX-vkpt drawing.

## Packaging And Staging Requirements

Before any runtime switch can be enabled by default, asset packaging must prove
the dependency path consumes the same authored asset set tracked by current
tools:

- `assets/ui/rml/` route documents, styles, imports, fonts, images, and
  controller metadata are packageable.
- `.install/<base-game>/ui/rml/` is refreshed by normal packaging/build flows.
- The package archive and loose staged mirror agree by path and content hash.
- Runtime path derivation matches the paths consumed by the C++ RmlUi file
  interface.
- Detailed runtime asset manifests are written to `.tmp/rmlui/` during
  validation so missing source, import, or staged files can be reviewed.

## Validation Checklist Before Default Enablement

The following commands/checks are required before any default runtime switch is
accepted. Paths may be adjusted for the active build directory and staging root,
but the evidence categories must remain intact.

Static and metadata checks:

```text
python tools\ui_smoke\check_rmlui_manifest.py
python tools\ui_smoke\check_rmlui_dependency_integration.py
python tools\ui_smoke\check_rmlui_route_contracts.py
python tools\ui_smoke\check_rmlui_controller_stub_coverage.py
python tools\ui_smoke\check_rmlui_controller_fixtures.py
python tools\ui_smoke\check_rmlui_navigation_graph.py
python tools\ui_smoke\check_rmlui_parity_manifest.py
python tools\ui_smoke\check_rmlui_semantics.py
python tools\ui_smoke\check_rmlui_menu_entrypoints.py
python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py
python tools\ui_smoke\check_rmlui_runtime_registry.py
python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json
python tools\ui_smoke\report_rmlui_progress.py --format json
```

Asset and staging checks:

```text
python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\dependency-validation --base-game basew --archive-name pak0.pkz
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\dependency-validation --base-game basew --format json
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\dependency-validation --base-game basew --write-manifest .tmp\rmlui\dependency-runtime-assets.json
```

Build and runtime checks:

- Configure and build the selected Meson target with the RmlUi dependency
  enabled.
- Compile the RmlUi runtime, file/system, input, font, and renderer bridge
  translation units.
- Run the focused `tools/ui_smoke` pytest suite.
- Open the Gate G1 sample route from normal menu entry points in OpenGL,
  Vulkan, and RTX/vkpt.
- Capture evidence for renderer output, escape/back behavior, a command
  button, a cvar-backed control, a conditional element, and clean fallback when
  RmlUi is disabled or cannot load a document.
- For guarded smoke evidence before Gate G1, run
  `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json`.

## Risks

- Dependency provenance or license terms may require a different vendoring
  approach or extra notices.
- RmlUi upstream build assumptions may conflict with WORR's Meson or Windows
  toolchain behavior.
- Renderer bridge work can diverge between OpenGL, Vulkan, and RTX/vkpt if the
  abstraction hides renderer-specific constraints.
- File path mismatches between package assets, loose `.install/` assets, and
  runtime lookup can create build-green but runtime-broken states.
- Input focus, text entry, escape/back, gamepad navigation, and mouse capture
  can regress existing menu behavior even when documents draw correctly.
- Font fallback and localization stress can produce layout regressions not
  visible in short English-only smoke routes.

## Rollback Policy

- Keep the legacy UI path authoritative until all relevant gates pass.
- Keep RmlUi default-disabled until the Gate G1 validation checklist succeeds
  across OpenGL, Vulkan, and RTX/vkpt.
- If dependency integration breaks build or runtime stability, disable the
  RmlUi build option or runtime switch and leave the legacy UI route active.
- If an individual route fails parity, keep that route on legacy JSON and
  record the reason in the parity manifest or follow-up docs.
- Do not remove legacy JSON loading, widgets, or assets until Gate G4 passes.

## Non-Goals

- The original record did not add a dependency; Round 15 and Round 17 now
  record separate wrap/build-option/compile-link implementation evidence.
- The Round 17 Core adapter does not make RmlUi the active menu presentation
  runtime.
- No runtime switch is enabled by default by this record.
- No default or supported-matrix normal menu route-visible native renderer
  backend is implemented by this record.
- This record does not claim route ownership beyond the explicit guarded
  OpenGL smoke/menu routes.
- No Vulkan renderer path is redirected to OpenGL.
- No live controller, full input service, or full font/text interface is
  implemented by this record.
- No broad screenshot matrix, runtime navigation, or end-user parity claim is
  made by this record beyond the explicit guarded OpenGL smoke and menu-route
  TGA evidence recorded in Rounds 24 through 29 and the renderer-family
  guardrail recorded in Round 30.
- No legacy JSON path is removed or deprecated by this record.

## Round 17 Implementation Update

Round 17 landed the first optional compiled Core boundary for the selected
dependency path:

- `subprojects/rmlui.wrap` keeps the pinned upstream RmlUi `6.2` archive and
  now provides `RmlUi` and `rmlui` dependency aliases.
- `meson.build` probes external `RmlUi::Core` first, then external pkg-config
  `rmlui`, then falls back to the pinned CMake subproject.
- The fallback selects the exposed `rmlui_core` target and sets
  `RMLUI_FONT_ENGINE=none`, `RMLUI_LUA_BINDINGS=false`,
  `RMLUI_SAMPLES=false`, and `RMLUI_TESTS=false`.
- `RMLUI_FONT_ENGINE=none` is a temporary compile-only adapter decision. The
  final font/text source, fallback, glyph coverage, localization, and scaling
  policy remain under `FR-09-T04` and `DV-06-T01`.
- `.tmp/rmlui/round17-rmlui-enabled3` configured with `-Drmlui=enabled` and
  linked both `subprojects/RmlUi-6.2/rmlui_core.dll` and
  `worr_engine_x86_64.dll`.
- `src/client/ui_rml/ui_rml_runtime.cpp` compiles only under
  `UI_RML_HAS_RUNTIME` and registers a runtime interface that still returns
  `CanOpenRoutes=false`.
- `renderer_unavailable` is the expected runtime availability result for this
  round when RmlUi Core is compiled but no render-ready native backend exists.

## Round 18 Implementation Update

Round 18 landed the first WORR-owned RmlUi Core system/file bridge:

- `src/client/ui_rml/ui_rml_runtime.cpp` installs a RmlUi `SystemInterface`
  before `Rml::Initialise`.
- The system interface uses `Sys_Milliseconds` for elapsed time and routes
  RmlUi logs to `Com_EPrintf`, `Com_WPrintf`, and `Com_Printf`.
- The current translation hook is pass-through until the localization bridge is
  implemented under `FR-09-T04`.
- The path-join hook normalizes document-relative RML/RCSS resource paths while
  preserving game-relative lookup semantics.
- `src/client/ui_rml/ui_rml_runtime.cpp` installs a RmlUi `FileInterface`
  before `Rml::Initialise`.
- The file interface uses `FS_OpenFile`, `FS_CloseFile`, `FS_Read`, `FS_Seek`,
  `FS_Tell`, and `FS_Length`, so RmlUi runtime probes read through WORR's
  filesystem/package search path.
- `ui_rml_runtime_probe [route_id]` starts the compiled runtime for an explicit
  probe, loads the resolved route document through
  `Rml::GetFileInterface()->LoadFile`, and shuts the runtime back down when the
  probe started it.
- `CanOpenRoutes=false` and `renderer_unavailable` remain expected because no
  render-ready native backend exists yet.

## Round 19 Implementation Update

Round 19 landed the first native renderer bridge contract:

- `src/client/ui_rml/ui_rml.h` declares `ui_rml_renderer_family_t` with
  OpenGL, Vulkan, and RTX/vkpt family values.
- `ui_rml_renderer_interface_t` declares renderer diagnostics, readiness, and
  an opaque native render-interface hook without exposing RmlUi types through
  the scaffold header.
- `src/client/ui_rml/ui_rml.cpp` stores renderer registration state, exposes
  renderer query helpers, and requires `CanRender=true` plus a non-null native
  render-interface pointer before renderer availability can become true.
- `UI_Rml_RuntimeCanOpenRoutes` now requires native renderer availability
  before consulting the runtime route-open hook.
- `src/client/ui_rml/ui_rml_runtime.cpp` reports renderer name/family in
  diagnostics but keeps `CanOpenRoutes=false`.
- Runtime-adapter validation now checks renderer-family coverage, route
  availability gating, native interface requirements, and no
  Vulkan-to-OpenGL redirection.

## Round 20 Implementation Update

Round 20 landed the first renderer-family implementation scaffold:

- `inc/renderer/renderer.h` now exposes renderer-side RmlUi family values and
  external renderer export slots for name, readiness, and an opaque native
  render-interface pointer.
- `src/renderer/rmlui_bridge.cpp` is compiled into the OpenGL renderer and
  defines an OpenGL-owned `Rml::RenderInterface` scaffold when
  `UI_RML_HAS_RUNTIME` is enabled.
- The scaffold's geometry, texture, and scissor methods are no-ops for this
  slice, and `R_RmlUiCanRender()` returns `false` until visible drawing is
  implemented.
- `src/renderer/renderer_api.c` exports the concrete bridge only for
  `USE_REF == REF_GL`; Vulkan and RTX/vkpt renderer DLL exports remain
  unavailable and do not redirect to OpenGL.
- `src/client/renderer.cpp` registers the renderer bridge after renderer init
  and clears it during renderer shutdown.
- `src/client/ui_rml/ui_rml_runtime.cpp` installs the supplied
  `Rml::RenderInterface` through `Rml::SetRenderInterface` before
  `Rml::Initialise`.
- Runtime-adapter validation now checks renderer API exports, OpenGL-scoped
  dependency wiring, client renderer lifecycle registration/clear, OpenGL
  scaffold method coverage, `CanRender=false`, and no Vulkan-to-OpenGL
  redirection.

## Round 21 Implementation Update

Round 21 landed the first OpenGL primitive render bridge:

- `src/renderer/rmlui_bridge.cpp` now compiles RmlUi geometry into
  OpenGL-renderer-owned `glVertexDesc2D_t` and `glIndex_t` caches.
- `RenderGeometry` copies translated RmlUi vertices and indices into WORR's
  existing OpenGL 2D `tess` path, resolves RmlUi texture handles to OpenGL
  texture ids, applies blend/smooth flags, and flushes the batch.
- Generated RmlUi textures upload through `qglGenTextures`,
  `GL_ForceTexture`, `qglTexImage2D`, linear filtering, and clamp-to-edge when
  available.
- Loaded RmlUi textures use `IMG_Find(..., IT_PIC, IF_NONE)` and remain owned
  by the renderer image manager; generated textures are tracked and deleted by
  the RmlUi bridge on `ReleaseTexture`.
- The bridge converts RmlUi premultiplied vertex colors and generated texture
  pixels back to straight alpha for WORR's current OpenGL blend state.
- RmlUi scissor enable/disable and rectangle changes now map to
  `GL_SCISSOR_TEST`, `qglScissor`, and `draw.scissor` tracking.
- `R_RmlUiCanRender()` now returns `true` in RmlUi-enabled OpenGL builds.
  Route ownership still remains guarded by the compiled runtime's
  `CanOpenRoutes=false`.
- Runtime-adapter validation now checks OpenGL geometry caching, draw
  primitives, generated texture upload, loaded/generated texture lifetime,
  scissor state handling, `CanRender=true`, and no Vulkan-to-OpenGL
  redirection.

## Round 22 Implementation Update

Round 22 landed the first guarded RmlUi context route:

- `ui_rml_runtime_interface_t` now exposes `CloseRoute`, `Update`, and
  `Render` hooks so the compiled RmlUi adapter can own context lifecycle while
  the public scaffold remains free of RmlUi types.
- `src/client/ui_rml/ui_rml_runtime.cpp` creates the `worr_ui` RmlUi context,
  loads and shows the `core.runtime_smoke` document, resizes the context from
  renderer dimensions, updates before rendering, renders through the native
  OpenGL bridge, and removes the context on shutdown.
- `src/client/ui_rml/ui_rml.cpp` allow-lists only `core.runtime_smoke` for
  runtime drawing, exposes `ui_rml_runtime_open [route_id]` and
  `ui_rml_runtime_close`, and keeps normal menu routes on legacy fallback.
- `src/client/ui_bridge.cpp` calls `UI_Rml_Draw(realtime)` before legacy UI
  draw while a guarded RmlUi route is active, suppresses legacy UI
  input/frame callbacks during that sample route, and closes it on Escape.
- Runtime-adapter validation now checks context lifecycle hooks, RmlUi
  `CreateContext`/document load/show/update/render/remove behavior, active
  route scaffolding, runtime open/close commands, UI bridge draw ordering, and
  no Vulkan-to-OpenGL redirection.

## Progression Status

| Task | Status after this record | Next required evidence |
| --- | --- | --- |
| `DV-06-T01` | Upstream RmlUi `6.2` source, hash, license/provenance notes, optional dependency state, wrap provide aliases, CMake fallback options, scratch compile/link evidence, WORR-backed system/file bridge evidence, renderer-contract dependency boundary, OpenGL-scoped renderer scaffold/primitive/context/input/capture/glyph/layout/input-back/viewport/menu-route dependency wiring, and renderer-family matrix guardrails documented. | Final notice/update policy, local patch policy, supported-matrix acceptance, full font/text policy, and renderer/runtime dependency policy. |
| `FR-09-T02` | Source wrap, default-disabled Meson feature option, optional probes, CMake fallback, and enabled scratch RmlUi Core link proof landed. | Supported build matrix and `.install` refresh prove the selected integration path beyond the scratch build. |
| `FR-09-T03` | Runtime/native renderer requirements documented; dependency-free hook boundary, compiled Core adapter boundary, RmlUi system/file bridge, native renderer bridge contract, OpenGL render-interface scaffold, OpenGL primitive bridge, guarded `core.runtime_smoke` context draw/input/glyph path, guarded screenshot/layout/input-back/viewport proof, guarded OpenGL menu-route matrix proof, and renderer-family matrix guardrails prepared. | Normal menu routes draw natively in OpenGL, Vulkan, and RTX/vkpt with full text rendering, controllers, navigation, and default-ownership fallback behavior proven. |
| `DV-03-T07` | Required validation command set documented; dependency-integration and runtime-adapter/system-file/renderer-contract/OpenGL-scaffold/OpenGL-primitive/context/input/capture/glyph plus guarded screenshot/layout/input/viewport/menu-route harness checks and renderer-family matrix checks added. | Runtime navigation, broader screenshot/layout/input capture, and live renderer-specific automation join the existing static checks. |
| `DV-07-T04` | Parity evidence expectations and developer-side OpenGL primitive/context/input/capture/glyph/layout/input-back/viewport/menu-route plus renderer-family matrix guardrails documented for future user-visible migration. | User-facing docs update only after runtime behavior and parity evidence are accepted. |

## Acceptance Rule

This record is accepted as planning plus Round 30 build/system-file/renderer-contract/OpenGL-primitive/guarded-context/input/capture/glyph/layout/input-back/viewport/menu-route/renderer-family boundary evidence. It
should be updated or superseded when `FR-09-T02` completes supported-matrix
dependency acceptance and when `FR-09-T03` lands default route-owning native
renderer proof across the supported renderer matrix.
