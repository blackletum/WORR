# RmlUi Native Vulkan and RTX Renderer Parity (2026-07-14)

Task: `FR-09-T03`

## Outcome

WORR's complete 58-route RmlUi menu set now renders through native OpenGL,
Vulkan, and Vulkan RTX/vkpt paths. Vulkan and RTX do not redirect RmlUi work
to OpenGL. Final installed-tree sweeps opened every registered route in all
three renderer lanes, and a visual contact-sheet audit found no route layout,
clipping, backdrop, color, or modal-presentation regressions.

The Player Setup preview is also native in both Vulkan renderers. The model,
skin, attached weapon, and animation render inside the authored preview panel
without overwriting the surrounding RmlUi shell.

## Native Vulkan preview composition

The raster Vulkan renderer now preserves no-world refdef rectangles in
`src/rend_vk/vk_entity.c` and exposes their state through
`src/rend_vk/vk_entity.h`. Entity recording applies a panel-local viewport and
scissor when the frame is a no-world preview.

The initial implementation attempted to append the preview after RmlUi inside
the main pass. That invalidated the visible menu on this renderer. The accepted
path in `src/rend_vk/vk_main.c` and `src/rend_vk/vk_local.h` uses a compatible
second render pass with color `LOAD`, an independent depth clear, and the
preview entity draw after the menu. This preserves both the menu color buffer
and correct preview depth behavior.

## Native RTX/vkpt preview composition

The path-traced renderer now carries `RF_FULLBRIGHT` into a dedicated
`MATERIAL_FLAG_FULLBRIGHT` material flag and gives preview albedo a modest
emissive contribution. This keeps menu models readable without applying a
separate raster renderer or hard-coded replacement material.

For no-world preview frames, `src/rend_rtx/vkpt/main.c` disables bloom and FSR
post-processing, fixes the top-down viewport translation, and submits RmlUi
before a scissored final blit. `src/rend_rtx/vkpt/draw.c` provides the bounded
final-blit helper, while `src/rend_rtx/vkpt/debug.c` initializes debug-draw
state before preview use. The final ray-traced image therefore replaces only
the preview rectangle and leaves all surrounding RmlUi pixels intact.

Shader and material changes are contained in:

- `src/rend_rtx/vkpt/vkpt.h`
- `src/rend_rtx/vkpt/shader/constants.h`
- `src/rend_rtx/vkpt/shader/path_tracer_rgen.h`

## RTX menu texture correctness

Two renderer-wide RTX defects were found by the full contact-sheet audit.

First, RmlUi background images are registered as sprites with repeat intent,
but the RTX texture path selected the sprite clamp sampler before considering
`IF_REPEAT`. `src/rend_rtx/vkpt/textures.c` now gives repeat intent precedence,
so the authored backdrop tiles instead of stretching its edge across the
screen.

Second, authored PNG, JPEG, and PCX menu colors were sampled as linear data and
encoded again by the sRGB swapchain. `src/renderer/rmlui_bridge.cpp` now adds
`IF_SRGB` only for `RENDERER_VULKAN_RTX`. This corrects RTX menu color without
changing the established OpenGL or raster-Vulkan color paths.

The same shared bridge had an OpenGL-only include-order build failure after it
was recompiled. The OpenGL header now declares common engine symbols before
`renderer_api.h` installs renderer-import macros. This is a compile fix only;
it does not alter OpenGL runtime behavior.

## Evidence

The following scratch manifests contain the final installed-tree route sweeps:

- OpenGL: `.tmp/rmlui/runtime-capture/full-opengl-final-20260714/manifest.json`
- Vulkan: `.tmp/rmlui/runtime-capture/full-vulkan-final-20260714/manifest.json`
- RTX/vkpt: `.tmp/rmlui/runtime-capture/full-rtx-final-20260714/manifest.json`

Each manifest records 58 passing routes. Corresponding
`contact-sheets-final/` directories contain four labeled sheets per renderer.
The final RTX sheets were regenerated after both repeat-sampler and sRGB fixes.

Player-preview captures are retained in the Vulkan and RTX final capture
directories as the `players` TGA/PNG pair. They show the complete model and
weapon inside the transparent `#players-preview-surface` panel.

## Build and staging validation

The following renderer targets built successfully:

```text
ninja -C builddir-win worr_opengl_x86_64.dll
ninja -C builddir-win worr_vulkan_x86_64.dll
ninja -C builddir-win vkpt_shaders.stamp
ninja -C builddir-win worr_rtx_x86_64.dll
```

The final build was staged and validated with:

```text
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

This refreshed current binaries, `basew/pak0.pkz`, and the loose RmlUi asset
tree under `.install/`.

The full engine DLL target remains independently blocked by unresolved symbols
from unrelated in-progress networking work in the shared dirty worktree. The
renderer DLLs affected by this task compile, link, stage, and run successfully;
no networking source was changed as part of this renderer parity closeout.

## Guardrails

`tools/ui_smoke/check_rmlui_runtime_ux_services.py` now verifies the native
Vulkan preview overlay, native RTX preview overlay, RTX repeat sampler, RTX
sRGB source-texture handling, and initialized RTX debug state. These checks
prevent a future implementation from silently restoring an OpenGL fallback or
regressing the renderer-specific composition rules.
