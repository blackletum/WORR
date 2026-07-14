# WORR RmlUi Widget SVG Assets

Date: 2026-07-08

Task IDs: `FR-09-T04`, `FR-09-T06`, `FR-09-T07`, `FR-09-T09`,
`DV-07-T02`, and `DV-07-T04`.

These assets are compact first-party SVG affordances for RmlUi widgets and
setting rows. They replace the previous menu-command pictogram pass and are
intended to clarify control type, not decorate high-level menu navigation.

Keep new widget assets inside WORR's current OpenGL SVG subset: `line`,
`polyline`, `polygon`, `rect`, `circle`, and simple `path` commands made from
`M`, `L`, `H`, `V`, and `Z`.


## PNG pipeline (2026-07-12)

Shipped documents reference the `.png` siblings, rendered from these SVG
sources by `tools/ui_gen_metal_skins.py` (8x supersampled and antialiased).
Generated widget PNGs are at least 64px in each source view-box dimension so
the engine keeps them out of the legacy scrap atlas and exposes valid
standalone texture handles to RmlUi. The SVGs remain the editable source of
truth and the renderer-neutral fallback; after editing one, rerun the
generator to refresh its PNG.
