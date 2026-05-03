# mm-rage Dynamic Shadowlights

## Task
Investigate missing `dynamic_light` illumination and shadows in `mm-rage`, using
the reported `dynamic_light` at `768 176 256` and test view coordinates
`768 32 256`, yaw `90`.

## Findings
- The compiled `mm-rage.bsp` contains 46 `dynamic_light` entities with
  `shadowlightradius`; the reported one is authored as a cone light targeting
  `info_notnull5`.
- These entities omit `shadowlightresolution`. The client-side submission path
  treated `resolution == 0` as "not a shadowlight", so the lights could still be
  considered ordinary dlights but never entered the shared shadow frontend.
- Several `mm-rage` lights use the shorter `shadowconeangle` key, which was not
  parsed and therefore fell back to the default cone angle.
- Existing development notes already documented `light` and `radius` fallback
  semantics for authored shadowlights, but the current spawn code only accepted
  `shadowlightradius`.

## Change
- Authored shadowlights now become shadow-casting lights whenever they have a
  positive radius and `cl_shadowlights` is enabled. A zero resolution is left as
  "use the renderer policy default" rather than "disable shadowing".
- Authored runtime shadowlight radius resolution now follows:
  1. `shadowlightradius`
  2. `radius`
  3. `light`, but only for `dynamic_light`
- `shadowconeangle` is accepted as an alias for `shadowlightconeangle`.
- Shadowlight configstring loading now bounds its loop by the active protocol
  remap instead of blindly scanning the static maximum.
- Plain BSP `light` entities are not promoted from their map-compiler `light`
  key alone; they still need explicit runtime radius keys. This avoids turning
  baked lighting markers into active shadow lights.

## Verification Target
Run `mm-rage` with OpenGL or Vulkan, teleport to `768 32 256` with yaw `90`,
then use `r_shadow_dump`. The reported area should show configstring
shadowlight candidates instead of `candidates=0`.

For deathmatch repros, join the match before teleporting:
`team free; teleport 768 32 256 0 90 0; viewpos; r_shadow_dump`.

Verified after the fix:
- OpenGL: `viewpos` reported `(768 32 242) : 91`; `r_shadow_dump`
  reported `candidates=64`, `selected=4`, `configstring_lights=61`, and
  selected the cone shadowlight at `(768 176 256)`.
- Vulkan: `viewpos` reported `(768 32 242) : 85`; `r_shadow_dump`
  reported `candidates=64`, `selected=4`, `configstring_lights=61`, and
  selected the same cone shadowlight at `(768 176 256)`.
