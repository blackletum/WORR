# Vulkan six-face sky seam parity

Date: 2026-07-14  
Project task: `FR-01-T05`  
Status: implemented, staged, and renderer/validation-layer verified

## Objective

Close the native Vulkan raster skybox artifact gap against OpenGL across all
four lateral faces, the up face, the down face, and their transitions. The
implementation remains entirely in `rend_vk`; no Vulkan path redirects to
OpenGL.

## Reference contract and defects

OpenGL's legacy sky path builds the same six `rt/lf/bk/ft/up/dn` faces around
the current view, clamps each face away from its source texture border, applies
the configured fixed or time-based axis rotation, and leaves ordinary world
geometry in front of the sky.

The Vulkan implementation already had the correct face-axis table, but four
details prevented a durable parity result:

- a fixed `1/512` inset assumed every face was 256 pixels wide and high;
- the 36-vertex sky buffer was destroyed, allocated, mapped, and unmapped every
  frame;
- using the ordinary depth-writing world pipeline for the full cube could
  reject foreground world fragments at cube/clip precision boundaries;
- sky reload normalized and stored the requested rotation axis before calling
  `VK_World_UnsetSky()`, which reset the stored axis to Z. Fixed rotations on
  any other axis therefore rendered the wrong face.

The first validation-layer capture sweep also exposed
`VUID-vkFreeDescriptorSets-pDescriptorSets-00309` on shutdown. Image
registration could release a descriptor set while the final submitted frame
still referenced it.

## Native Vulkan implementation

### Exact sampling and persistent geometry

`VK_World_SetSky()` now queries the decoded dimensions of every registered
face and records the exact half-texel inset, `0.5 / width` and `0.5 / height`.
The legacy `1/512` value remains only as a defensive fallback when dimensions
are unavailable. This preserves bilinear filtering without sampling beyond a
face edge.

The complete cube remains a fixed 36-vertex triangle list. Its host-visible,
coherent vertex buffer is allocated and persistently mapped once, then updated
in place as the camera or sky rotation changes. Per-face first/count metadata
keeps descriptor selection and draws explicit without per-frame allocation.

### Background pipeline and face selection

Sky rendering now uses a dedicated opaque background pipeline with depth test
and depth writes disabled. Ordinary world geometry is emitted afterward and
therefore deterministically covers the background without depending on the
finite cube depth.

The cube triangles wind outward. The sky pipeline culls those front-facing
outer surfaces, retaining only the inward surfaces visible to the camera. This
prevents opposite cube faces from overwriting one another in the no-depth pass
and removes their redundant fragment work. The six source textures retain the
same face-axis and UV convention as OpenGL.

The renderer deliberately keeps the compact full cube instead of performing
OpenGL's CPU sky-portal polygon recursion every frame. The later world pass
provides the portal mask, while persistent geometry and face culling make the
Vulkan path both simpler and cheaper.

### Rotation and descriptor lifetime

The requested sky axis is now normalized into local state, the previous sky is
unregistered, and only then is the normalized axis copied into live renderer
state. Fixed and automatic rotations can no longer be replaced by the reset
axis during re-registration.

`R_UnregisterImage()` now waits for the tracked submitted-frame fence before
destroying the image descriptor. This is scoped to actual image releases and
uses the renderer's existing frame fence rather than a device-wide idle. A
successful wait now clears `frame_submitted`, avoiding a second wait for the
same already-signaled fence between `R_BeginFrame()` and `VK_DrawFrame()`. The
final six-scene validation sweep contains no descriptor lifetime VUID or other
Vulkan validation error.

## Owned parity matrix

The repository-owned inputs are:

- `assets/renderer_parity/fr01_sky_seams_common.cfg`;
- `assets/renderer_parity/fr01_sky_seams_side_{0,90,180,270}.cfg`;
- `assets/renderer_parity/fr01_sky_seams_top.cfg`;
- `assets/renderer_parity/fr01_sky_seams_bottom.cfg`;
- the unrotated and fixed-X-180 `base1.ent` overrides under
  `assets/renderer_parity/fr01_sky_seams/`;
- `assets/renderer_parity/fr01_sky_seams_manifest.json`.

The four 45-degree lateral views exercise the vertical/up transitions. The
top view covers the nominal up face. The last scene disables automatic
rotation and applies a fixed 180-degree X rotation, exposing the nominal down
face through the same portal and proving non-Z axis persistence. All scenes use
fixed presentation controls, synchronous TGA capture, and disabled 2D overlays.

`run_capture_matrix.py` now accepts repeatable `--renderer` and `--scene`
filters so a failed backend/view can be recaptured without relaunching the
entire matrix. The comparator additionally supports inclusive
`min_color`/`max_color` probes alongside the existing target-color/tolerance
form. Unit coverage verifies both forms.

Each manifest scene uses a sky-only crop with these acceptance gates:

- mean absolute error no greater than `2` in any RGB channel;
- no more than `0.5%` of pixels above an 8-point maximum-channel error;
- expected warm-sky or down-face color coverage in both backends;
- no more than `1%` backend coverage-count delta;
- at least `0.99` coverage-mask intersection-over-union.

The range probe prevents matching empty/black output from passing, while the
strict crop metrics reject holes, incorrect faces, displaced transitions, and
visible seam discontinuities.

## Final staged evidence

The clean installed-tree run launched all six scenes in both backends at
960x720. It completed all twelve processes, produced all twelve captures, and
passed every gate:

| Scene | Mean absolute RGB | Pixels over 8 | Coverage IoU |
|---|---:|---:|---:|
| side 0 | `0.108909 / 0.159416 / 0.029792` | `0.080519%` | `0.999753` |
| side 90 | `0 / 0.016778 / 0` | `0%` | `1.0` |
| side 180 | `0.002857 / 0.261905 / 0.004286` | `0%` | `1.0` |
| side 270 | `0.000167 / 0.099200 / 0.006100` | `0%` | `1.0` |
| up | `0.005905 / 0.105778 / 0.006524` | `0%` | `1.0` |
| down, fixed X 180 | `0.015984 / 0.007413 / 0.001444` | `0%` | `1.0` |

The stable JSON report is
`.tmp/renderer-parity/fr01-sky-seams-final/results.json`. A scan of every final
process log found no `VUID`, validation error, access violation, stack
overflow, or fatal error.

## Validation

The following checks pass:

```text
python -m py_compile tools/renderer_parity/compare_captures.py tools/renderer_parity/run_capture_matrix.py
python tools/renderer_parity/test_compare_captures.py
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll worr_opengl_x86_64.dll
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_sky_seams_manifest.json --run-root .tmp/renderer-parity/fr01-sky-seams-final --timeout 45 --vulkan-validation --json-output .tmp/renderer-parity/fr01-sky-seams-final/results.json
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64
```

The built and staged `worr_vulkan_x86_64.dll` files are byte-identical at
SHA-256
`8C0688C4595ADFC678B74EC3656E69EFD34739A8A21E186BA7AF04C5F57193FB`.
The final staged `basew/pak0.pkz` hash is
`2BEC9DCF312FA7D7AD7F4A068F25D4272EF61E941676EE154C5F7D0C16BAE7F4`.

No cvar or end-user workflow changed, so this engineering slice does not need
a `docs-user/` update.
