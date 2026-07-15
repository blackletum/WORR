# Native Vulkan Indexed MD2 and MD5 Submission

Date: 2026-07-15

Task ID: `FR-01-T14`

Status: partial implementation. Standard MD2 and MD5 model instances use a
native indexed Vulkan stream, while eligible MD2 models use device-local static
geometry and GPU interpolation and eligible MD5 meshes use device-local static
mesh/weight resources with native GPU vertex skinning. This is not a claim of
full model/effect static residency or a measured end-to-end performance win.

## Implemented path

The legacy entity path constructed a fully transformed `vk_vertex_t` for every
triangle index, then copied that expanded triangle list to the current frame's
mapped vertex buffer. Standard MD2 and MD5 rendering now instead:

1. resolves the same animation-frame interpolation, lighting, shell, glowmap,
   alpha, and transform inputs as before; normal MD5 fallback meshes skin on
   the CPU while eligible MD5 meshes upload only a joint palette for GPU vertex
   skinning;
2. writes one transformed vertex for each source MD2 vertex;
3. appends the original validated triangle index order as local `uint16_t`
   values;
4. uploads that compact vertex range and index range to the current native
   frame context; and
5. binds `VK_INDEX_TYPE_UINT16` and records `vkCmdDrawIndexed` with that
   batch's first vertex as the Vulkan vertex offset.

The native source MD2/MD5 mesh index type is already 16-bit. Keeping it that
way halves index-stream staging and device-local transfer bytes compared with
the earlier widened `uint32_t` stream. Adjacent compatible batches still
coalesce, but only while their combined vertex range fits the 16-bit relative
index address space. The newly appended index range is rebased to the merged
batch's first vertex; otherwise it starts a new indexed draw. Checked bounds
retain a clear failure rather than silently wrapping a large scene.

The new index stream has a device-local target plus a persistently mapped upload
buffer, both owned per bounded frame slot alongside the vertex stream. Its live
bytes are included in the Vulkan entity upload telemetry. The batch retains its
first vertex and first index independently, so descriptor, blend, depth-hack,
and liquid-order routing remain unchanged.

## Device-local stream residency

The frame-local entity vertex and index targets are now allocated in
`VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` memory with transfer-destination usage.
Each target has a matching persistently mapped, host-visible/coherent staging
buffer in the same bounded frame slot. `VK_Entity_RenderFrame` copies only the
live CPU vertex/index ranges into staging; command recording then copies those
ranges to the device-local targets before any render pass and inserts a
transfer-to-vertex-input buffer barrier. The vertex target becomes visible to
vertex-attribute reads and the index target to index reads.

This keeps PCIe/host-visible memory out of the graphics fetch path on discrete
GPUs while retaining no-stall ownership: the frame slot fence is waited before
either its staging or device-local resources are reused. The transfer is part
of the normal native Vulkan command submission—there is no immediate queue
idle, renderer fallback, or OpenGL path.

The same command buffer now writes a dedicated Vulkan timestamp immediately
after the transfer/barrier work. `vk_stats` exposes this completed-frame value
as `gpu_upload_ms`, separating transfer cost from native shadow, scene, and
post-process timings.

## Compatibility boundary

`RF_ITEM_COLORIZE` and `RF_OUTLINE` model instances intentionally stay on the
previous expanded path. Those features make subsequent passes reuse and mutate
exact triangle ranges for their base, overlay, stencil, and shell stages.
Keeping that established path avoids changing visual ordering while the shared
indexed batch representation is introduced. Other dynamic entity/effect
primitives remain on the expanded stream for now.

No Vulkan path is routed through OpenGL.

## Expected cost change and remaining work

For standard MD2 and MD5 meshes, per-frame vertex generation and vertex upload
scale with the source vertex count rather than the triangle index count. The
index upload scales with the original index count and triangle order is
preserved. This can reduce CPU transform copies and GPU vertex-shader
invocations on meshes that reuse vertices, but the repository does not yet
contain compliant paired runtime telemetry to quantify the gain.

The completed MD2 follow-up is documented in
`docs-dev/renderer/vulkan-gpu-md2-submission-2026-07-15.md`; the MD5
vertex-skinning follow-up is in
`docs-dev/renderer/vulkan-gpu-md5-skinning-2026-07-15.md`. The next
`FR-01-T14` slice is a general bounded transient allocator. Any later
optimization must retain the fallback paths until equivalent material, outline,
and colorize evidence is available.

## Headless validation

The focused source test verifies the compact `uint16_t` index-buffer ownership
and allocation, validated local MD2/MD5 index emission, vertex-offset indexed
draw commands, checked 16-bit batch rebasing, device-local staging/copy/barrier
ownership, and the explicit compatibility fallback.
Validation did not launch a client window:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_entity_stream_growth_source.py
```

This build refreshed `.install/`. A representative paired OpenGL/Vulkan
benchmark remains required before declaring a performance budget or superiority.
