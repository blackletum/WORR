# Renderer startup log cleanup (2026-03-27)

Task ID: `FR-02-T08`

## Summary
Cleaned up the `q2dm1` local launch smoke log so real startup failures remain visible instead of being buried under false-positive warnings and a brittle OpenGL postfx initialization path.

## Issues addressed
- `SpawnEnt_MapFixes: missing data; skipping map fixes` was logged for large numbers of normal map entities that do not carry a model key.
- `PF_Client_Print to a free/zombie client 0` was emitted during server loading for the local host slot before the client was fully connected.
- OpenGL post-processing could fail with `FBO_SCENE framebuffer status 0x8cdd (GL_FRAMEBUFFER_UNSUPPORTED)` on unsupported DOF depth/stencil attachment combinations, which disabled the whole postfx path.
- Follow-up local validation showed that the sound-only frame path could call `CL_CalcViewValues` before cgame entity extensions were available, producing repeated startup log noise while a local connect was still incomplete.

## Implementation
- `src/game/sgame/gameplay/g_spawn.cpp`
  - Relaxed `SpawnEnt_MapFixes` validation so the function only warns on missing classname data by default.
  - Kept the `bunk1` map fix path targeted to `func_button` entities that actually require a model key.
- `src/game/sgame/gameplay/g_map_manager.cpp`
  - Tightened connected-client checks so map-pool/cycle messaging only targets live connected clients.
- `src/server/game.c`
  - Suppressed `PF_Client_Print` free/zombie warnings during `ss_loading`, while preserving the warning for later runtime misuse.
- `src/rend_gl/texture.c`
  - Added a fallback ladder for postfx framebuffer initialization:
    - requested configuration
    - bloom MRT disabled
    - DOF disabled
    - both disabled
  - Added DOF depth-format retries across safer non-stencil formats before attempting stencil-backed formats.
  - Gated stencil-backed DOF attachment usage behind `r_dof_allow_stencil` so unsupported depth/stencil combinations do not poison the whole postfx path.
- `src/client/entities.cpp`
  - Guarded the sound-only `CL_CalcViewValues` path so it returns quietly before `ca_precached` / valid-frame state, but still fails hard if the cgame entity API is missing during an active runtime path.

## Validation
- `meson compile -C builddir-win`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew`
- Local staged OpenGL smoke launch with `logfile_flush 2` wrote `E:\Repositories\WORR\.install\basew\logs\vscode_console_fix_20260328_b.log`.
- Rechecked the previous smoke-log failure signatures:
  - the map-fix warning storm no longer appears in the refreshed local launch log
  - the zombie-client startup print is suppressed during load
  - the `CL_CalcViewValues` startup spam and loopback startup noise seen during intermediate validation are no longer present in the flushed smoke log
  - the OpenGL renderer now has explicit postfx fallback paths for the `GL_FRAMEBUFFER_UNSUPPORTED` failure case captured in `vscode_launch_smoke.log`

## Notes
- This change is intentionally biased toward clean launch diagnostics: startup-time false positives were removed, but genuine missing cgame entity exports still remain fatal once the client reaches an active game state.
