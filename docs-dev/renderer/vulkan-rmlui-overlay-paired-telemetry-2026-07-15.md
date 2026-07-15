# Guarded RmlUi Overlay Paired Telemetry

Date: 2026-07-15

Task IDs: `FR-01-T15`, `FR-02-T05`

Status: reproducible overlay-bearing telemetry and strict visual lane
implemented. It proves the same routed RmlUi workload is active in both native
renderers, that Vulkan records UI uploads, and that the guarded core document
is pixel-identical. It is not a general UI visual-parity or performance-
superiority claim.

## Purpose

The original fixed-view telemetry fixture intentionally disables RmlUi and
therefore cannot measure the device-local Vulkan UI stream. The paired capture
runner now has an explicit `--rmlui` workload mode. It enables RmlUi in the
launch profile, hashes that mode into the configuration provenance, requires
the guarded runtime-capture route marker in both logs, and rejects a Vulkan
capture that has no nonzero `ui_uploads` telemetry.

The shared scenario is
`assets/renderer_parity/fr01_renderer_perf_rmlui.cfg`. It enables reduced
motion, opens the deterministic `core.runtime_smoke` document, allows it to
settle, then samples at the same two-frame telemetry interval used by the
fixed-view collector. The document itself is the manifest fixture, so its
source hash captures UI workload changes.

`tools/renderer_parity/run_rmlui_overlay_parity.py` runs the same guarded
capture for OpenGL and validation-enabled Vulkan, stores isolated evidence
under one capture root, and evaluates
`assets/renderer_parity/fr01_rmlui_overlay_manifest.json`. The default matrix
now covers the core runtime document, the main shell, performance settings,
and the quit-confirmation popup. The core 960x720 route is locked to zero
maximum/mean RGB error and zero pixels over threshold. The other routes use
retained, feature-specific bounded thresholds; the exact limits and observed
values are recorded in `vulkan-rmlui-overlay-matrix-expansion-2026-07-15.md`.

## First local collection

The following completed headlessly under `VK_LAYER_KHRONOS_validation`:

```text
python tools/renderer_parity/run_renderer_perf_capture.py \
  --install-dir .install \
  --config renderer_parity/fr01_renderer_perf_rmlui.cfg \
  --fixture assets/ui/rml/core/runtime_smoke.rml \
  --scenario-id fr01-rmlui-overlay-telemetry --rmlui \
  --run-root .tmp/renderer-parity/fr01-renderer-perf-rmlui \
  --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" \
  --driver "<display-driver>" --vulkan-validation --min-samples 100
```

Both native renderers selected `Intel(R) Iris(R) Xe Graphics`, exited cleanly,
and recorded 120 samples. The post-warmup Vulkan sample set has 100 valid GPU
results with these means:

| Metric | Vulkan | OpenGL |
|---|---:|---:|
| CPU milliseconds | 0.538 | 0.000* |
| GPU milliseconds | 0.492 | 0.000* |
| Draws | 17 | 20 |
| Upload bytes | 3,104 | 0 |

`*` The local OpenGL run did not return valid GPU timer results, and its
telemetry does not provide equivalent UI-stream byte accounting. Timing ratios
are deliberately omitted. The local driver identity is also recorded as
`not-recorded-local-validation`, so this evidence must not be used as a
cross-run performance budget.

Vulkan's sampled records identify all 3,104 upload bytes as `ui_uploads`, with
17 draws and 216 indices. The guarded RmlUi route marker appears in both logs,
and Vulkan validation emits no diagnostic text.

The paired visual lane now independently covers four 960x720 RmlUi routes.
The core document completed with zero mean and maximum RGB error across all
691,200 pixels; the main shell and performance settings routes passed their
bounded manifest thresholds. It can be reproduced with:

```text
python tools/renderer_parity/run_rmlui_overlay_parity.py \
  --install-dir .install \
  --capture-root .tmp/renderer-parity/fr01-rmlui-overlay
```

## Remaining work

The four-route matrix is not a full menu/HUD visual matrix. `FR-02-T05`
still needs broader gameplay HUD, session-popup, input-state, and menu-route
coverage.
`FR-01-T15` needs repeated captures with a recorded driver and valid OpenGL
GPU timing before establishing any performance budget.
