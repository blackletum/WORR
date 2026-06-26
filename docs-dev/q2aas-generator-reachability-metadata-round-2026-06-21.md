# Q2AAS Reachability, Metadata, and Reference Baseline Round

Date: 2026-06-21

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Purpose

Close the next Phase 1 q2aas checklist slice by promoting route-policy
diagnostics, deterministic metadata policy, and the first optional id Quake II
deathmatch reference map into repeatable validation evidence.

## Implementation

- Added `metadata_policy` to the q2aas validation report and generated
  `.aas.meta.json` sidecars. Sidecars remain deterministic scratch validation
  artifacts under `.tmp/q2aas`; release packages carry AAS identity through
  package/audit reports and staged-release archive member validation instead of
  shipping `.aas.meta.json` members.
- Added per-map `diagnostics.reachability_policy` for water entry/exit,
  movers, teleports, and rocket-jump action ownership. The report records
  generated swim, water-jump, elevator, teleport, and rocket-jump route counts,
  plus the decision that real rocket-jump weapon execution belongs to the
  higher-level behavior/weapon action layer.
- Added per-map `diagnostics.mover_route_report`, pairing generated
  `TRAVEL_ELEVATOR` / `TRAVEL_TELEPORT` counts with door/elevator/teleport
  entity inventories.
- Tightened manifest-relative path resolution so validation manifests resolve
  map/archive paths relative to their configured root before considering the
  current process directory.
- Promoted locally staged `q2dm1.bsp` as an optional reference baseline in
  `tools/q2aas/validation_manifest.json`. When present, it now enforces
  structural and travel-count minima for broader DM routing, weapon/item pickup
  coverage, water entry/exit, and elevator/vertical movement.

## Local Reference Input

- Source used for this validation round:
  `E:\Games\Quake2\baseq2\maps\q2dm1.bsp`
- Staged validation copy:
  `.install\basew\maps\q2dm1.bsp`
- The BSP itself is local validation input and is not a committed WORR source
  artifact. The committed contract is the optional manifest entry and its
  baselines when the map is available.

## Validation

- `python -m py_compile tools\q2aas\validate_worr_q2aas.py tools\q2aas\test_validate_worr_q2aas.py`
- `python -m unittest tools.q2aas.test_validate_worr_q2aas`
- `meson compile -C builddir-win q2aas-staged-smoke`
  - `mm-rage` remained at `428` areas, `562` reachability records, `4`
    clusters, and `1` elevator route.
  - `q2dm1` passed with `1245` areas, `3066` reachability records, `3`
    clusters, `2019` walk routes, `30` barrier jumps, `164` jumps, `541`
    walk-off-ledge routes, `254` swim routes, `19` water-jump routes, `10`
    elevator routes, and `28` rocket-jump route candidates.
  - `q2dm1` diagnostics reported `11` spawns, `83` items, `3` high-value
    pickups, `2` movers, `0` spawn/item origin orphans, `0` unreachable
    high-value pickups, and water route policy `validated`.
- `meson compile -C builddir-win q2aas-stage-aas`
  - Passed and staged `.install\basew\maps\mm-rage.aas` plus
    `.install\basew\maps\q2dm1.aas`.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`
  - Passed, packaged `maps/mm-rage.aas` and `maps/q2dm1.aas`, audited both
    archive members, and validated the staged `windows-x86_64` payload.
