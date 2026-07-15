# Vulkan alias outline parity and capture automation

Date: 2026-07-14  
Project task: `FR-01-T04`  
Status: implemented and renderer-validated; staged process lifecycle issue recorded separately

## Objective

Close the remaining OpenGL/Vulkan alias-model presentation gap for
`RF_OUTLINE` and `RF_OUTLINE_NODEPTH`, and replace the one-off parity capture
workflow with repository-owned scenes, thresholds, and comparison tooling.

The implementation is native Vulkan. It does not route Vulkan rendering
through the OpenGL backend.

## OpenGL contract

The reference behavior is `draw_alias_mesh()` in `src/rend_gl/mesh.c`:

- `RF_OUTLINE` writes stencil reference `1` over the unexpanded alias mesh,
  draws a uniformly expanded solid-color shell only where stencil is not `1`,
  then clears the mesh stencil footprint.
- `RF_OUTLINE_NODEPTH` disables depth testing for both the mask and shell,
  which is the teammate-through-walls presentation path.
- The outline uses `entity_t::rgba`, with red as the zero-color fallback.
  Alpha below `0.999` selects standard alpha blending.
- `cl_player_outline_width` is clamped to `0.5..6.0` pixels. Projected model
  radius converts that requested width to a minimum scale of `1.02` and a
  maximum scale of `3.0`.
- MD5 replacement models use the source MD2 frame radii for the same
  distance-aware scale calculation.

## Native Vulkan implementation

### Depth/stencil capability

`VK_ChooseDepthFormat()` now prefers `D24_UNORM_S8_UINT`, then
`D32_SFLOAT_S8_UINT`, while retaining `D32_SFLOAT` as a compatibility fallback.
Stencil-capable render passes clear stencil at frame start and expose the
actual capability through `R_GetGLConfig()`.

If a device only supports the depth-only fallback, Vulkan continues to render
normally, reports zero stencil bits, skips outline emission, and writes one
warning. There is no OpenGL fallback.

### Presentation semaphore lifetime

The first validation-layer map run exposed
`VUID-vkQueueSubmit-pSignalSemaphores-00067`: the single render-finished
semaphore was being signalled again after submission completed but before the
presentation engine had necessarily released its prior wait.

Render-finished semaphores are now allocated per swapchain image and selected
by the acquired image index. Reacquiring an image establishes the safe reuse
point for its presentation semaphore, while the existing single-frame fence
continues to serialize command-buffer and image-available semaphore reuse.
The repeated validation-layer captures contain no validation error after this
change.

### Outline draw sequence

Each outlined MD2 model or MD5 mesh appends three explicit batch stages:

1. mask the original transformed triangle range into stencil;
2. draw an expanded solid-color copy where stencil is not the mask reference;
3. clear stencil through the original range.

Depth-tested and no-depth mask/shell variants are prebuilt when swapchain
resources are created. Opaque and alpha shell variants preserve the authored
outline alpha. The cleanup stage has color and depth writes disabled and zeros
the mesh footprint before the next outlined mesh is processed.

Depth-hack entities retain their reduced viewport depth range and weapon-model
projection push constants throughout all outline stages.

### Scale and model data

MD2 loading now retains one raw-coordinate radius per animation frame, matching
the OpenGL alias frame bounds contract. The scale calculation interpolates the
current/previous frame radii, applies the largest absolute entity scale, and
uses view distance, vertical FOV, and viewport height to maintain the requested
pixel thickness.

MD5 replacement meshes deliberately use their source MD2 radii, as OpenGL does.
The already skinned/transformed mesh stream supplies the stencil mask and
cleanup. Only the expanded shell is copied, so an outline does not repeat MD5
skeleton interpolation, weight skinning, or smooth-normal construction.

Models without `RF_OUTLINE` allocate no extra vertices or batches. Outline
pipelines are created outside gameplay and are never rebuilt per entity or
frame.

## Durable comparison workflow

The owned scene inputs are:

- `assets/renderer_parity/fr01_alias_outline/fact2.ent`
- `assets/renderer_parity/fr01_alias_outline_md2.cfg`
- `assets/renderer_parity/fr01_alias_outline_md5.cfg`
- `assets/renderer_parity/fr01_alias_outline_manifest.json`

The configs use the same fixed `fact2` spawn/camera, 960x720 window, green
full-alpha self/team outline, four-pixel width, and third-person view. One scene
forces MD2 fallback and the other forces the MD5 replacement.

