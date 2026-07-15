# Native Vulkan Particle Fog Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: deterministic particle fog is covered by retained native-Vulkan versus
OpenGL evidence. Beam, flare, sprite, and other effect receivers remain open.

## Fixture

`assets/renderer_parity/fr01_particle_fog.cfg` reuses the authored global-fog
map and enables the debug-build `cl_testparticles` field after the fixed camera
is in place. The engine creates 8,192 fixed particles in view, while both
backends retain the legacy non-additive style and a part scale of two. This
avoids gameplay timing, networking, and random emitter state while exercising
the native Vulkan particle submission path as a real fog receiver.

## Gate and result

The 640 x 500 crop contains 320,000 pixels. It exact-compares OpenGL and
native Vulkan with no tolerated error. The fogged deterministic particle-field
probe requires at least 300,000 pixels of exact `75 / 132 / 174` on each
backend, zero count delta, and a 1.0 mask intersection-over-union; the capture
produced that color across all 320,000 crop pixels on both renderers.

The same fogged world without the particle field differs in 272,700 crop
pixels, proving the required probe cannot be satisfied by the world-only
background. A fog-disabled particle capture differs from the fogged capture
at all 320,000 crop pixels (mean absolute RGB difference `27 / 88 / 78`), so
the receiver is also demonstrably under authored fog. Vulkan validation emitted
no diagnostics.

## Scope

The fixture needs a debug-capable capture build because `cl_testparticles` is
intentionally compiled under `USE_DEBUG`; the repository's parity build is
configured with debug support. This is a test-only scene, not a user-facing
renderer control. Beam, flare, sprite, and specialised effect fog receivers
remain `FR-01-T12` work.
