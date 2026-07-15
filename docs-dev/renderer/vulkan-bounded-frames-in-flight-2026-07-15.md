# Vulkan Bounded Frames in Flight

Date: 2026-07-15

Task ID: `FR-01-T15`

Status: partial implementation. The native Vulkan raster renderer can now
prepare one frame while the preceding frame executes on the GPU. This removes
the former unconditional one-frame CPU/GPU serialization, but it is not yet
benchmark evidence of a gain over OpenGL or a completed performance budget.

## Ownership model

`VK_MAX_FRAMES_IN_FLIGHT` is deliberately fixed at two. Swapchain creation
caps the active count by the number of available images. Each active frame
context owns:

- one primary command buffer;
- one image-available semaphore;
- one signalled-on-create in-flight fence; and
- one depth image/view and a framebuffer for every swapchain image; and
- one sampled scene-copy image/view/descriptor; and
- one bloom ping-pong image pair and its external descriptors; and
- one depth-only sampled view when the selected depth format supports it; and
- one gameplay-DOF quarter-resolution ping-pong pair, full-resolution
  composite image, and external descriptors; and
- submitted-state used to resolve the slot's completed GPU timing range.

Presentation completion remains associated with the swapchain image, so each
image keeps its own render-finished semaphore. `image_frame_slots[]` records
which frame slot last submitted each image. Before a new frame records, only
its own slot fence is waited. After acquisition, if a different slot last used
that image, that owner slot is retired before command recording. The result is
bounded reuse with no command-buffer or presentation-semaphore aliasing.

## Frame-local transient resources

Removing the global wait safely required more than replacing one fence. The
following host-visible or query-backed paths are partitioned by frame slot:

- UI vertex and index uploads;
- entity/effect vertex uploads;
- compact world animation frame data;
- shadow uniform data, shadow caster vertex uploads, and shadow descriptors;
- debug line vertex uploads; and
- timestamp query ranges and pending/recorded state.

Depth attachments are also frame-local. Each frame slot therefore has a
framebuffer for every swapchain colour image, pairing that image with only the
depth image that the slot may write. This is required even with no
post-process effect: two queue submissions rendering separate swapchain images
must never depth-test/write the same attachment concurrently.

The CPU writes the current slot only after that slot fence has signalled. The
previous slot can therefore remain in GPU execution without a host write to
memory that its recorded command buffer reads. Existing flare query handling
continues to be asynchronous: it only resets a query when a later queue-ordered
command records that reset, and it never introduces a host wait.

## Shared-resource rebuild boundary

The scene-copy image, view, and base descriptor are frame-local, so liquid
refraction plus ordinary waterwarp/colour/CRT composition can overlap without
sampling a preceding slot's scene. Each scene-copy image tracks its own first
use layout as well as its fence ownership.

Bloom ping-pong images plus final/LUT/bloom external descriptors are frame
local. A LUT image change advances a descriptor generation; each slot rebuilds
only its descriptor sets after its own fence has signalled. A bloom
enable/disable similarly updates only the reusable slot. A bloom size/format
rebuild replaces every slot's ping-pong pair, so it remains a rare global
retirement point before replacement.

The depth-aware DOF images and descriptors are also frame-local. Their layout
state follows the frame slot, so a gameplay-focus frame can overlap the
preceding slot's command buffer without cross-frame render-target writes.

`VK_PostProcess_NeedsSafeResourceUpdate` additionally detects LUT descriptor
changes, bloom enable/disable transitions, and bloom/DOF dimension or rebuild
changes. Its resource replacement shares the same retirement boundary before
`VK_PostProcess_PrepareFrame` destroys or creates images/descriptors.

Shadow page reallocation already uses `vkDeviceWaitIdle` on its rare format,
storage-family, or resolution transition, so its shared image/view/pipeline
replacement remains valid with two slots. Normal shadow draws use the
frame-local uniform, descriptor, and vertex resources listed above.

## Performance boundary

This work removes the ordinary-frame CPU fence wait and creates safe ownership
for overlap on liquid refraction, colour/LUT/CRT presentation, bloom, and
depth-aware DOF. It does not make a claim about frame time, latency, throughput, or
superiority over the OpenGL renderer. The remaining `FR-01-T15` work must use
reproducible paired runs on the same scene sequence to record CPU and GPU phase
time, draw/batch count, and upload bytes before and after the change. FPS alone
is not sufficient.

The implementation does not by itself resolve the remaining `FR-01-T14`
CPU-expanded special-model/effect submission, general transient-ring work,
dynamic resolution scaling, or the outstanding visual-parity capture work.
Ordinary inline BSP residency is now native and device-local; see
`vulkan-static-inline-bsp-residency-2026-07-15.md`.

## Headless verification

The source-level regression is intentionally non-interactive and checks the
two-slot scheduler, swapchain-image ownership, frame-local transient
allocations, per-slot timestamp ranges, post-process rebuild drain, and lack
of an OpenGL renderer route:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_frames_in_flight_source.py
```

The full focused Vulkan source suite additionally covers native color
correction, bloom, CRT, entity/shadow stream growth, GPU timing, fog, glowmaps,
and liquid refraction. Windows capture runners now select the explicit,
non-archived `win_headless 1` native-surface mode, which hides the client HWND,
does not claim focus, forces windowed operation, and leaves desktop hardware
gamma unchanged. Runtime visual validation remains a separate compliant
no-window capture activity. The first hidden native Vulkan debug/telemetry
smoke passed with validation enabled, but paired visual and performance
evidence is still pending. See `windows-headless-renderer-capture-2026-07-15.md`.

## Paired budget collection foundation

OpenGL now exposes a matching `gl_stats` command and can emit its
machine-readable `GL_STATS` record at an opt-in `gl_profile_log` interval;
`vk_stats_log` supplies the equivalent native Vulkan interval output. Vulkan
CPU submission timing is now microsecond-resolution rather than rounded to
integer milliseconds. `tools/renderer_parity/run_renderer_perf_capture.py`
collects both logs through the hidden native surface, requires that both logs
identify the same adapter, hashes the executed config tree/launch profile, and
writes the provenance manifest. `analyze_renderer_perf.py` consumes those
saved logs, discards an explicit warmup interval, and writes mean/p95
CPU/GPU/draw/upload summaries plus Vulkan/OpenGL timing ratios.

The analyzer is deliberately collection-only at this stage: no unmeasured
hardware threshold is encoded as a passing claim. Its paired-capture manifest
hashes the fixture/configuration and exact telemetry logs, and records the
hardware/driver identity before a budget can run. The first capped,
adapter-matched fixed-view run produced 100 valid post-warmup samples per
renderer but showed Vulkan slower in that intentionally small workload; it
therefore establishes a regression target, not a budget. Repeated
representative headless samples must still establish the actual threshold
before this task can claim superior performance; see
`vulkan-paired-fixed-view-telemetry-2026-07-15.md` and
`vulkan-paired-performance-capture-contract-2026-07-15.md`.

After those samples exist, pass a repository-owned JSON budget through
`--budget`. Its schema requires `schema_version: 1` and can require minimum
sample count, valid GPU samples, and maximum Vulkan/OpenGL ratios for mean or
p95 CPU/GPU time. This makes a regression a failing artifact rather than an
informal FPS observation; it does not create a threshold before hardware
evidence exists.
