# Paired Fixed-View Vulkan/OpenGL Telemetry

Date: 2026-07-15

Task ID: `FR-01-T15`

Status: collection path implemented and first provenance-bound measurement
recorded. No performance budget or superiority claim is justified.

## Capture path

`tools/renderer_parity/run_renderer_perf_capture.py` launches the native
Vulkan and OpenGL renderers sequentially through `win_headless 1`. The Windows
client HWND remains a real non-zero native presentation surface, but stays
hidden and never takes focus. The runner also requests `CREATE_NO_WINDOW`,
disables audio/UI/bots, uses a 960x720 fixed view, and sets both `cl_maxfps`
and `r_maxfps` to 62.

The shared scenario is `renderer_parity/fr01_renderer_perf_bmodel.cfg`. It
loads `worr_fr01_bmodel_first_frame`, moves to the deterministic bmodel view,
then begins sampling after the map/setup waits. The config intentionally uses
the existing OpenGL profiler (`gl_profile_log`) and the new native Vulkan
counter (`vk_stats_log`) at the same two-render-frame interval.

OpenGL profile logging now emits its established machine-readable `GL_STATS`
record on each opt-in interval. Vulkan emits the matching `VK_STATS` record at
`vk_stats_log` intervals after a completed frame snapshot is available. Both
controls are non-archived and default to zero, so ordinary clients do not log
per-frame telemetry.

The runner hashes the fixture plus the full recursively executed config tree
and shared launch profile. It writes the resulting hashes and exact telemetry
log hashes into the `schema_version: 1` manifest used by
`analyze_renderer_perf.py`. It also extracts `Vulkan device:` and
`GL_RENDERER:` from the two logs and rejects a capture if they are missing or
name different adapters.

## CPU timing precision

Vulkan previously measured CPU submission using `Sys_Milliseconds()`. Although
that clock is backed by Windows QPC, its integer millisecond return value
rounded normal submission work to zero and could not be compared with the
OpenGL profiler's microsecond calculation.

`vk_main.c` now obtains an overflow-safe microsecond timestamp from
`QueryPerformanceCounter` on Windows (and monotonic microseconds elsewhere).
`VK_Debug_EndFrame` receives fractional milliseconds. This changes telemetry
only; it does not add synchronization or a GPU wait to the Vulkan render path.

## First measurement

The capped hidden capture is retained at
`.tmp/renderer-parity/fr01-renderer-perf-win-headless-hires`:

```text
scenario:       fr01-bmodel-fixed-view-telemetry
fixture SHA-256: 1b65468863a0d849a319aaf9b12ec00abdf7cfd995fc733b6aca99b4b70ae873
config SHA-256:  f34b460305830e00305e0f3b42577b4e98e006344b56688b23544f67dfac4ad4
adapter:         Intel(R) Iris(R) Xe Graphics (both renderers)
samples:         120 collected / 100 after warm-up, all GPU-valid
validation:      no VUID, validation-error, device-lost, or fatal-error text
```

The post-warmup report records the following means and p95 values:

| Metric | Vulkan | OpenGL | Vulkan / OpenGL |
|---|---:|---:|---:|
| CPU mean (ms) | 0.497 | 0.218 | 2.28 |
| CPU p95 (ms) | 0.628 | 0.253 | 2.48 |
| GPU mean (ms) | 1.436 | 0.014 | 99.08 |
| GPU p95 (ms) | 2.036 | 0.016 | 127.25 |
| Draws mean | 8 | 2 | — |
| Upload bytes mean | 2032 | 0 | — |

The report passes only the collection-integrity conditions: both logs have the
required sample count, valid GPU records, matching adapter, and hashes that
match the provenance manifest. It does **not** pass a performance budget, and
none is supplied.

## Interpretation and next work

This intentionally small deterministic scene is a collection harness, not a
representative-map optimization budget. It revealed two actionable Vulkan
costs: the native path recorded four times as many draws and uploaded 2,032
bytes per frame where the OpenGL run reported no streamed bytes. The first
follow-up now gives ordinary inline BSP models immutable device-local local
geometry plus one compact current-frame transform/light instance. The hidden,
validation-backed rerun is pixel-identical to OpenGL and reports 1,872 Vulkan
upload bytes per sample, a 160-byte (7.87%) fixed-view reduction. It retains
eight draws, so model batching/indirect submission remains an open follow-up.
See `vulkan-static-inline-bsp-residency-2026-07-15.md`.

The GPU figures also must not be treated as a universal backend verdict: the
two renderers expose different batching and phase decomposition. Before any
budget is proposed, collect a representative map/scene family and reconcile
the GPU scope accounting. Static inline-BSP/bmodel residency is now implemented
under `FR-01-T14`; re-run this exact manifest for later entity batching or
transient-stream changes, but do not use it alone as a budget.

The native `FR-01-T13` sky follow-up makes the six-face cube immutable
device-local geometry, carries its rotation in the current world frame record,
and now copies compatible faces once into a native texture array. The rerun at
`.tmp/renderer-parity/fr01-renderer-perf-sky-array` records exactly 192 Vulkan
upload bytes per sample: `world_uploads=64` plus `entity_uploads=128`. This is
a 1,680-byte (89.74%) reduction from the static-bmodel result and a 1,840-byte
(90.55%) reduction from the original baseline. It has 120 matching-adapter
samples per renderer, 100 valid post-warmup samples, and clean Vulkan
validation. The array reduces the fixed fixture from eight to three Vulkan
draws; OpenGL reports two draws under a telemetry scope that does not include
its sky loop. The local driver identity is still
`not-recorded-local-validation`, so its CPU/GPU report is evidence of stream
residency/submission reduction only, not a cross-run speed claim. See
`vulkan-static-sky-and-liquid-validation-2026-07-15.md` and
`vulkan-sky-texture-array-submission-2026-07-15.md`.

## Commands

```text
python tools/renderer_parity/run_renderer_perf_capture.py \
  --install-dir .install \
  --run-root .tmp/renderer-parity/fr01-renderer-perf \
  --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" \
  --driver "<display-driver>" \
  --vulkan-validation --min-samples 100

python tools/renderer_parity/analyze_renderer_perf.py \
  --vulkan .tmp/renderer-parity/fr01-renderer-perf/vulkan.log \
  --opengl .tmp/renderer-parity/fr01-renderer-perf/opengl.log \
  --capture-manifest .tmp/renderer-parity/fr01-renderer-perf/capture.json \
  --warmup 20 --min-samples 100 \
  --json-output .tmp/renderer-parity/fr01-renderer-perf/perf-report.json
```

Use `--budget` only after representative repeated captures establish a defensible
threshold. The analyzer rejects a budget without the capture manifest.
