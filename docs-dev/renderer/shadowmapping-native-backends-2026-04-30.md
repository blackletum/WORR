# Shadowmapping Native Backend Pass

Date: 2026-04-30

Tasks: `FR-02-T09`, `FR-02-T10`, `FR-02-T11`, `DV-02-T06`, `DV-07-T05`

## Summary

This pass turns the shadowmapping replacement baseline from a policy-only frontend into real non-RTX renderer work. OpenGL now allocates, renders, and samples native depth and moment 2D-array pages. The native Vulkan renderer now allocates its own depth-array `VkImage`, optional moment color-array `VkImage`, records a shadow pass before the main pass, performs explicit image-layout barriers, binds a shadow descriptor set in the world pipeline, and samples the generated pages in the embedded world fragment shader.

The shared frontend remains the only owner of selection, page residency, dirty reasons, candidate PVS2 policy, caster bounds, and filter policy. Backend code receives concrete `shadow_view_desc_t` records and never reintroduces the removed slot fallback or sticky slot-churn behavior.

Resident pages also remember the caster membership and quantized bounds hash they last rendered. Static-cache reuse is dirtied when any entity caster enters, leaves, appears, disappears, moves, changes bounds, or otherwise changes the view membership. Cluster masks remain a cheap reject only when complete; oversized mask collection falls through to precise sphere/cone overlap tests.

## OpenGL Backend

`src/rend_gl/shadow.c` implements the native OpenGL backend:

- creates a sampled `GL_TEXTURE_2D_ARRAY` depth texture for hard/PCF/PCSS;
- creates `GL_RGBA16F` moment pages and mip chains for VSM/EVSM;
- renders each frontend shadow view into a dedicated layer through `glFramebufferTextureLayer`;
- records page matrices and bias data in a `ShadowPages` UBO;
- maps frontend light/page selections into dynamic-light shader data;
- culls BSP world faces against the active shadow view before submission;
- renders opaque BSP world faces plus frontend-selected brush, alias, and MD5 skeletal caster geometry into depth or moment pages;
- binds depth and moment arrays in the dynamic shader path and applies hard, PCF, PCSS, VSM, or EVSM receiver sampling according to the frontend filter family;
- exposes materialization, counters, focused caster chains, and live debug overlays through `r_shadow_dump`/`gl_shadow_dump` and `r_shadow_draw_debug`;
- forces the dynamic shader path when sun shadows are active so zero-dlight sun-only frames still sample the shadow pages.

The GL backend reports both depth and moment storage support. PCSS remains budgeted by `r_shadow_pcss_max_lights`; lower-priority lights fall back to PCF.

## Native Vulkan Backend

`src/rend_vk/vk_shadow.c` and `src/rend_vk/vk_shadow.h` implement the native Vulkan backend:

- chooses a depth format that supports both depth-stencil attachment and sampled-image use;
- chooses a moment color format that supports color-attachment and sampled-image use;
- creates a fixed 2D-array depth image, whole-array sampled view, per-layer render views, framebuffers, raw depth sampler, and depth render pass;
- creates a moment image/view/sampler and color+depth render path when VSM/EVSM storage is selected;
- generates Vulkan moment mip chains when the selected format supports linear blit mip generation;
- queues per-page shadow jobs from frontend dirty views;
- records the shadow pass before the main swapchain render pass;
- transitions the depth image between `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` and `DEPTH_STENCIL_READ_ONLY_OPTIMAL` with explicit barriers;
- transitions the moment image through color attachment, transfer, and shader-read layouts when mip generation is active;
- uploads page matrices, bias data, and per-dlight page bindings through a backend-owned uniform buffer;
- exposes a shadow descriptor set consumed by the world pipeline;
- updates `vk_world_spv.h` so world receivers sample the native shadow depth array;
- evaluates dynamic lights in the world fragment shader, applying local point/cone shadow pages only inside the owning light contribution while applying sun cascades as a separate global lightmap factor;
- reports selected materialization and focused caster chains through `r_shadow_dump`/`vk_shadow_dump`.

The Vulkan path is fully native and does not redirect any renderer path to OpenGL.

