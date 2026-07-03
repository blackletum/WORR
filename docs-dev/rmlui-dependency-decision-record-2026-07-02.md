# RmlUi Dependency Decision Record

Date: 2026-07-02

Task IDs: `FR-09-T02`, `FR-09-T03`, `DV-06-T01`, `DV-03-T07`, `DV-07-T04`

Decision status: active; native renderer/runtime route ownership not implemented; Round 20 OpenGL render-interface scaffold landed.

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
Round 20 adds an OpenGL renderer-owned `Rml::RenderInterface` scaffold and
keeps it non-render-ready with `CanRender=false`. No route-rendering runtime,
visible renderer backend, or parity claim is made.

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
- `runtime_stub` means guarded document probing with legacy fallback for
  `main`, `game`, and `download_status`; it does not mean RmlUi owns
  presentation.
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
- No visible native renderer backend, live controller execution, runtime
  navigation, screenshot/layout evidence, or legacy JSON removal is accepted
  yet.

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
adds the first OpenGL `Rml::RenderInterface` scaffold, but keeps it
`CanRender=false` because it does not yet upload geometry/textures, draw
visible output, or count as Gate G1 renderer proof.

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
- No visible native renderer backend is implemented by this record.
- No Vulkan renderer path is redirected to OpenGL.
- No live controller, filesystem, input, or font interface is implemented by
  this record.
- No screenshot, layout, runtime navigation, or end-user parity claim is made
  by this record.
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

## Progression Status

| Task | Status after this record | Next required evidence |
| --- | --- | --- |
| `DV-06-T01` | Upstream RmlUi `6.2` source, hash, license/provenance notes, optional dependency state, wrap provide aliases, CMake fallback options, scratch compile/link evidence, WORR-backed system/file bridge evidence, renderer-contract dependency boundary, and OpenGL-scoped renderer scaffold dependency wiring documented. | Final notice/update policy, local patch policy, supported-matrix acceptance, and renderer/runtime dependency policy. |
| `FR-09-T02` | Source wrap, default-disabled Meson feature option, optional probes, CMake fallback, and enabled scratch RmlUi Core link proof landed. | Supported build matrix and `.install` refresh prove the selected integration path beyond the scratch build. |
| `FR-09-T03` | Runtime/native renderer requirements documented; dependency-free hook boundary, compiled Core adapter boundary, RmlUi system/file bridge, native renderer bridge contract, and OpenGL render-interface scaffold prepared. | Sample route opens through the real RmlUi runtime and draws natively in OpenGL, Vulkan, and RTX/vkpt. |
| `DV-03-T07` | Required validation command set documented; dependency-integration and runtime-adapter/system-file/renderer-contract/OpenGL-scaffold checkers added. | Runtime navigation, screenshot/layout capture, and renderer-specific automation join the existing static checks. |
| `DV-07-T04` | Parity evidence expectations and developer-side OpenGL scaffold guardrails documented for future user-visible migration. | User-facing docs update only after runtime behavior and parity evidence are accepted. |

## Acceptance Rule

This record is accepted as planning plus Round 20 build/system-file/renderer-contract/OpenGL-scaffold boundary evidence. It
should be updated or superseded when `FR-09-T02` completes supported-matrix
dependency acceptance and when `FR-09-T03` lands the first real runtime and
native renderer bridge.
