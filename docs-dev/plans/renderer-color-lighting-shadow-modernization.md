# Renderer Color, Lighting, and Shadow Modernization Plan

Date: 2026-07-10

Status: Phase 0 complete; Phases 1-4 planned

Roadmap tasks: `FR-01-T09`, `FR-02-T12`, `FR-02-T13`, `FR-02-T14`,
`FR-02-T15`, `FR-03-T11`, `DV-02-T07`, and `DV-03-T08`.

Implementation baseline:
`docs-dev/renderer/gamma-lighting-shadow-audit-hardening-2026-07-10.md`.

## Objective

Give OpenGL, native Vulkan raster, and Vulkan RTX explicit and testable color,
lighting, and shadow contracts. The end state should be physically coherent
where the content permits it, preserve deliberate Quake II compatibility
controls, avoid backend-dependent gameplay behavior, and scale without fixed
worst-case allocations.

Native Vulkan work remains native Vulkan. This plan does not route Vulkan
rendering through OpenGL, and it does not modify `q2proto/`.

## Current Contract Problems

### Color and gamma

- OpenGL `r_gamma` is either an operating-system gamma ramp or a destructive
  source-texture transform. The software path affects selected wall/skin
  images rather than the fully composited frame, so UI, fog, lightmaps, and
  alpha blending do not share one output transform.
- Native Vulkan raster had no effective `r_gamma` path. Its albedo, lightmap,
  lighting, blending, and swapchain writes are display-code-value operations,
  not a declared linear pipeline.
- OpenGL's optional HDR path estimates luminance and bloom before its
  approximate legacy decode, so exposure does not measure the same signal that
  tone mapping consumes.
- Texture and render-target transfer functions are implicit. `IF_SRGB` is not
  a complete cross-renderer image classification contract.
- An SRGB Vulkan swapchain format would encode already display-referred raster
  output a second time. Phase 0 now prefers supported UNORM B8/R8 formats, but
  a real presentation transform is still required.
- Hardware gamma capability is coarse and platform-dependent. SDL3 explicitly
  cannot provide it; Win32 uses a single generated curve and does not preserve
  a modern per-display color-management contract.

### Lighting

- Much of legacy raster lighting is evaluated in gamma-encoded texture space.
  This produces incorrect addition, attenuation, interpolation, and blending.
- Visual cvars (`r_gamma`, intensity/modulate, fullbright, dynamic lights) can
  influence `R_LightPoint`, which is also used to produce the gameplay
  `lightlevel` byte. Phase 0 prevents non-finite conversion and wraparound, but
  the query is still renderer-dependent.
- Native Vulkan previously omitted `r_intensity`; Phase 0 adds shader-side
  parity with the OpenGL surface/model eligibility rules.
- Non-uniform/reflected entity transforms previously used the model matrix for
  OpenGL normals; Phase 0 now uses the orthogonal-axis inverse transpose.
- Native Vulkan MD5 receivers still use per-triangle normals rather than smooth
  skinned normals (`FR-01-T04`).

### Shadowmapping

- Fixed 64-layer arrays are allocated at the largest requested page size. A
  1024 EVSM configuration can approach a gigabyte once depth, RGBA16F moments,
  and the full mip chain are included.
- Mid-frame array growth destroys pages already rendered during the frame.
  Existing extra-frame rerendering recovers later, but allocation should be
  preflighted and transactional.
- Lower per-light resolutions do not reduce raster cost after the global array
  has grown. Resolution buckets or an atlas are required.
- OpenGL and native Vulkan use different comparison/filter primitives. Native
  Vulkan "hard" currently inherits linear comparison filtering, while OpenGL
  performs explicit nearest raw-depth comparisons.
- Alpha-tested world/model surfaces cast solid silhouettes because caster
  passes do not carry material textures, UVs, and cutoffs.
- Moment mip generation touches the complete array when only a subset of pages
  changed. Moment storage also reserves four channels for two moments.
