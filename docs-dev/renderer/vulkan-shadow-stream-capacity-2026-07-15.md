# Native Vulkan Shadow Stream Capacity Growth

Date: 2026-07-15

Task ID: `FR-02-T14`

Status: partial implementation. The shadow system still uses fixed page and
light budgets pending active-capacity buckets, transactional allocation,
capability-correct sampler selection, dirty-layer mip generation, and
alpha-tested caster materials. Its transient caster vertex stream now avoids
resize-driven allocation spikes and uses device-local draw storage; the
follow-up is documented in
`vulkan-shadow-device-local-stream-validation-2026-07-15.md`.

## Outcome

`VK_Shadow_EnsureVertexBuffer` previously destroyed and recreated its
persistently mapped host-visible vertex buffer at the exact byte count of each
new shadow-caster high-water mark. A frame that introduced more shadow-casting
world geometry or entities could therefore incur unmap, destroy, free, create,
allocate, bind, and map work in its shadow-recording path. The later
device-local follow-up retains this growth policy but stages the live range
before binding a frame-local destination buffer for drawing.

The native stream now starts at 64 KiB and doubles until it holds the requested
vertex bytes, with overflow-safe capacity arithmetic. Only the live vertex
range is copied and reported through `VK_DEBUG_DOMAIN_SHADOW`; spare capacity
does not inflate draw ranges or per-frame upload telemetry.

## Performance and safety boundary

This makes successive capacity increases amortized rather than reallocating on
every incremental high-water mark. The device-local follow-up partitions the
stream by frame in flight, preserves shadow page/job ordering, and records the
transfer-to-vertex-input dependency explicitly. It neither changes shadow
rendering output nor claims active shadow capacity scaling.

The buffer path remains entirely native Vulkan and does not use an OpenGL
renderer fallback.

## Headless validation

`tools/renderer_parity/test_vulkan_shadow_stream_growth_source.py` checks the
minimum capacity, geometric/overflow-safe growth, capacity-backed Vulkan
allocation, live-range upload accounting, and native-only boundary. The build
and source test are non-interactive:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_shadow_stream_growth_source.py
```