`tools/renderer_parity/run_capture_matrix.py` launches the staged executable
once per backend and scene, directs screenshots into backend/scene-specific
directories under `.tmp/`, copies the named TGA files to a canonical capture
tree, retains per-process logs, and then runs the comparator. It never cleans
paths outside its selected run root. Windows rerelease mode overrides the
requested `homedir`, so the scene configs explicitly set every presentation
control used by the comparison instead of depending on a user config.

`tools/package_assets.py` mirrors `assets/renderer_parity` loose beside the
canonical `pak0.pkz`. This is required for the current no-zlib Windows runtime,
which cannot mount `.pkz` files, and keeps the exact same authored scene usable
by archive-capable builds.

`tools/renderer_parity/compare_captures.py` is standard-library-only and:

- decodes uncompressed 24-bit or 32-bit TGA files;
- normalizes all four TGA origin combinations;
- compares optional crops using per-channel mean absolute error, per-channel
  root mean square error, maximum error, and the percentage of pixels above a
  selected maximum-channel threshold;
- runs configurable color-presence probes with per-backend population delta
  and pixel-mask intersection-over-union for feature-specific evidence;
- emits a stable JSON report and returns a nonzero status on threshold failure.

The manifest requires at least 500 near-green outline pixels per backend, no
more than a 15% backend population delta, and at least `0.8` mask
intersection-over-union. This rejects missing, shifted, or grossly mis-sized
outlines. Full-frame RGB metrics remain in the JSON as diagnostics but are not
the acceptance gate for this feature scene: OpenGL readback omits the visible
hardware-gamma/overbright presentation adjustment, while native Vulkan writes
display-referred values directly. The renderer-neutral presentation contract
that will make those captures directly comparable is already tracked by
`FR-02-T13`.

Run the focused gate from the repository root after refreshing `.install/`:

```text
python tools/renderer_parity/run_capture_matrix.py --vulkan-validation --json-output .tmp/renderer-parity/fr01-alias-outline/report.json
```

Existing captures can be re-evaluated without launching the client:

```text
python tools/renderer_parity/run_capture_matrix.py --compare-only
```

## Validation completed before the staged gate

The following checks pass:

```text
python tools/gen_vk_world_spv.py --validate
python -m py_compile tools/renderer_parity/compare_captures.py tools/renderer_parity/run_capture_matrix.py tools/renderer_parity/test_compare_captures.py
python tools/renderer_parity/test_compare_captures.py
ninja -C builddir-win worr_vulkan_x86_64.dll worr_opengl_x86_64.dll
python tools/test_package_assets.py
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64
```

The comparator tests cover a passing bounded delta, simultaneous metric/probe
failures, and bottom-origin normalization.

An isolated Vulkan `fact2` run on Intel Iris Xe produced both MD2 and MD5 green
silhouette borders. A same-camera pre-change capture contained zero strict
green pixels; the MD5 outline contained 2,570 and the MD2 outline 2,564, with
similar player-model bounding boxes. Visual inspection confirmed that stencil
rejected the model interior. A validation-layer initialization run created all
new pipelines without reporting a Vulkan error.

## Staged comparison evidence

All four staged scenes reached synchronous TGA completion at 960x720. The
feature-only comparison report passes:

| Scene | OpenGL pixels | Vulkan pixels | Count delta | Mask IoU |
|---|---:|---:|---:|---:|
| MD2 outline | 2,370 | 2,564 | 7.5663% | 0.924337 |
| MD5 outline | 2,306 | 2,570 | 10.2724% | 0.831018 |

The full-frame diagnostic recorded mean absolute RGB errors of approximately
`17.36/23.06/14.83` for MD2 and `17.46/23.09/14.89` for MD5. Visual inspection
showed identical camera/animation state and the expected presentation-wide
brightness offset described above, rather than an outline displacement.

The validation-layer rerun on Intel Iris Xe produced no `VUID` or validation
error after the per-image semaphore fix. The final built and staged Vulkan DLLs
are byte-identical at SHA-256
`D98B7DBF5E1EAB747EA73FB444DB30508B6106A2E383F4A4E602CD4667A3D6B7`.

## Process-lifecycle limitation

The current unrelated sgame worktree either stack-overflows with Windows status
`0xC00000FD` or hangs after the synchronous screenshot when the scene executes
`quit`. The runner preserves every completed capture, reports nonzero/hung
processes as failures, and never turns that lifecycle failure into a green full
gate. `--compare-only` passes the renderer feature thresholds over the retained
captures. This limitation does not change the `FR-01-T04` outline result and
should be rechecked when the sgame lifecycle work stabilizes.

No cvar or end-user workflow changed, so this slice does not require a
`docs-user/` update.
