# Vulkan global-fullbright static-world fast path

Date: 2026-07-17

Task ID: FR-01-T15

## Outcome

Global `r_fullbright 1` now sends eligible opaque, lightmapped static-world
batches through Vulkan's existing native `VK_WORLD_TEXTURE_REPLACE` material
pipelines. The renderer keeps immutable world geometry and chooses the
equivalent material program at record time; it never redirects a Vulkan draw
through OpenGL.

This removes the generic Vulkan fragment path's otherwise-inert lightmap,
glow-map, normal, shadow, and dynamic-light work from the global-fullbright
case. The normal fog-aware and no-fog texture-replace modules remain the only
programs involved, so this does not add shader binaries or a new pipeline
lifetime class.

The archived, default-on `vk_world_fast_lit` control remains the native
specialization gate. `0` retains the complete Vulkan world fragment path as a
driver-workaround and regression control; it does not change render backends.

## Recovered OpenGL contract

OpenGL rebuilds ordinary non-transparent world faces with
`GLS_TEXTURE_REPLACE` whenever it has global fullbright enabled. For the
eligible face family, both backends therefore evaluate:

1. the base texture;
2. optional shared intensity; and
3. applicable surface fog.

The Vulkan gate permits only `LIGHTMAPPED`, `INTENSITY`, and `GLOWMAP` vertex
flags while global fullbright is active. The lightmap and glow-map inputs are
inert because global fullbright fixes lighting to one; world vertex colour is
white for this opaque family and lightmap alpha is authored as one. The gate
still excludes alpha-tested, translucent, warped, and sky batches, all of
which retain their complete native material behavior.

The established `world_texture_replace_draws` and
`world_texture_replace_no_fog_draws` counters therefore provide direct runtime
selection evidence. The existing `world_fast_lit_fullbright` counter remains a
diagnostic that the lightmapped receiver specialization itself was not chosen;
it is expected to be one in the optimized global-fullbright scene.

## Owned visual coverage

`assets/renderer_parity/fr01_bmodel_first_frame_fullbright_world.cfg` and the
`global_fullbright_lightmapped_world` scene in
`fr01_bmodel_first_frame_manifest.json` explicitly cover the lightmapped
static-world region under `r_fullbright 1`. The crop is 100 by 340 pixels and
contains only the intended legacy-lightmap surface.

The staged current binary passed headlessly with Vulkan validation enabled:

| Check | Result |
| --- | --- |
| OpenGL/Vulkan RGB comparison | Exact over all 34,000 crop pixels; maximum and mean RGB error are zero. |
| Material probe | Both renderers report exactly 34,000 pixels in the required `[24, 40, 72]` colour range, with IoU 1.0. |
| Vulkan validation | No validation error or VUID was reported. |

## Controlled timing

The paired, release-style control uses the same 960x720 hidden fixed view,
fixture, adapter, `r_multisamples 0`, fullbright configuration, two renderers,
and 100 post-warmup samples. Only `vk_world_fast_lit` changes between the two
captures. The test system is Intel(R) Iris(R) Xe Graphics under the recorded
`local-headless` driver identity.

| Vulkan metric | Generic fallback (`0`) | Native path (`1`) | Change |
| --- | ---: | ---: | ---: |
| Opaque-world GPU mean | 0.86881 ms | 0.37880 ms | -56.4% |
| Opaque-world GPU p50 | 0.978 ms | 0.377 ms | -61.5% |
| Scene GPU mean | 0.99377 ms | 0.40925 ms | -58.8% |
| Completed GPU-frame mean | 1.05295 ms | 0.44018 ms | -58.2% |
| Completed GPU-frame p50 | 1.038 ms | 0.438 ms | -57.8% |

Both rows retain three Vulkan draws and 192 uploaded bytes per frame. The
fallback has zero world texture-replace draws; the enabled row has one
texture-replace and one no-fog texture-replace draw per frame. CPU mean moves
from 0.28534 to 0.30729 ms, so no CPU-performance claim is made.

This is a narrowly scoped native GPU improvement, not evidence that Vulkan has
closed the renderer-wide GPU-frame gap to OpenGL. FR-01-T15 remains open for
representative-map and product-budget proof.

## Reproduction

1. Run source and packaging checks, build, and stage:

   ```text
   python -m unittest tools.renderer_parity.test_vulkan_world_fast_lit_source
   python tools/test_package_assets.py
   python tools/gen_vk_world_spv.py --validate
   ninja -C builddir-win worr_vulkan_x86_64.dll
   python tools/stage_install.py --build-dir builddir-win --install-dir .install
   python tools/package_assets.py --assets-dir assets --install-dir .install
   ```

2. Run the validation-backed visual scene:

   ```text
   python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json --scene global_fullbright_lightmapped_world --vulkan-validation --timeout 180
   ```

3. Capture `renderer_parity/fr01_renderer_perf_bmodel.cfg` twice with
   `r_multisamples=0` and `vk_world_fast_lit=0` then `=1`; analyze each pair
   with 20 warm-up samples and a 100-sample minimum.