## Ralph Closure Fixes

The later full-plan Ralph loop replaced the first pass's temporary caster-box emission. OpenGL now keeps CPU-side copies of uploaded model and index buffers so its shadow pass can resolve VBO offsets back to real alias and MD5 geometry. Vulkan exposes a native `VK_Entity_EmitShadowCaster` callback that reuses the renderer's MD2, MD5, and inline BSP decoding without exposing private model storage to `vk_shadow.c`.

Sun views are no longer fixed nested orthographic boxes. The shared frontend fits each cascade to the active camera frustum split, pads the light-space bounds, and snaps the origin to shadow texels for stability. Vulkan receiver sampling now applies the same normal-offset bias as OpenGL by offsetting the sampled world position before page projection.

## Entity Caster Visibility Fix

Date: 2026-05-01

Tasks: `FR-02-T09`, `FR-02-T10`, `FR-02-T11`

First-person player bodies are now marked as explicit `RF_CASTSHADOW` shadow-only entities in both client entity paths, while `RF_WEAPONMODEL` view weapons are excluded from the shared caster list. This keeps the local player's body eligible for shadow generation without letting the held weapon become the caster.

OpenGL and native Vulkan also emit a bounds proxy for `RF_CASTSHADOW` casters that have no drawable model geometry. That closes the fallback path already implied by the shared frontend's model-less caster bounds and prevents the hidden first-person body representation from resolving to a selected caster with no emitted triangles. The `r_shadow_sun` default is now enabled so `r_shadowmaps 1` produces immediately visible entity-caster shadows on first run; users can still set `r_shadow_sun 0` to use only map-authored local shadowlights.

## Transient Effect No-Cast Fix

Date: 2026-05-01

Tasks: `FR-02-T09`, `FR-02-T10`, `FR-02-T11`, `DV-02-T06`

Projectile, particle, and explosion visuals are now treated as transient receiver-only effects. Both client entity paths add `EF_GRENADE` to the existing projectile `RF_NOSHADOW` mask, matching the other rocket, blaster, BFG, ionripper, plasma, tracker, and trap effects. The cgame temp-entity path now mirrors the engine client path by marking plain explosions, BFG explosions, smoke/flash flashes, blaster-impact explosion models, welding-spark flash placeholders, and berserk slam explosion models with `RF_NOSHADOW`.

The shared shadow frontend also rejects known transient effect model paths before caster bounds are resolved. This covers sprites, base projectile objects such as laser/rocket/grenade models, temp explosion/smoke/flash objects, and `models/proj/` assets if a future caller forgets to set `RF_NOSHADOW`. `tools/check_shadowmapping_guardrails.py` now checks these no-cast invariants along with the existing removed-fallback scans.

## Entity Shadowlight Coverage Fix

Date: 2026-05-01

Tasks: `FR-02-T09`, `FR-02-T10`, `FR-02-T11`, `DV-02-T06`

Authored `light` and `dynamic_light` shadowlights are now handled consistently through the engine and cgame entity paths. The cgame `CL_AddShadowLights` path no longer requires the owner entity to be present in the current server frame, no longer drops the whole light when shadowmaps or per-pixel support are disabled, and now uses the same baseline fallback, max-fade test, color reconstruction, owner/configstring metadata, and strict-PVS handoff as the engine client path. This keeps off-PVS-but-influential point and spot lights eligible for frontend PVS2/influence testing instead of skipping them before the renderer can score them.

Spot light culling was also tightened. Cone angles are clamped to a valid projection range before the client builds `dlight_t` records, wide-cone bounding spheres now enclose the actual cone instead of underestimating it, and the shared frontend tests both the light origin sphere and derived influence sphere against area/PVS2 masks. Cone caster-cluster filtering and broad-phase caster rejection now use the derived influence sphere as well, so wide spot lights do not lose valid casters at the edge of the cone. The frontend local-light array now reserves one extra record so enabling sun shadows does not silently discard the last local dlight candidate.