- PCSS blocker search uses projected depth for perspective pages rather than
  linearized light distance.
- Point-light page selection and edge clamping can still expose cube-face
  seams. A cube-array/seam-remap strategy is preferable to increasing filter
  cost indefinitely.
- Sun visibility currently multiplies the complete baked/static term. This is
  why `r_shadow_sun` remains opt-in; a direct sun term must be separated before
  default-on use.

### Vulkan RTX tone mapping and shadows

- Phase 0 fixed histogram/curve out-of-bounds access, partial-workgroup shared
  initialization, divergent barriers, reset synchronization, degenerate
  vertical sun bases, and avoidable NaN paths.
- Tone-mapping cvars still need a centralized finite/range policy, and the
  signed fixed-point histogram accumulator can overflow at very high internal
  resolutions.
- Path-traced dynamic/polygonal light sampling needs initialized contribution
  state and coincident-ray distance guards.
- The RTX UNORM/SRGB mutable-view fallback must either negotiate mutable
  swapchains correctly or explicitly encode to an ordinary UNORM view.

## Target Architecture

### Scene and image domains

Every image/resource must declare one of these roles:

1. Color/albedo/UI source: stored as sRGB where appropriate and decoded once
   by the sampling hardware (or an exact fallback function).
2. Linear data: lightmaps, normals, masks, shadow depths/moments, lookup data,
   and other non-color resources; never sRGB decoded.
3. Linear scene color: FP16 or a documented fallback, containing lighting,
   transparency, fog, emissive contribution, bloom inputs, and view blends.
4. Display output: written only by a final presentation pass using the chosen
   SDR or HDR transfer function.

The resource API should expose the role rather than letting backends infer it
from filenames or broad image types.

### Final presentation pass

Both raster backends should render 3D, UI, and overlays into a defined scene
target and perform one final full-screen presentation transform:

1. resolve scene exposure;
2. apply bloom and color grading in their documented linear/display domains;
3. tone map HDR scene values;
4. apply compatibility `r_gamma` as an output control;
5. apply exact sRGB OETF for SDR UNORM output, or the negotiated HDR transfer;
6. dither to the destination precision;
7. write the swapchain/backbuffer once.

Gamma must not be applied independently to each translucent draw because that
changes blend math. Quake3e's final-FBO gamma/composite path is useful
structural inspiration, but WORR should use explicit modern transfer functions
and renderer-neutral cvars.

### Gameplay light query

`FR-01-T09` will add a renderer-neutral query that returns a finite linear or
clearly documented legacy luminance from BSP lightmaps/lightgrid only. It will
not include visual fullbright, exposure/gamma, entity intensity/modulate, or
transient dlights unless a gameplay API explicitly requests them. The network
byte conversion remains saturating and deterministic.

### Shadow resource model

`FR-02-T14` should introduce:

- an explicit page budget rather than `max_lights * 6` reservation;
- separate accounting for cone, point, and sun views;
- preflighted maximum requirements before recording any page;
- resolution buckets or an atlas so small pages remain small;
- transactional replacement with the previous valid resource retained on
  allocation failure;
- active-layer allocation and dirty-page mip generation;
- RG16F moments when render/linear-filter support is available;
- capability-tested depth comparison and linear filtering, with explicit
  nearest/manual fallbacks;
- distinct true-hard and filtered samplers;
- alpha-tested caster batches with base texture, UV, and material cutoff;
- persistent static BSP shadow geometry and per-frame reused skinned geometry.

## Phases and Task Gates

### Phase 0: bounded audit hardening — complete (`FR-02-T12`)

Land correctness and safety fixes that do not commit the project to a new
output contract. This includes EVSM clear encoding, cache invalidation,
finite/range checks, safe sampling, intensity/filter parity, strict remote
shadow-light parsing, stable identities, and VKPT synchronization/index fixes.

Gate: current implementation log validation passes on GL, Vulkan raster, and
RTX builds, with staged runtime smoke for PCF and EVSM.

