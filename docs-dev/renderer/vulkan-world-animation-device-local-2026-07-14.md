# Vulkan World Animation and Device-Local Storage

Date: 2026-07-14

Task IDs: `FR-01-T10`, `FR-01-T13`

Status: partial implementation; flowing and turbulent warp are complete.
The native transparent-liquid refraction and full-screen waterwarp follow-up
is recorded in `vulkan-liquid-refraction-2026-07-15.md`.

## Outcome

The Vulkan static-world vertex stream is now immutable after map registration.
It lives in device-local memory and is filled through a one-time host-visible
staging buffer plus transfer command. The registration path waits for that
copy to complete before releasing the staging allocation; this is intentional
map-load work, not a frame-path synchronization point.

The previous CPU animation path retained a second world-sized vertex array,
walked every vertex each frame to rewrite flow/warp UVs, and copied the
complete stream back to the GPU. It has been removed. The registration CPU
array is released immediately after its device-local upload.

`vk_world.c` instead owns an eight-byte, persistently mapped instance stream:

```text
float time
uint32 effects_enabled
```

It uploads exactly those eight bytes once per rendered frame. The static
world buffer is never mapped or rewritten after registration.

## Native shader contract

The visible native Vulkan world pipeline now binds the static world vertex
stream at binding zero and the compact frame stream at instanced binding one.
The animated vertex shader applies the OpenGL `GL_ScrollPos` contract:

- ordinary `SURF_FLOWING`: `uv.x -= 1.6 * time`;
- `SURF_FLOWING | SURF_WARP`: `uv.x -= 0.5 * time`.

The fragment shader then applies turbulent warp per pixel, after that scroll:

```text
uv += 0.0625 * sin(uv.yx * 4.0 + time)
```

This placement is material. Applying the sine only at face vertices flattens
the wave over large BSP polygons; OpenGL applies it in its fragment shader.
The `effects_enabled` bit preserves the `vk_shaders 0` non-animated fallback
without rebuilding geometry.

The same shader source is compiled twice by `tools/gen_vk_world_spv.py`:
an animated visible-world module and an unanimated shadow module. Shadow maps
therefore retain their original vertex interface and alpha-test sampling;
Vulkan is not redirected through OpenGL.

## Paired regression scene

`tools/renderer_parity/generate_warp_flow_fixture.py` owns a small BSP with a
full-screen opaque background and a front `SURF_WARP | SURF_FLOWING` plane
using a high-contrast checker. Its config disables OpenGL refraction solely
to isolate the shared base flow/warp contract.

The config sets the existing cheat-only `fixedtime` control to 16 ms before
loading the map and pauses before capture. Renderer creation has different
startup costs, so ordinary wall-clock frame deltas make time-based screenshots
non-deterministic. Fixed simulation time makes both captures compare the
same shader phase; it is reset before exit. The scene is mirrored loose and
packaged in `pak0.pkz` for staged runtimes that cannot read compressed assets.

## Validation

Completed from the refreshed `.install` staging root:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/test_package_assets.py
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python tools/renderer_parity/generate_warp_flow_fixture.py --asset-root assets --validate --json
python -u tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_warp_flow_manifest.json --run-root .tmp/renderer-parity/fr01-warp-flow-fixedtime-pair-a --vulkan-validation
python -u tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_warp_flow_manifest.json --run-root .tmp/renderer-parity/fr01-warp-flow-fixedtime-pair-b --vulkan-validation
python -u tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_warp_flow_manifest.json --run-root .tmp/renderer-parity/fr01-warp-flow-device-local-fixed --vulkan-validation
python -u tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_warp_flow_manifest.json --run-root .tmp/renderer-parity/fr01-warp-flow-device-local-fixed-repeat --vulkan-validation
```

All four paired captures pass with no process or Vulkan-validation failures.
The two device-local captures are repeatable. The measured 640 by 480 crop
has 307,200 pixels; the final device-local run
has maximum and mean absolute RGB error `[0, 0, 0]`, with zero pixels above
the threshold of 8. The independent native Vulkan debug-overlay smoke also
passes after this change.

## Remaining work

- `FR-01-T10`: native transparent-liquid refraction and OpenGL-equivalent
  underwater/full-screen waterwarp and entity alpha phases now have dedicated
  Vulkan passes. A compliant no-window paired visual scene remains open; see
  `vulkan-liquid-refraction-2026-07-15.md`.
- `FR-01-T13`: map registration currently performs a synchronous one-time
  staging copy. A future upload scheduler may make that asynchronous, but it
  must preserve safe ownership across map changes and frames in flight.
- `FR-01-T15`: capture before/after CPU and GPU timings for representative
  maps. The byte reduction is structural and visible in `vk_stats`, but this
  change does not claim a completed performance budget.