Follow-up spotlight shadowcasting revision: cone lights now keep their authored light radius as the shadow projection range, while the conservative cone bounding sphere is used only for candidate/caster influence tests. This prevents wide spotlights from expanding the shadow camera far plane to the much larger cone bounding-sphere radius, preserving depth precision and keeping cone shadow maps aligned with the actual light range.

Follow-up culling stability revision: area and PVS2 light admission now tests conservative leaf/cluster masks for the whole influence bounds instead of six sparse sphere samples. This removes false negative flicker near area boundaries, doorways, and wide cone edges. The OpenGL per-pixel dlight receiver path now uses 64-bit dlight masks consistently, so selected shadowlights with source indices 32-63 remain eligible for receiver lighting/shadow sampling instead of being dropped by 32-bit shifts.

Follow-up flashlight owner-caster revision: player flashlight cone shadowlights now mark themselves as owner-ignoring lights. Client and cgame render entities carry the originating server entity number into the shared shadow frontend, so the flashlight excludes the first-person owner shadow proxy and weapon-model flags without disabling the rest of the flashlight caster set. Native Vulkan now advertises per-pixel lighting support so EF flashlight entities take the same shadowmapped cone path as OpenGL instead of falling back to a non-shadow point light.

Follow-up regression fix: the owner ignore test is deliberately narrow. It no longer rejects every caster with the same owner entity number, because that starved the flashlight cone of ordinary nearby casters and made the shadowmapped flashlight look unshadowed in play. The filter now only removes the synthetic first-person owner proxy and weapon-model casters; regular world occluders and eligible scene entities remain in the flashlight shadow page.

Follow-up flashlight cache fix: EF_FLASHLIGHT shadow cones are now marked as dynamic shadow producers when they are converted into renderer dlights. Authored `CS_SHADOWLIGHTS` entries remain cacheable `DL_SHADOW_LIGHT` sources, but runtime player flashlights use `DL_SHADOW_DYNAMIC`, so the shared frontend dirties the page from the moving light itself instead of relying on the owner proxy or another animated caster to force a refresh.

Follow-up flashlight receiver fix: OpenGL now treats enabled shadowmaps as requiring the per-pixel dynamic-light receiver path, even when `gl_per_pixel_lighting` is off or legacy dynamic-light options would otherwise clear the dlight list. This keeps the flashlight's selected cone page connected to the world/entity shader pass that actually applies shadows. OpenGL and native Vulkan also use tighter receiver, normal-offset, and raster depth bias defaults for cone views so close-range flashlight casters and nearby world geometry are not peter-panned out of the shadow result.

Follow-up flashlight visibility fix: the EF_FLASHLIGHT per-pixel cone now uses a 1024-unit local and remote trace/radius, a stronger intensity scalar, and a slightly wider 24-degree cone. Native shadowmaps also apply full-strength occlusion instead of leaving 25% of the shadowed dynamic-light contribution behind. The earlier 512-unit, intensity-2 setup could successfully allocate and render a cone page while still looking unshadowed in normal play because the flashlight contribution was too faint at the receiver.

Follow-up viewweapon and torso-motion flashlight revision: owner flashlights are allowed to light the first-person weapon again, but the local player's shadowmapped flashlight origin now moves down to a torso mount before the client builds the `cl_shadow_light_t`. The local cone also receives a `cl_flashlight_torso_sway`-gated torso-motion layer: idle play gets a slow breathing sway that nudges the light with simulated chest expansion and depression, horizontal player speed drives a subtle side/up/forward walking bob, airborne movement damps the stride, and active damage blend or low health adds a sharper high-frequency wobble for moderate to severe damage feedback. The OpenGL and Vulkan receiver-side owner filters were removed, so the viewweapon receives the same owner flashlight as the rest of the scene; the changed origin and synchronized cone direction make that contribution arrive from below the view instead of from the eye/weapon line. Owner-caster filtering remains limited to the synthetic first-person player proxy and weapon-model casters, so the projection pass still keeps regular world and entity occluders.

