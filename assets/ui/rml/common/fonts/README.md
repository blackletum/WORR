# WORR RmlUi Font Staging

No binary fonts are staged in this first-round asset contract.

Expected runtime contract:
- The RmlUi font service registers the primary UI family as `WORR UI`.
- The optional monospace/debug family is registered as `WORR Mono`.
- `assets/ui/rml/common/theme/base.rcss` refers to those family names through
  RCSS variables so the final implementation can switch between the stock
  RmlUi font engine and a WORR-specific font bridge without rewriting content.
- Font file selection, licensing, fallback order, and `.install/` staging are
  still pending under `FR-09-T04`.

Notes for future work:
- Do not add binary fonts here without recording the source, license, and
  install-path decision in `docs-dev/`.
- RmlUi requires TTF faces to be loaded through the C++ interfaces before RCSS
  can reference the family names.
