# Vulkan Inline-BSP Instance Coalescing

Date: 2026-07-15

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: implemented for compatible opaque inline-BSP face ranges. This is a
validated, provenance-bound submission improvement for a dense deterministic
scene, not closure of the renderer-wide performance objective or a general GPU
budget.

## Problem

The native static-inline-BSP path already placed immutable face geometry in a
device-local buffer and emitted a compact 128-byte transform/light record per
entity. Collection remained entity-major, however: every visible face range of
each ordinary door, platform, or `func_wall` became a separate Vulkan draw.

That preserves the legacy order, but it prevents Vulkan's per-instance input
layout from doing useful work when many adjacent entities use the same static
mesh. The first small fixed-view benchmark cannot expose the issue because it
has only one ordinary bmodel.

## Native Vulkan design

`VK_Entity_CoalesceGpuBmodelBatches` now compacts compatible GPU-BSP batches
after entity collection and before uploads/recording. It combines only ranges
that have the same descriptor set, source vertex range, flags, phase, and a
contiguous instance range. The existing draw call already supplies
`firstInstance` and `instanceCount`; the bind-time validation was expanded to
accept the resulting range.

The coalescer is deliberately conservative:

- Only opaque, non-alpha-tested, non-additive GPU-BSP batches can merge.
- Alpha/blended, alpha-tested, depth-hack, outline, shell, rimlight, and item
  colourize behavior stays on its established native path and ordering.
- No OpenGL route or compatibility renderer is involved.
- The pass compacts the existing frame batch array in place and makes no
  transient allocation. Its current linear search is bounded by the collected
  batch list; broader GPU-driven/indirect submission remains open work.

## Dense deterministic coverage

`worr_fr01_bmodel_instances.bsp` contains the base bmodel plus a 6x6 grid of
ordinary `func_wall` entities that all reference `*1`. The shared source mesh
is therefore immutable while each entity still supplies its own transform.
The paired visual manifest compares the central authored view volume, where
all 37 instances are present and the two renderers are pixel-identical.

The deliberately very-wide-FOV full frame includes geometry outside the small
background plane. That edge region differs between OpenGL and Vulkan even with
the coalescer disabled, so it is retained as a separate renderer-parity issue
rather than being attributed to this optimization. The validated crop is
`[220, 150, 520, 420]` and passed under validation with:

```text
pixels compared:              218,400
maximum / mean RGB error:     [0, 0, 0] / [0, 0, 0]
pixels over threshold 8:      0
inline-BSP mask IoU:          1.0 (179,742 pixels on each backend)
Vulkan validation diagnostics: none
```

## Paired telemetry evidence

Both captures use a hidden native 960x720 surface, 120 matching-adapter
samples per renderer, a 20-sample warm-up trim, Vulkan validation, and the
same recorded hardware/driver provenance:

```text
adapter: Intel(R) Iris(R) Xe Graphics
CPU:     13th Gen Intel(R) Core(TM) i7-13700H
OS:      Windows 11 Home 10.0.26200
driver:  Intel 31.0.101.5590 (2024-06-10)
```

The uncoalesced control at
`.tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-batched` and the
coalesced result at
`.tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-coalesced` use the
same fixture/configuration hashes. The control is the immediately preceding
native implementation, before the post-collection coalescer was introduced.

| Metric | Uncoalesced Vulkan | Coalesced Vulkan | Change |
|---|---:|---:|---:|
| Draws mean / p95 | 98 / 98 | 18 / 18 | -80 (-81.63%) |
| Upload mean / p95 | 4,800 / 4,800 bytes | 4,800 / 4,800 bytes | unchanged |
| CPU mean | 0.75460 ms | 0.54163 ms | -28.22% |
| CPU p95 | 0.963 ms | 0.722 ms | -25.03% |
| GPU mean | 1.39616 ms | 1.28962 ms | -7.63% |
| GPU p95 | 1.934 ms | 1.318 ms | -31.85% |

The coalesced paired OpenGL run reports 38 draws and a CPU mean of `0.77710`
ms. Vulkan's `0.54163` ms CPU mean is therefore `0.697x` OpenGL (30.30% lower)
for this exact dense scene. This supports a narrowly scoped CPU-submission
superiority result; it does not justify a renderer-wide claim.

Vulkan GPU timing remains `1.28962` ms versus OpenGL's `0.01427` ms. The
renderers have different timestamp phase scopes and batching decomposition, so
this value is not used as a cross-renderer GPU conclusion or a budget.

## Verification

```text
python tools/renderer_parity/generate_bmodel_instance_fixture.py --validate
Push-Location tools/renderer_parity
python -m unittest test_generate_bmodel_instance_fixture.py test_vulkan_gpu_bmodel_submission_source.py
Pop-Location
meson compile -C builddir-win
python tools/stage_install.py --build-dir builddir-win --assets-dir assets --install-dir .install --base-game basew
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_instances_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-instances-coalesced --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg --fixture assets/maps/worr_fr01_bmodel_instances.bsp --scenario-id fr01-bmodel-instance-grid-telemetry --run-root .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-coalesced --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" --driver "<display-driver>" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-coalesced/vulkan.log --opengl .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-coalesced/opengl.log --capture-manifest .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-coalesced/capture.json --warmup 20 --min-samples 100
```

## Remaining work

General transient-ring allocation, large-scene indirect submission, special
entity/effect batching, representative-map capture families, and reconciled
GPU timing scope remain open under `FR-01-T14`/`FR-01-T15`.