Follow-up OpenGL viewweapon receiver fix: the torso-mounted origin can place the first-person weapon outside the flashlight spotlight cone even though the world-facing cone and shadow page are valid. OpenGL now tags first-person weapon receivers with their owner entity while uploading per-pixel dlights, keeps the same owner spotlight and shadow page, and relaxes only that owner weapon receiver's cone attenuation. This lets the local weapon receive the torso flashlight without widening the flashlight cone for the world or for other entities.

Build staging note: `tools/stage_install.py` now overlays current root-level `cgame*` and `sgame*` build artifacts into `.install/<basegame>/` after copying the generated base-game tree. The previous staging flow could leave `.install/basew/cgame_x86_64.dll` stale after a targeted cgame rebuild, which masked flashlight fixes that lived in the cgame entity path.

The server-side shadowlight setup now preserves existing render flags when adding `RF_CASTSHADOW` and guards `MAX_SHADOW_LIGHTS` overflow instead of writing past the fixed configstring-backed shadowlight table.

## Compatibility And Guardrails

- Shared cvars remain canonical under `r_`.
- `gl_` and `vk_` aliases remain no-archive compatibility aliases for backend-local configuration.
- `sv_shadow_strict_replication` keeps the multiplayer PVS2 tradeoff explicit.
- `r_shadow_model_exclusion_list` provides a KEX-like model-path exclusion list for content-specific no-cast cases.
- `tools/check_shadowmapping_guardrails.py` blocks the removed no-slot fallback and sticky slot-churn cvars/paths from returning in active source trees, verifies that transient projectile/explosion models remain non-casters, and checks the cgame/entity shadowlight coverage invariants.
- `tools/shadowmapping_repro_smoke.py` records repeatable smoke/repro command lines for the plan's off-PVS, moving bmodel, translated MD2, projectile, sun-cascade, and HOM scene families.

## Verification

Commands run from `E:\Repositories\WORR`:

