# Vulkan Inline-BSP Performance Budget

Date: 2026-07-15

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: first environment-bound native Vulkan CPU-submission budget active.

## Scope

`assets/renderer_parity/fr01_renderer_perf_bmodel_instances_budget.json`
protects the dense 6-by-6 ordinary inline-BSP instance scene after native
opaque face-range coalescing. It is intentionally not a renderer-wide speed
claim and not a cross-renderer GPU budget: the Vulkan and OpenGL timestamp
phase scopes differ.

The budget binds to the exact scenario ID, fixture and configuration SHA-256
hashes, Intel Iris Xe / i7-13700H / Windows 11 hardware identifier, and Intel
31.0.101.5590 driver. It rejects a capture from any other provenance before
using its measurements. Both renderer logs must contain at least 100 valid-GPU
samples, even though only the Vulkan CPU and submission limits are asserted.

## Limits

For the pinned scenario, Vulkan must remain within:

- CPU mean at or below `0.7 ms` and p95 at or below `1.0 ms`;
- 18 mean and p95 draws;
- 4,800 mean and p95 upload bytes;
- Vulkan/OpenGL CPU-mean ratio at or below `0.8`.

`analyze_renderer_perf.py` now supports `capture_contract` for manifest
provenance equality and `vulkan_max` for raw native metrics, in addition to
the existing ratio limits. The analyzer fails if required metrics are missing,
the capture contract differs, or any bound is exceeded.

## Current evidence

The validation-enabled 120-sample hidden-surface capture at
`.tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-current` passes the
budget after its 20-sample warm-up trim:

| Metric | Vulkan | OpenGL | Result |
|---|---:|---:|---|
| CPU mean | 0.54184 ms | 1.62964 ms | 0.33249 ratio |
| CPU p95 | 0.686 ms | 3.089 ms | within Vulkan cap |
| Draws mean / p95 | 18 / 18 | 38 / 38 | within Vulkan cap |
| Upload mean / p95 | 4,800 / 4,800 bytes | 0 / 0 bytes | within Vulkan cap |
| Valid GPU samples | 100 | 100 | required provenance condition met |

The reported Vulkan GPU mean (`1.2308 ms`) is retained in the capture report
but is deliberately excluded from the budget and any OpenGL comparison.

## Validation

```text
python -m unittest tools/renderer_parity/test_analyze_renderer_perf.py
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg --fixture assets/maps/worr_fr01_bmodel_instances.bsp --scenario-id fr01-bmodel-instance-grid-telemetry --run-root .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-current --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-current/vulkan.log --opengl .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-current/opengl.log --capture-manifest .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-current/capture.json --warmup 20 --min-samples 100 --budget assets/renderer_parity/fr01_renderer_perf_bmodel_instances_budget.json
```

The collector launches only hidden native renderer surfaces; it never launches
an interactive client.
