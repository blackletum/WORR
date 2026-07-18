# Vulkan Dense Inline-BSP Full-Frame GPU Budget

Date: 2026-07-17  
Project task: `FR-01-T15`  
Status: active environment-bound native Vulkan regression budget; not a
cross-renderer GPU-parity claim

## Scope

`assets/renderer_parity/fr01_renderer_perf_bmodel_instances_gpu_budget.json`
protects the deterministic six-by-six inline-BSP instance workload after the
native static-world and texture-replace specializations. It uses the shared,
completed-frame `gpu_frame_ms` timestamp rather than a renderer-specific phase
sum.

The budget binds to the fixture/configuration SHA-256 values, scenario ID,
adapter (`Intel(R) Iris(R) Xe Graphics`), and `local-headless` driver identity
captured by the paired collector. It requires 100 post-warmup samples and a
valid full-frame GPU timestamp for both native renderers before evaluating any
limit. That makes it a regression guard for this exact environment, not a
portable benchmark or a claim that Vulkan is already GPU-faster than OpenGL.

## Baseline and limits

The hidden 960x720 paired collection at
`.tmp/renderer-parity/fr01-bmodel-instances-current` collected 120 samples per
backend, discarded 20 warm-up samples, and passed the budget.

| Metric | Vulkan observed | Vulkan limit |
|---|---:|---:|
| CPU mean | 0.2778 ms | 0.4000 ms |
| CPU p95 | 0.3780 ms | 0.5000 ms |
| Full GPU-frame mean | 0.59433 ms | 0.80000 ms |
| Full GPU-frame p50 | 0.5240 ms | 0.6500 ms |
| Full GPU-frame p95 | 1.3240 ms | 1.5000 ms |
| Scene p50 | 0.4930 ms | 0.6000 ms |
| Opaque-world p50 | 0.3180 ms | 0.4000 ms |
| Opaque-entity p50 | 0.1730 ms | 0.2250 ms |
| Draw mean / p95 | 18 / 18 | 18 / 18 |
| Upload bytes mean / p95 | 4,800 / 4,800 | 4,800 / 4,800 |

For context only, OpenGL records 38 draws and 1.03126 ms CPU mean on the same
collection, while its comparable full-frame GPU p50 is 0.3230 ms. The native
CPU/submission gain is measured; the GPU delta remains open optimization work
and is deliberately not added as a Vulkan/OpenGL ratio threshold.

## Reproduction

```text
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg --fixture assets/maps/worr_fr01_bmodel_instances.bsp --scenario-id fr01-bmodel-instance-grid-current --run-root .tmp/renderer-parity/fr01-bmodel-instances-current --hardware-id "Intel(R) Iris(R) Xe Graphics" --driver local-headless --timeout 180 --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-bmodel-instances-current/vulkan.log --opengl .tmp/renderer-parity/fr01-bmodel-instances-current/opengl.log --capture-manifest .tmp/renderer-parity/fr01-bmodel-instances-current/capture.json --warmup 20 --min-samples 100 --budget assets/renderer_parity/fr01_renderer_perf_bmodel_instances_gpu_budget.json
```

The collector always uses `win_headless=1` with input disabled; it does not
launch or control a visible client window.
