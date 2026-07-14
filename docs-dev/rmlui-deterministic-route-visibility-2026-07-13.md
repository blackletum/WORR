# RmlUi Deterministic Route Visibility (2026-07-13)

Task IDs: `FR-09-T04`, `FR-09-T09`, `DV-03-T07`, `DV-07-T02`,
`DV-07-T04`

## Summary

Guarded reduced-motion captures exposed an RmlUi load-time failure that could
leave otherwise valid menu geometry partially or completely invisible. The
failure was most obvious on Legacy Key Bindings and Address Book: top chrome,
headers, field rows, and status controls could remain sampled at an entrance
animation's transparent frame even after hundreds of rendered frames.

The shared menu theme now avoids decorative load-time opacity/margin fades.
Routes begin in their final visual state under both normal and reduced-motion
preferences. Interaction feedback remains animated where safe: focus pulse,
progress fill, and the active key-capture pulse are unchanged, and the shared
reduced-motion selectors still suppress them when requested.

## Root cause and rejected approaches

The documents are loaded from memory, hydrated while hidden, shown, and then
updated. RmlUi can instantiate a CSS entrance animation while constructing
elements, before the runtime has synchronized its dynamic accessibility class.
Cancelling that animation later can retain the sampled animation-origin
opacity in compiled geometry rather than resolving to the ordinary completed
style.

Two increasingly strict class-based approaches were tested:

- explicitly pinning reduced-motion containers to `opacity: 1`; and
- injecting reduced-motion classes into the in-memory body markup before
  `LoadDocumentFromMemory()`.

Preloading accessibility state remains useful and is retained, but neither
CSS cancellation nor inline animation cancellation was deterministic for all
document shapes. The 16-field Address Book could still lose most geometry.
This made load-time fades unsuitable for a correctness-critical menu shell.

## Final contract

The following decorative entrance declarations were removed from
`base.rcss`:

- route/screen fade;
- page/menu header fade and margin slide;
- general content/group stagger fade;
- footer/status-bar stagger fade;
- top-bar fade and margin slide;
- popup entrance fade/slide;
- card-row/card-open panel fades; and
- dropdown panel fade.

Their unused keyframe definitions remain harmless source vocabulary for now,
but no base route selector instantiates them. The runtime still preloads
`ui-high-visibility` and `ui-reduced-motion` on the body before document
construction and synchronizes both body/document classes afterward. This
prevents a first-frame accessibility flash and keeps live preference changes
consistent.

No renderer fallback or redirection is involved. The correction applies to
the shared RmlUi presentation contract and keeps OpenGL, Vulkan, and RTX/vkpt
ownership boundaries unchanged.

## Validation

`tools/ui_smoke/check_rmlui_keybind_provider.py` now rejects restoration of
the unreliable load-time animation declarations and validates accessibility
preload wiring. Its focused suite also rejects late preload and interrupted
opacity regressions.

Reduced-motion 960x720 evidence was accepted for:

- `rmlui_keys_live_provider_20260713`;
- `rmlui_legacykeys_live_provider_20260713`;
- `rmlui_weapons_live_provider_20260713`; and
- `rmlui_addressbook_live_provider_20260713`.

The final frames show complete brand/top bars, page headers, all route
content, action controls, and status bars. Their logs contain no RCSS parser,
animation, missing-media, warning, or error entries. Normal-motion comparison
captures were used only to isolate the fault; the accepted evidence keeps
`ui_rml_reduced_motion=1` through the standard harness.

## Remaining work

- Extend screenshot assertions beyond the sample smoke route so missing
  topbar/header/status regions fail automatically on every representative
  route.
- Complete the large-text, localization, controller-navigation, viewport,
  and native renderer matrices.
- Reintroduce route entrance motion only if a future RmlUi/runtime path can
  prove deterministic animation initialization without compromising reduced
  motion or first-frame visibility.