- `ninja -C builddir-win worr_opengl_x86_64.pdb`
- `ninja -C builddir-win worr_vulkan_x86_64.pdb`
- `python tools\check_shadowmapping_guardrails.py`
- `C:\VulkanSDK\1.4.313.1\Bin\spirv-val.exe .tmp\vk_world_shadow.vert.spv`
- `C:\VulkanSDK\1.4.313.1\Bin\spirv-val.exe .tmp\vk_world_shadow.frag.spv`
- `ninja -C builddir-win worr_opengl_x86_64.pdb worr_vulkan_x86_64.pdb`
- `ninja -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `meson test -C builddir-win --print-errorlogs`
- `ninja -C builddir-win worr_engine_x86_64.dll cgame_x86_64.dll worr_opengl_x86_64.pdb worr_vulkan_x86_64.pdb`
- `python tools\shadowmapping_repro_smoke.py --renderer opengl --renderer vulkan --scene flashlight-owner --filter pcf --wait 140`
- `python tools\shadowmapping_repro_smoke.py --renderer opengl --renderer vulkan --scene off-pvs-light --filter pcf --wait 120`
- `python -m py_compile tools\stage_install.py tools\refresh_install.py tools\shadowmapping_repro_smoke.py tools\check_shadowmapping_guardrails.py`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set vid_fullscreen 0 +set vid_width 1280 +set vid_height 720 +set logfile 1 +set logfile_flush 1 +set logfile_name shadow_opengl_flashlight_after_fix_pcf +set r_screenshot_async 0 +set r_screenshot_message 0 +set r_shadowmaps 1 +set r_shadow_filter 1 +set r_shadow_sun 0 +set cl_shadowlights 1 +set cheats 1 +map test/base1_flashlight +wait 60 +give item_flashlight +use Flashlight +wait 140 +screenshotpng flashlight_after_fix_pcf +r_shadow_dump 0 +quit`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set vid_fullscreen 0 +set vid_width 1280 +set vid_height 720 +set logfile 1 +set logfile_flush 1 +set logfile_name shadow_opengl_flashlight_unshadowed_fix +set r_screenshot_async 0 +set r_screenshot_message 0 +set r_shadowmaps 0 +set r_shadow_sun 0 +set cl_shadowlights 1 +set cheats 1 +map test/base1_flashlight +wait 60 +give item_flashlight +use Flashlight +wait 140 +screenshotpng flashlight_unshadowed_fix +quit`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set vid_fullscreen 0 +set vid_width 1280 +set vid_height 720 +set logfile 1 +set logfile_flush 1 +set logfile_name shadow_opengl_flashlight_torso_on +set r_screenshot_async 0 +set r_screenshot_message 0 +set r_shadowmaps 1 +set r_shadow_filter 1 +set r_shadow_sun 0 +set cl_shadowlights 1 +set cheats 1 +map test/base1_flashlight +wait 60 +give item_flashlight +use Flashlight +wait 140 +screenshotpng flashlight_torso_on +r_shadow_dump +quit`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set vid_fullscreen 0 +set vid_width 1280 +set vid_height 720 +set logfile 1 +set logfile_flush 1 +set logfile_name shadow_opengl_flashlight_torso_unshadowed +set r_screenshot_async 0 +set r_screenshot_message 0 +set r_shadowmaps 0 +set r_shadow_sun 0 +set cl_shadowlights 1 +set cheats 1 +map test/base1_flashlight +wait 60 +give item_flashlight +use Flashlight +wait 140 +screenshotpng flashlight_torso_unshadowed +quit`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set vid_fullscreen 0 +set vid_width 1280 +set vid_height 720 +set logfile 1 +set logfile_flush 1 +set logfile_name shadow_opengl_flashlight_torso_off +set r_screenshot_async 0 +set r_screenshot_message 0 +set r_shadowmaps 1 +set r_shadow_sun 0 +set cl_shadowlights 1 +set cheats 1 +map test/base1_flashlight +wait 200 +screenshotpng flashlight_torso_off +quit`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer opengl +set vid_fullscreen 0 +set logfile 1 +set logfile_name shadow_opengl_smoke +set r_shadowmaps 1 +wait 120 +quit`
- `.install\worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set r_renderer vulkan +set vid_fullscreen 0 +set logfile 1 +set logfile_name shadow_vulkan_smoke +set r_shadowmaps 1 +wait 120 +quit`
- `python tools\shadowmapping_repro_smoke.py --scene sun-cascade --filter vsm --filter evsm --filter pcss --wait 120`

Result: both renderer targets linked, the full build linked all targets, `.install` refreshed and validated, guardrails passed, generated SPIR-V modules validated, both renderer startup smokes exited `0`, both renderer dump smokes exited `0` for VSM/EVSM/PCSS, and Meson reported no tests defined for this build. The flashlight follow-up linked the engine, cgame, OpenGL, and Vulkan targets; staged binaries produced OpenGL and Vulkan flashlight-owner dumps with a selected 1024-unit, intensity-6 dynamic cone and eight eligible casters; and the OpenGL shadowed-vs-unshadowed screenshots showed the same flashlight contribution being occluded only when `r_shadowmaps 1` was enabled. The viewweapon/torso-origin follow-up linked OpenGL and Vulkan, refreshed `.install`, passed guardrails, reran the flashlight-owner smoke on both renderers, and captured staged OpenGL screenshots to confirm the owner flashlight still projects shadows while lighting the first-person weapon from the lower local origin. The torso-motion follow-up linked the engine, cgame, OpenGL, and Vulkan targets again, refreshed `.install`, passed the expanded guardrails, and reran the OpenGL/Vulkan flashlight-owner smoke with selected dynamic cone pages and eight eligible casters on both renderers. The OpenGL viewweapon receiver follow-up linked the OpenGL target and added guardrails so owner weapon receivers retain the owner flashlight contribution even when the torso cone would otherwise exclude the weapon. The cvar-gated idle sway follow-up links the engine and cgame paths to `cl_flashlight_torso_sway`, expands the guarded torso-motion constants, and keeps `0` as a full opt-out while preserving the default subtle breathing/walking/damage motion. A Standard-tier Ralph architect review approved the initial native backend implementation after fixes for caster cache membership, cluster-mask overflow, and Vulkan local-light receiver scoping; the follow-up full-plan pass is recorded in `docs-dev/renderer/shadowmapping-full-plan-2026-04-30.md`.
