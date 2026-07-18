# Native Renderer Selection Control

Task: `FR-03-T11` (with renderer naming/lifecycle supplied by `FR-02-T01`).

## Problem

Each Video settings implementation labelled a selector **Rendering Backend**,
but bound it to `gl_shaders`.  That cvar changes OpenGL's legacy-versus-GLSL
implementation path only.  It did not select native Vulkan, and it was a
misleading no-op when Vulkan was already active.

## Implementation

The legacy menu, cgame JSON menu, and RmlUi Video route now bind the selector
to `r_renderer`, using the engine's canonical renderer names:

| Label | `r_renderer` value | Native lifecycle |
| --- | --- | --- |
| OpenGL | `opengl` | OpenGL renderer DLL |
| Vulkan | `vulkan` | native raster Vulkan renderer DLL |
| Vulkan RTX | `rtx` | native Vulkan path-tracing renderer DLL |

`r_renderer` now has `CVAR_ARCHIVE | CVAR_RENDERER`.  A selection persists in
the client configuration and the existing `CVAR_RENDERER` processing performs
the normal renderer restart.  This continues to use the native Vulkan and RTX
lanes; it adds no Vulkan-to-OpenGL fallback or redirection.

The RmlUi contract examples were converted from legacy `gl`/`vk` aliases to
the same canonical values.  The engine still accepts those aliases for command
line and existing-config compatibility.

## Boundary

The visible choices reflect the renderers compiled into this distribution.
Adapter-specific filtering and a user-facing capability report remain
`FR-02-T01`, `FR-02-T02`, and `FR-03-T11` work.  In particular, this change
does not claim that every adapter can execute the RTX lane, nor does it expose
the unfinished Vulkan MSAA setting as a silent control.

## Validation

`tools/renderer_parity/test_shared_renderer_backend_control_source.py` verifies
all three Video routes, canonical names, archived lifecycle flag, and the
native restart route.  The existing RmlUi renderer-family matrix remains the
guardrail that Vulkan and RTX use distinct native RmlUi interfaces.
