# Q2AAS Generator Policy and Semantics Closeout

Date: 2026-06-21

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Purpose

Close the next q2aas Phase 0/1 checklist slice by turning generator-scope,
presence, Q2 content/surface, and BSPX decisions into machine-readable
validation evidence instead of loose project notes.

## Implementation

- Extended `tools/q2aas/validate_worr_q2aas.py` with a top-level
  `generator_scope` report section. WORR's supported q2aas path is now recorded
  as Q2 `IBSP` version 38 only, with non-Q2 inherited BSPC loaders treated as
  compiled compatibility code isolated by the strict `--require-q2-bsp`
  validation gate.
- Added a top-level `presence_policy` report section parsed from
  `tools/q2aas/cfg/worr_q2.cfg`. The report records the standing and crouched
  player hulls, movement constants, and the explicit decision to defer a
  large/NPC presence type until monster AI or another non-player navigation
  consumer needs it.
- Added BSP texinfo/brushside surface-flag inspection for `SURF_SLICK`,
  `SURF_SKY`, `SURF_NODRAW`, translucency flags, and related Q2 surface
  metadata.
- Added per-map `diagnostics.aas_semantic_policy`, tying Q2 water/slime/lava
  brush contents to AAS area contents and travel policy, recording
  `trigger_hurt` as diagnostic-only hazard evidence, documenting
  slick/sky/nodraw/detail/translucent handling, and stating the BSPX tolerance
  policy for trailing Q2R/BSPX extension markers.
- Updated `tools/q2aas/README.WORR.md` so developers can find these report
  fields from the q2aas vendor notes.

## Checklist Resolution

- Future imported-file local edits remain gated by the credits ledger and
  `Modified for WORR` notes; no imported source was edited in this round.
- Unused Q1/HL/Sin/Q3 BSPC map loaders are isolated from the supported WORR path
  by validation policy rather than removed from vendored shared code in this
  round.
- WORR player presence policy is now report-backed for standing/crouch hulls and
  the large/NPC deferral decision.
- Water, slime, lava, hurt, slick, sky, nodraw, detail, and translucent handling
  now has explicit validation-report semantics.
- BSPX markers after the standard Q2 lump range are recorded and tolerated when
  the standard lump table remains clean.

## Validation

- `python -m py_compile tools\q2aas\validate_worr_q2aas.py`
- `meson compile -C builddir-win q2aas-staged-smoke`
  - Passed with `mm-rage` still at `428` areas, `562` reachability records,
    `4` clusters, and the new summary line:
    `water=mapped_no_reference`, `slime=mapped_no_reference`,
    `lava=mapped_no_reference`, `hurt=diagnostic_only`,
    `slick=diagnostic_only`, `bspx=tolerated_as_trailing_extension`.
  - `.tmp\q2aas\validation-report.json` now contains
    `generator_scope.legacy_loader_policy = isolated_by_validation`,
    `generator_scope.supported_map_format = quake2_ibsp38`,
    parsed active presences `PRESENCE_NORMAL,PRESENCE_CROUCH`, and
    per-map `diagnostics.aas_semantic_policy`.
- `meson compile -C builddir-win q2aas-stage-aas`
  - Passed and refreshed `.tmp\q2aas\stage-report.json` plus
    `.install\basew\maps\mm-rage.aas`.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`
  - Passed, re-injected `maps/mm-rage.aas`, wrote the refresh package reports,
    and validated the staged `windows-x86_64` payload.
