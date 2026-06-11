# OpenGL GPU-Offload Baseline Instrumentation

Task: `DV-05-T03`

Implemented the first prioritized item from `docs-dev/plans/renderer-gpu-offload.md`: baseline instrumentation and debug markers for the OpenGL renderer. The goal is to make future GPU-offload work measurable before changing streaming, particles, submission, or lighting architecture.

## Runtime controls

- `gl_debug_markers` defaults to `1`. When `GL_KHR_debug` / OpenGL 4.3 debug groups are available, WORR emits named ranges for frame, render frame, world, brush/opaque/transparent/effect/flares/debug/postfx phases, lightmap upload, particles, beams, and alias models.
- `gl_cpu_timers` defaults to `0`. Set to `1` to collect high-resolution CPU timings for renderer hot scopes.
- `gl_gpu_timers` defaults to `0`. Set to `1` to collect delayed `GL_TIME_ELAPSED` query results for world opaque, lightmap update, effects, transparent surfaces, and postfx.
- `gl_profile_log` defaults to `0`. Set to a positive frame interval to print a stable profile record every N frames. This also enables CPU timers so the log is self-contained.

GPU timer readback is intentionally delayed. Query results are polled only after at least two later frames, and unavailable results remain pending instead of forcing a same-frame stall. If a query slot is still pending when a new scope wants to reuse it, the frame records a missed GPU timer rather than blocking.

## Telemetry block

Added `glFrameTelemetry_t gl_telemetry` as the stable per-frame renderer telemetry snapshot. It captures:

- existing renderer counters from `statCounters_t`,
- visible nodes, entity/dlight/particle totals,
- CPU timings for frame, mark leaves, mark lights, world traversal, solid face draw, light pushes, lightmap uploads, particles, beams, alias models, and postfx,
- last completed GPU timings for the five GPU scopes,
- delayed GPU-query state (`gpu_pending`, `gpu_missed`, `gpu_available_mask`),
- fast-path flags for shader path, GPU alias lerp, static world VBO, and per-pixel-lighting use,
- streamed VBO/EBO bytes and texture-upload bytes.

The existing `renderer` stats panel now shows the new stream-byte counters, fast-path flags, and key CPU/GPU timing values.

## Instrumented hot paths

CPU timers were added around `GL_MarkLeaves`, `GL_MarkLights`, the top-level `GL_WorldNode_r` traversal, `GL_DrawSolidFaces`, `GL_PushLights`, `GL_UploadLightmaps`, `GL_DrawParticles`, `GL_DrawBeams`, `GL_DrawAliasModel`, and the postfx block in `R_RenderFrame`.

GL debug groups were added around the frame and major render phases so Nsight, RenderDoc, and driver captures can attribute work to the same named ranges used by the in-engine telemetry.

Streamed byte accounting was added where the current transient path calls `qglBufferData` for dynamic vertex and index uploads. Texture-upload bytes are now counted for lightmap subimage uploads and renderer-owned RGBA texture updates.

## Notes

This does not optimize the renderer yet. It establishes the baseline measurement layer needed for the next roadmap steps: fast-path audit, persistent streaming buffers, instanced particles/beams, async lightmap uploads, and submission cleanup.
