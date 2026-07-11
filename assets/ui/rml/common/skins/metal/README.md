# WORR RmlUi Grimy-Metal Skin Assets

Generated raster chrome for the shared RmlUi themes. Do not hand-edit the
PNGs: they are produced deterministically by `tools/ui_gen_metal_skins.py`,
which also rewrites the `@spritesheet worr-metal` block in
`common/theme/base.rcss` between its GENERATED markers.

```
python tools/ui_gen_metal_skins.py            # regenerate everything
python tools/ui_gen_metal_skins.py --seed N   # try a different grunge roll
```

## Files

- `ui-metal.png` — 2x-resolution sprite sheet (`resolution: 2x` in the
  spritesheet block). Widget chrome: buttons, fields, checkboxes, range
  tracks/thumbs, progress, scrollbars, panel/popup/dropdown frames, the
  screen-edge grime vignette, and the header rail.
- `backdrop.png` — seamless dark plate tile for `.screen`/`.ui-route`
  backgrounds, used with `decorator: image(../skins/metal/backdrop.png repeat)`.
- `plate.png` — seamless panel-interior tile layered beneath the `panel` and
  `popup` ninepatch frames.

## Usage rules

- Widget chrome uses `ninepatch(<sprite>, <sprite>-inner)`; every ninepatch
  sprite in the sheet has a matching `-inner` stretch rect.
- RmlUi rejects `repeat` fit modes on sprites, so anything that must tile at
  arbitrary size is a standalone PNG (`backdrop.png`, `plate.png`) drawn with
  the plain `image(<file> repeat)` decorator. The renderer bridge registers
  RmlUi file textures with `IF_REPEAT` to make that work.
- State variants (`-hover`, `-focus`, `-active`, `-disabled`) share the same
  base plate per widget family so decorator swaps read as lighting changes,
  not texture jumps.
- Keep the flat `background-color`/`border-color` declarations in RCSS: they
  are the graceful fallback when decorators are disabled (high-visibility
  mode sets `decorator: none`) and preserve soft border transitions.
- The legacy flat SVG skins in `../widgets/` remain the renderer-neutral
  fallback set and the reference for the palette.