### Phase 1: output/color contract (`FR-02-T13`, P0)

- Inventory every image class and render target.
- Add explicit sRGB/linear formats and sampling policy.
- Add linear FP16 scene targets and a final presentation pass in GL and native
  Vulkan.
- Define SDR swapchain selection and encode behavior; negotiate HDR only when
  the platform exposes all required capabilities.
- Preserve `r_gamma` as an output compatibility control and deprecate source
  texture mutation/hardware gamma as the default.
- Align exposure, bloom, fog, color grading, UI, screenshots, and video capture
  with the new domains.

Gate: an 18% gray patch, grayscale ramp, saturated color chart, additive-light
chart, and 50% alpha blend produce expected linearized values and match GL/VK
within agreed tolerances.

### Phase 2: renderer-neutral light query (`FR-01-T09`, P0)

- Move static light sampling semantics behind a shared/query contract.
- Remove visual cvar and backend feature dependence.
- Add finite, boundary, 1xN/Nx1, missing-style, and extreme-value tests.
- Verify the serialized byte for inputs below zero, NaN/Inf, nominal values,
  and values above the protocol range.

Gate: identical maps/positions produce identical gameplay lightlevel bytes in
OpenGL, native Vulkan, and headless/reference test paths.

### Phase 3: scalable/material-aware shadows (`FR-02-T14`, P1)

- Implement the resource/page model above.
- Unify hard/PCF/PCSS/VSM/EVSM semantics and quality tiers.
- Add alpha-tested casters and smooth native Vulkan skinned normals.
- Linearize PCSS depth and solve cube-face filtering seams.

Gate: budget stress cannot exceed configured memory; allocation failure keeps
the previous valid shadows; every filter has deterministic GL/VK captures;
alpha fences and foliage cast matching silhouettes.

### Phase 4: direct sun separation (`FR-02-T15`, P1)

- Define ambient/baked-indirect and direct-sun terms.
- Apply cascade visibility only to the direct term.
- Retune cascade split/blend/bias behavior under the linear scene pipeline.
- Enable sun shadows by default only after indoor/outdoor reference captures
  prove that interiors retain their baked illumination.

## Validation and Tooling

`DV-02-T07`:

- make GLSL-to-SPIR-V generation a real build dependency;
- fail when embedded headers are stale;
- run `spirv-val` for native Vulkan and VKPT in CI;
- retain source paths that are reproducible and never refer to scratch trees.

`DV-03-T08`:

- unit/fuzz tests for cvar alias mirroring, finite clamps, cache-key changes,
  shadow configstrings, and lightlevel conversion;
- scene-specific smoke assertions rather than process-exit-only checks;
- readback/capture comparisons for gamma, lighting, shadow filters, alpha
  casters, sun cascades, and allocation fallback;
- GPU-assisted validation for tone-map reset/barrier and edge-workgroup cases;
- small, 4K, 8K, supersampled, empty-light, one-light, and maximum-light cases.

## User Interface

`FR-03-T11` will hide unsupported controls, replace renderer-specific knobs
with shared `r_` controls where semantics are shared, expose practical shadow
quality controls, and display whether SDR/HDR/hardware-gamma capabilities are
actually active. It depends on the finalized output contract; the menu must not
promise controls that a backend silently ignores.

## Definition of Done

- No gamma or transfer function is implicit at an API/resource boundary.
- Lighting and blending occur in a documented linear domain.
- A single final pass owns display encoding and compatibility gamma.
- Gameplay lightlevel is backend/settings independent and saturating.
- GL and native Vulkan expose equivalent documented lighting/shadow controls.
- Shadow allocations are budgeted, transactional, and proportional to active
  resolution/capacity.
- Sun visibility affects direct sun, not the complete baked term.
- Native Vulkan and RTX shader binaries cannot drift from their sources.
- Automated semantic/pixel tests cover the contracts above.
