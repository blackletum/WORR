# Native Vulkan UI Device-Local Geometry Stream

Date: 2026-07-15

Task ID: `FR-01-T15`

Status: partial performance-modernization implementation. The UI geometry
stream is device-local and validation-backed; representative-map performance
budgets and a measured gain over OpenGL remain open.

## Outcome

Vulkan UI and RmlUi geometry previously copied directly into a mapped
host-visible vertex and index buffer, then bound that memory for rasterization.
Each frame slot now owns two native Vulkan pairs:

- device-local vertex/index draw buffers with transfer-destination usage; and
- persistently mapped host-visible, coherent staging buffers with
  transfer-source usage.

After UI construction is complete, `VK_UI_RecordUploads` copies only the live
vertex and index ranges into the current frame slot's device-local buffers. An
explicit transfer-write to vertex-input-read barrier makes the ranges visible
before any scene, liquid, post-process, or overlay UI pass can bind them.
`VK_UI_Record` will not draw until that current-frame upload was recorded.

The renderer waits the current frame fence before UI construction, so both the
staging writes and device-local reuse are slot-safe. Capacity remains owned per
frame slot and follows the existing bounded/doubling CPU draw-list capacity
growth. The work is entirely native Vulkan; no OpenGL renderer path is used.

## Performance boundary

The change removes host-visible memory from the UI draw path, which is
particularly relevant for large RmlUi menus and text-heavy overlays on discrete
GPUs. It adds one vertex and one index buffer copy per UI-bearing frame. No
cross-renderer time reduction is claimed here: the current fixed-view
performance fixture intentionally disables RmlUi and reports zero UI upload
bytes, so it is not a suitable measurement for this stream. A representative
overlay workload and manifest-gated OpenGL/Vulkan budget remain `FR-01-T15`
work.

## Verification

Focused structural coverage verifies the device-local allocation, staging
pairing, live-range copies, transfer-to-vertex-input barrier, upload-before-
draw ordering, and primary-command placement:

```text
python -m unittest tools/renderer_parity/test_vulkan_ui_device_local_stream_source.py
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

The existing guarded RmlUi runtime capture was then run headlessly with
`VK_LAYER_KHRONOS_validation` enabled:

```text
python tools/ui_smoke/check_rmlui_runtime_capture.py --install-dir .install \
  --renderer vulkan --evidence-dir .tmp/rmlui/ui-device-local-vulkan \
  --evidence-id ui_device_local_vulkan --run --format json
```

The capture completed with exit code zero, no validation diagnostics, a fresh
960x720 screenshot, all layout assertions passing, and 372 native RmlUi render
frames. Its synthetic key, character, pointer, button, and wheel input path
also completed, confirming that the staged UI geometry supports real routed UI
updates rather than an empty overlay.

## Remaining work

This does not provide a full UI visual parity matrix or a representative
performance budget. The overlay-bearing reproducible capture now exists and is
recorded in `vulkan-rmlui-overlay-paired-telemetry-2026-07-15.md`; it remains
collection evidence only until repeated driver-recorded paired timing runs
establish a budget. Its durable visual matrix now covers exact core runtime
parity plus bounded main-shell, performance-settings, and confirmation-popup
parity; broader HUD, session-popup, and input-state coverage remains
`FR-02-T05` work.
