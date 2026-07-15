# Vulkan RmlUi Core, Shell, Settings, and Popup Visual Matrix

Date: 2026-07-15

Task IDs: `FR-01-T15`, `FR-02-T05`

Status: six deterministic native-renderer overlay routes now have retained,
validation-backed visual comparison evidence. This is a coverage expansion,
not completion of general RmlUi or renderer visual parity.

## Change

`tools/renderer_parity/run_rmlui_overlay_parity.py` now captures six guarded
RmlUi routes by default for both native renderers:

| Route | Capture | Intent | Contract |
|---|---|---|---|
| `core.runtime_smoke` | `rmlui_core_runtime_smoke.tga` | Deterministic runtime document and input/layout baseline | Exact pixel parity |
| `main` | `rmlui_main.tga` | Main menu shell, backdrop, and command layout | Bounded image difference |
| `performance` | `rmlui_performance.tga` | Dense video-performance settings controls | Bounded image difference |
| `quit_confirm` | `rmlui_quit_confirm.tga` | Focused confirmation-popup layout and close input | Bounded image difference |
| `leave_match_confirm` | `rmlui_leave_match_confirm.tga` | In-session confirmation-popup layout and close input | Bounded image difference |
| `forfeit_confirm` | `rmlui_forfeit_confirm.tga` | In-session forfeit-confirmation layout and close input | Bounded image difference |

The runner isolates evidence per renderer and route, enables
`VK_LAYER_KHRONOS_validation` for every Vulkan launch, writes individual
runtime-capture logs, and evaluates the single retained manifest
`assets/renderer_parity/fr01_rmlui_overlay_manifest.json`. `--route-id` is
repeatable for a focused route subset, while its default executes the full
six-route matrix. A focused subset also filters the comparison to the matching
manifest scenes, so it does not require unrelated captures.

## Reproduction and result

The completed local headless evidence run was:

```text
python tools/renderer_parity/run_rmlui_overlay_parity.py \
  --install-dir .install \
  --capture-root .tmp/renderer-parity/fr01-rmlui-overlay-matrix
```

The initial six core/shell/settings captures completed and the comparison report
passed. Follow-up shell-quit, session-leave, and session-forfeit confirmation
probes completed in both renderers and passed their retained manifest
thresholds. No Vulkan log
contained `VUID-`, `Validation Error`, or validation-error diagnostics. The
report compared 960 x 720 captures (691,200 pixels each):

| Scene | Maximum absolute RGB | Mean absolute RGB | Pixels over threshold | Result |
|---|---:|---:|---:|---|
| Core runtime | `0 / 0 / 0` | `0 / 0 / 0` | `0` at threshold `0` | Exact pass |
| Main shell | `89 / 96 / 79` | `0.293 / 0.249 / 0.198` | `0.942%` at threshold `8` | Manifest pass |
| Performance settings | `97 / 88 / 81` | `0.164 / 0.170 / 0.141` | `0.632%` at threshold `8` | Manifest pass |
| Quit confirmation | `57 / 51 / 48` | `0.113 / 0.092 / 0.077` | `0.339%` at threshold `8` | Manifest pass |
| Leave-match confirmation | `54 / 51 / 48` | `0.104 / 0.094 / 0.077` | `0.341%` at threshold `8` | Manifest pass |
| Forfeit confirmation | `57 / 51 / 48` | `0.113 / 0.093 / 0.077` | `0.340%` at threshold `8` | Manifest pass |

The nonzero shell/settings deltas are localized renderer raster/color
rounding differences in an otherwise functional guarded route. The manifest
limits mean absolute RGB to `0.32 / 0.28 / 0.22`, `0.20 / 0.20 / 0.17`,
`0.14 / 0.12 / 0.10`, and `0.13 / 0.12 / 0.10`, respectively, and limits
`0.14 / 0.12 / 0.10`, respectively, and limits pixels over threshold to `1.0%`,
`0.7%`, `0.4%`, `0.4%`, and `0.4%`. Those values are deliberately tighter than
the observed result and should be revisited only with a retained reference
capture and an explained visual change.

## Scope boundary

This matrix proves native Vulkan parity for one core document, the main shell,
a representative settings page, a shell confirmation popup, and two in-session
confirmation popups. It does not cover gameplay status-bar data,
inventory/chat/weapon-wheel overlays, the remaining session popup states,
download/progress animation, controller or text-entry focus states, arbitrary
resolutions, or the wider map/effects sequence. Those cases remain `FR-02-T05`
work. It also provides no timing budget or performance-superiority claim; that
still requires driver-recorded, matched-adapter paired telemetry under
`FR-01-T15`.

## Focused verification

```text
python -m unittest tools/renderer_parity/test_run_rmlui_overlay_parity.py \
  tools/renderer_parity/test_run_renderer_perf_capture.py
python tools/renderer_parity/run_rmlui_overlay_parity.py \
  --install-dir .install \
  --capture-root .tmp/renderer-parity/fr01-rmlui-overlay-matrix
```
