# Native Vulkan Sprite Fog Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: the native Vulkan sprite fog receiver has retained OpenGL-versus-Vulkan
evidence. Flare and other specialised effect receivers remain open.

## Fixture

`tools/renderer_parity/generate_sprite_fog_fixture.py` derives
`worr_fr01_sprite_fog.bsp` from the authored global-fog map and adds a
`misc_model` that registers `sprites/s_bfg1.sp2` through the normal game and
cgame entity route. The sprite is therefore submitted through
`VK_Entity_AddSprite`, with its native billboard, texture, alpha/cutout, and
fog receiver behavior; it is not a renderer test hook or an OpenGL fallback.

The entity has a fixed scale of four and a fixed camera crop, making the
otherwise ordinary BFG sprite large enough for durable headless coverage.
The generated map, configuration, manifest, and source checks are packaged as
normal renderer-parity assets.

## Gate and result

The 360 x 400 crop contains 144,000 pixels. Vulkan differs from OpenGL by at
most one RGB value, with mean absolute error `0.063646 / 0.036597 / 0.306354`;
the manifest therefore allows error one and no over-threshold pixels. The
fogged sprite probe accepts `74..76 / 124..128 / 173..177`, finding 44,989
OpenGL and 45,277 Vulkan pixels (0.636% count delta and 0.987012
intersection-over-union). The identical fogged map without the sprite has zero
pixels in that range, so the probe is receiver-specific. Vulkan validation
emitted no diagnostics.

## Fog activation control

The fog-disabled sprite control differs from its no-sprite equivalent across
60,212 pixels above RGB delta eight on both renderers, with mean absolute
contribution `57.132 / 92.365 / 64.391`. With the authored dense global fog
enabled, the same sprite contribution is attenuated to a maximum delta of
`3 / 2 / 3` in OpenGL and `4 / 3 / 4` in Vulkan, leaving no pixels above eight.
This confirms that the retained gate exercises sprite fog attenuation, not a
clear sprite layered over a fogged world.

## Remaining scope

Global, height, sky, transparent world, particle, beam, and sprite fog
receivers now have durable native Vulkan/OpenGL evidence. `RF_FLARE` and
remaining specialised effect receivers remain `FR-01-T12` work.
