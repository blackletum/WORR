# Vulkan Colour-Correction Visual Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: the fixed combined brightness, contrast, saturation, and tint case
has retained OpenGL-versus-native-Vulkan visual evidence.

## Runtime lifecycle correction

The normal `R_ModeChanged` Vulkan lifecycle previously rebuilt world, entity,
debug, and UI swapchain resources but omitted
`VK_PostProcess_CreateSwapchainResources`. This left the colour controls
registered and evaluated, but left the final compositor without its pipeline,
per-frame scene-copy images, or descriptors. Consequently a non-identity
`vk_color_*` configuration presented the unmodified scene.

`R_ModeChanged` now destroys the old post-process swapchain resources before
the old swapchain and rebuilds them after UI resources. The regular
`VK_RecreateSwapchain` path follows the same destroy/rebuild order. The fix is
entirely in `rend_vk`; it does not redirect or sample an OpenGL path.

`test_vulkan_color_correction_source.py` guards both lifecycle paths and their
ordering, alongside the existing shader/order and identity-fast-path checks.

## Retained paired scene

`renderer_parity/fr01_color_correction.cfg` reuses the stable first-frame
inline-BSP view and disables bloom, DOF, CRT, LUT grading, and split toning.
It enables the matched non-identity transform on both backends:

- brightness `0.10`;
- contrast `1.20`;
- saturation `0.70`; and
- cyan tint.

`fr01_color_correction_manifest.json` crops the 250 by 200 background region
at `(100, 100)`. The final hidden-native-surface validation run passed with
zero RGB error over all 50,000 pixels and an identical `[0, 48, 74]` output
mask of 50,000 pixels at IoU `1.0`.

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install \
  --manifest assets/renderer_parity/fr01_color_correction_manifest.json \
  --run-root .tmp/renderer-parity/fr01-color-correction-native-final \
  --vulkan-validation
```

The runner sets `win_headless 1`, disables input and sound, uses isolated
runtime homes, and does not launch an interactive client window.

## Remaining scope

This gate proves the native combined basic colour transform and its post-mode
lifecycle. It does not close HDR/auto exposure, bloom input parity, split
toning, LUT grading, the underwater combination, DOF, CRT, or resolution
scaling; those remain tracked under `FR-01-T12`.
