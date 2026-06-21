# Q2AAS Reference Map and BotLib Runtime Adapter Round

Date: 2026-06-21

Tasks: `FR-04-T11`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Purpose

Close the next checklist slice by expanding q2aas reference-map validation
beyond the first local DM map and by promoting the already-compiled Q3A AAS
runtime/adapter trace paths from temporary checklist wording to documented WORR
ownership.

## Implementation

- Staged additional local Quake II reference BSPs under `.install/basew/maps/`:
  `q2dm2.bsp`, `q2dm8.bsp`, `q2ctf1.bsp`, `base1.bsp`, `base2.bsp`, and
  `train.bsp`.
- Extracted `maps/q2ctf1.bsp` from local `E:\Games\Quake2\ctf\pak0.pak` for
  validation. The extracted BSP remains a local validation input and is not a
  committed source artifact.
- Extended `tools/q2aas/validate_worr_q2aas.py` with:
  - Manifest-row-owned diagnostic gates, so broad command-line strictness does
    not force the DM high-value pickup reachability gate onto CTF/campaign
    structural references.
  - `team_objective_report`, recording CTF team spawns, flags, and flag
    reachability from spawn-connected AAS areas.
  - `campaign_progression_report`, recording campaign goals, changelevels,
    keys, triggers, doors, and mover surfaces as route-adjacent progression
    evidence.
- Updated `tools/q2aas/validation_manifest.json` with baselines for `q2dm2`,
  `q2dm8`, `q2ctf1`, `base1`, `base2`, and `train`. `q2dm1` and `q2dm2` keep
  strict high-value pickup reachability gates; broader open-layout, CTF, and
  campaign references record high-value reachability but do not gate on it.
- Updated the q2aas reference coverage categories so DM, open DM, CTF, team
  objective, campaign, water/liquid, mover, teleport, elevator, and door
  coverage now point at validated local candidates where available. Slime and
  lava remain explicit pending feature categories because the staged local set
  did not contain those brush contents.
- Documented that the compiled Q3A AAS runtime C set is complete for the
  current WORR route/query surface. This is the AAS runtime subset, not the full
  Q3A arena AI/EA/goal system.
- Promoted `AAS_Trace` and `AAS_EntityCollision` ownership in documentation:
  static-world traces are owned by the WORR-native active-map Q2 BSP collision
  bridge, and entity traces cross `botlib_adapter.*` into
  `BotRuntimeEntityTrace`, which calls the server-game `gi.clip` path for
  linked BBOX/BSP entities.

## Local Reference Input

- Loose BSPs copied from `E:\Games\Quake2\baseq2\maps\`:
  `q2dm2.bsp`, `q2dm8.bsp`, `base1.bsp`, `base2.bsp`, and `train.bsp`.
- CTF BSP extracted from `E:\Games\Quake2\ctf\pak0.pak`:
  `maps/q2ctf1.bsp`.
- Staged copies live under `.install\basew\maps\` for local validation only.
  The BSPs are not committed WORR source artifacts.

## Validation

- `python -m py_compile tools\q2aas\validate_worr_q2aas.py tools\q2aas\test_validate_worr_q2aas.py`
- `python -m unittest tools.q2aas.test_validate_worr_q2aas`
- `meson compile -C builddir-win q2aas-staged-smoke`
- `meson compile -C builddir-win q2aas-stage-aas`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`

Fresh `q2aas-stage-aas` metrics:

| Map | Areas | Reachability | Clusters | Key evidence |
|---|---:|---:|---:|---|
| `mm-rage` | 428 | 562 | 4 | Required WORR smoke map; 1 elevator route. |
| `q2dm1` | 1245 | 3066 | 3 | DM/water/elevator baseline; 254 swim, 19 water-jump, 10 elevator routes. |
| `q2dm2` | 1313 | 2476 | 8 | Multi-level DM baseline; 465 swim, 21 water-jump, 71 ladder, 30 elevator routes. |
| `q2dm8` | 1499 | 2846 | 3 | Open DM baseline; 1972 walk, 224 jump, 613 walk-off-ledge routes, door diagnostics. |
| `q2ctf1` | 2122 | 4447 | 6 | CTF baseline; team objective report `validated`, 2 reachable flags, 456 swim routes. |
| `base1` | 4235 | 5865 | 14 | Campaign diagnostics baseline; 39 triggers, 4 doors, 92 swim routes. |
| `base2` | 3500 | 4641 | 24 | Campaign/liquid baseline; progression report `validated`, 35 triggers, 19 doors. |
| `train` | 3316 | 4590 | 18 | Campaign/mover baseline; progression report `validated`, 52 triggers, 24 doors, 21 elevator routes. |

The install refresh packaged and audited 8 AAS archive members:
`maps/mm-rage.aas`, `maps/q2dm1.aas`, `maps/q2dm2.aas`, `maps/q2dm8.aas`,
`maps/q2ctf1.aas`, `maps/base1.aas`, `maps/base2.aas`, and
`maps/train.aas`.

## Provenance

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported.
- The q2aas validator and manifest changes are WORR-owned tooling.
- The Q3A runtime/adapter closeout documents already-imported files and
  existing WORR-native callback ownership; it does not add imported source.
