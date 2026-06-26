# Q2 AAS Generator Tailoring Implementation Closeout

Date: 2026-06-21

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Purpose

Close the remaining generator tailoring implementation-log checklist row for
the WORR Q2 AAS generator. This document summarizes the implemented
`TTimo/bspc`-derived tool surface, validation gates, packaging integration, and
current reference-map evidence.

## Implemented Generator Surface

- `tools/q2aas/` carries the pinned `TTimo/bspc` snapshot with WORR-local
  compatibility and Q2 reachability tailoring recorded in the credits ledger.
- `meson_options.txt` enables the `q2aas` option by default, and
  `tools/q2aas/meson.build` builds `worr_q2aas` as a non-installed development
  tool plus the q2aas validation, staging, package, and audit run targets.
- `tools/q2aas/cfg/worr_q2.cfg` records the WORR/Q2 player hull and movement
  preset used by validation.
- `tools/q2aas/validate_worr_q2aas.py` owns manifest validation, Q2 `IBSP`
  preflight, archive-backed map extraction, deterministic AAS sidecars,
  reachability diagnostics, entity/content diagnostics, structural/travel
  baselines, reference-feature coverage, team objective diagnostics, campaign
  progression diagnostics, and expected-failure schema smokes.
- `tools/q2aas/validation_manifest.json` currently describes eight locally
  staged validation maps: `mm-rage`, `q2dm1`, `q2dm2`, `q2dm8`, `q2ctf1`,
  `base1`, `base2`, and `train`.
- `tools/refresh_install.py --package-q2aas-aas` can re-inject staged generated
  `.aas` files after `.install/basew/pak0.pkz` is rebuilt from assets, then
  audit archive-member hashes through generic staged release validation.

## Reference Map Coverage

The current local reference set validates the combined checklist breadth:

| Category | Current staged evidence |
|---|---|
| Current WORR DM | `mm-rage` |
| Canonical DM | `q2dm1`, `q2dm2` |
| Open DM | `q2dm8` |
| CTF/team objective | `q2ctf1` |
| Campaign/coop | `base1`, `base2`, `train` |
| Liquid/water | `q2dm1`, `q2dm2`, `q2ctf1`, `base1`, `base2`, `train` |
| Teleport entity diagnostics | `q2ctf1`, `train` |
| Door diagnostics | `q2dm8`, `q2ctf1`, `base1`, `base2`, `train` |
| Elevator/platform routes | `mm-rage`, `q2dm1`, `q2dm2`, `q2ctf1`, `train` |

Slime and lava stay as future feature-specific candidate categories in the
manifest because the currently staged local BSP set does not contain those
brush contents. The broader combined liquid checklist row is satisfied by the
validated water/liquid coverage above.

## Validation

- `meson compile -C builddir-win worr_q2aas`
- `meson compile -C builddir-win q2aas-staged-smoke`
- `meson compile -C builddir-win q2aas-stage-aas`
- `meson compile -C builddir-win q2aas-stage-audit`
- `.install/basew/maps/` currently contains the eight generated `.aas` files and
  their corresponding local BSP validation inputs.
- Static release workflow evidence: `.github/workflows/nightly.yml` and
  `.github/workflows/release.yml` build the shared release matrix from
  `tools/release/targets.py`; Linux and macOS runners both execute
  `meson compile -C builddir`, and the default-enabled `q2aas` option includes
  `worr_q2aas` in those builds.

## Provenance

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto files were
  imported for this closeout.
- The locally staged Quake II BSPs are validation inputs under `.install/`, not
  committed WORR source artifacts.
- Existing imported BSPC files and WORR-local modifications remain covered by
  `docs-dev/q3a-botlib-aas-credits.md`.
