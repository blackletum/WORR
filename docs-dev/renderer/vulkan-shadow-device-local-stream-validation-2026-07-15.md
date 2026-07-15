# Native Vulkan Device-Local Shadow Stream and Validation Repair

Date: 2026-07-15

Task ID: `FR-02-T14`

Related automation tasks: `FR-10-T11`, `FR-10-T14`, `FR-10-T15`

Status: partial `FR-02-T14` implementation; the transient caster stream is
native, device-local, frame-safe, and runtime-validated. Fixed page arrays,
active capacity/resolution buckets, transactional allocation,
capability-correct samplers, dirty-layer mip generation, and alpha-tested
caster materials remain open.

## Outcome

The Vulkan shadow backend previously drew the transient caster vertex stream
directly from a persistently mapped host-visible buffer. It now owns a matched
pair of buffers for every frame in flight:

- a host-visible, coherent staging buffer receives only the live caster range;
- a `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` vertex/transfer-destination buffer
  is the only buffer bound by `VK_Shadow_Record`.

`VK_Shadow_RecordUploads` copies the active range before the shadow passes and
inserts an explicit transfer-write to vertex-input-read buffer barrier. The
primary Vulkan command recording path calls it after entity uploads and before
the shadow backend records page draws. A frame cannot bind the destination
until that frame's copy was recorded. This keeps the data path native Vulkan,
avoids an OpenGL route, and moves high-frequency raster reads to device-local
memory without changing caster selection, page order, or depth-materialization
semantics.

The existing 64 KiB geometric capacity policy remains in force for both buffers
in a frame slot. Capacity spare space is never copied, counted as an upload, or
drawn.

## Descriptor-lifetime correction

Vulkan validation exposed a separate pending-use error while loading the
flashlight scene: GPU MD5 skinning rewrote every frame-indexed descriptor set
when preparing one frame. The other slots may still be referenced by submitted
command buffers. `VK_Entity_UpdateMd5DescriptorSets` now updates only the
current fence-safe frame slot; the first use of each slot initializes it after
its fence wait and before its command buffer is recorded.

Ordinary UI image-pixel updates also no longer rewrite an unchanged descriptor
set. Paired glow-map discovery changes the material's second image binding, so
it first retires outstanding device work before rewriting that descriptor. This
preserves the existing native glow-material relationship without updating
pending descriptors.

## Headless safety repair

The validation launcher uses `win_headless 1`, `in_enable 0`, and `in_grab 0`.
That intentionally skips input-grab cvar setup, but local-map activation still
calls `IN_Activate`. `IN_GetCurrentGrab` now fails closed when input is disabled,
headless, or has no grab cvar, preventing the prior null dereference during the
connection-to-game transition. This is an engine automation guard; it does not
alter interactive input behavior.

## Verification

The staged Windows build and focused structural checks passed:

```text
python -m unittest tools/networking/test_headless_input_contract.py
python -m unittest tools/renderer_parity/test_vulkan_gpu_md5_submission_source.py
python -m unittest tools/renderer_parity/test_vulkan_glowmaps_source.py
python -m unittest tools/renderer_parity/test_vulkan_shadow_stream_growth_source.py
python -m unittest tools/renderer_parity/test_shadowmapping_repro_runner.py
ninja -C builddir-win worr_engine_x86_64.dll worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

The isolated non-interactive runtime scene was then run with
`VK_LAYER_KHRONOS_validation` enabled:

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadowmapping-repro/device-local-shadow-validation-clean \
  --renderer vulkan --scene flashlight-owner --filter pcf --wait 90 \
  --vulkan-validation
```

Its captured process log contains no `VUID`, validation-error, or error text.
The completed `r_shadow_dump` reports the native Vulkan depth-compare 2D-array
backend with 25 pages, five selected lights, 25 views, and 12 dynamic casters.
That exercises the new transfer path rather than merely compiling it.

The matching isolated OpenGL run reports the same shared-frontend workload:
five selected lights, 25 views, and 12 dynamic casters. The backends retain
their own native page formats and world-caster totals, so this is a functional
selection/caster parity check rather than a pixel-comparison claim.

## Remaining boundary

This improves the transient vertex stream only. It is not evidence that the
whole shadow system is budgeted or that Vulkan is globally faster than OpenGL.
Future `FR-02-T14` work must replace the fixed page/light budgets and cover
alpha-tested caster materials with representative parity captures.
