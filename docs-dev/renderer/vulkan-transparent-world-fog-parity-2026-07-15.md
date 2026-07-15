# Native Vulkan Transparent-World Fog Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: the transparent world-material fog receiver now has retained
OpenGL-versus-native-Vulkan evidence. Particle, beam, flare, and other effect
receivers remain open.

## Fixture

`tools/renderer_parity/generate_transparent_fog_fixture.py` derives a fogged
map from the existing two-layer `SURF_TRANS33`/`SURF_TRANS66` ordering fixture.
It preserves the background, far-green, and near-red planes, then authors the
same global fog worldspawn inputs used by the opaque global-fog gate. The
fixture therefore exercises blending order and fog composition together rather
than checking an opaque substitute.

## Gate and result

The central 240 x 210 blend crop contains 50,400 pixels. Vulkan is one green
and blue RGB level above OpenGL for the fogged blend (`0 / 1 / 1` mean and
maximum error), so the retained contract permits RGB error one with no larger
pixel and locks mean error to `0 / 1 / 1`. The fogged blend probe accepts the
OpenGL reference color `93 / 119 / 163` with tolerance one; all 50,400 pixels
match on both backends with a 1.0 mask intersection-over-union.

Every crop pixel differs from the clear transparent-ordering capture, with mean
absolute RGB difference `79.307 / 104.409 / 150.225`, confirming fog is active
in the blend receiver. Vulkan validation emitted no diagnostics.

## Remaining scope

Opaque world/inline-BSP, transparent-world, global, height, and sky fog now
have visual gates. Particle, beam, flare, sprite, and other effect fog
receivers remain `FR-01-T12` work.
