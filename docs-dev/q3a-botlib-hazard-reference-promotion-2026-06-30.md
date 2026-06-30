# Q3A BotLib Hazard Reference Promotion

Date: 2026-06-30

Tasks: `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Scope

This slice promotes the previous hazard-context gap into an accepted runtime
movement scenario. The prior candidate-discovery work found a scratch lava
candidate, but this round uses the official Quake II campaign map `fact2`
instead because it validates cleanly and carries real lava, slime, water,
mover, door, elevator, `trigger_hurt`, and `target_laser` evidence.

## Implementation

- Staged `.install\basew\maps\fact2.bsp` from the local clean Quake II data.
- Added optional `fact2` to `tools/q2aas/validation_manifest.json` with strict
  Q2 BSP, clean-lump, spawn, item, high-value reachability, metric, and travel
  baselines.
- Added `fact2` to the campaign, liquid/hazard, water, slime, lava, mover,
  elevator, door, and new `runtime_hazard_entity_reference` coverage groups.
- Promoted bot scenario mode `96` from `movement_hazard_context_gap` on `base2`
  to accepted `movement_hazard_context` on `fact2`.
- Renamed the server mode helper from `SV_BotFrameCommandSmokeIsHazardContextGap`
  to `SV_BotFrameCommandSmokeIsHazardContext`.
- Updated the movement reference audit to prefer the accepted
  `movement_hazard_context` row while retaining fallback compatibility with the
  old gap name.

## Evidence

`q2aas-staged-smoke` now validates ten local staged maps when optional
`fact2` and `q2dm7` are present. Only `crouch_reference` remains incomplete.

`fact2` validation evidence:

- `3015` AAS areas and `4650` reachability records.
- `2865` walk, `47` barrier-jump, `256` jump, `778` walk-off-ledge,
  `664` swim, `30` water-jump, `7` elevator, and `2` rocket-jump routes.
- `23` slime brushes, `5` lava brushes, `2` water brushes.
- `120` mapped AAS slime areas and `182` mapped AAS lava areas.
- `14` `trigger_hurt` entities and `4` `target_laser` entities.
- `55` doors, `39` elevator/platform entities, and validated campaign
  progression diagnostics.

The promoted live scenario passed from
`.tmp\bot_scenarios\movement_hazard_context_fact2.json`:

- `movement_hazard_context` mode `96`, map `fact2`.
- `60` frames, `60` commands, `60` route commands, `0` route failures.
- Runtime interaction context saw positive hazard evidence.

The movement reference audit now reports:

- `natural_crouch`: `blocked_no_reference_content`.
- `hazard_context`: `accepted`.

## Artifacts

- `.tmp\q2aas\validation-report.json`
- `.tmp\q2aas\stage-report.json`
- `.tmp\q2aas\refresh-package-archive-report.json`
- `.tmp\q2aas\refresh-package-archive-audit-report.json`
- `.tmp\bot_scenarios\movement_hazard_context_fact2.json`
- `.tmp\bot_scenarios\movement_hazard_context_fact2.md`
- `.tmp\bot_scenarios\movement_reference_gap_audit.json`
- `.tmp\bot_scenarios\movement_reference_gap_audit.md`

## Validation

```powershell
python tools\q2aas\discover_reference_candidates.py --map E:\Games\q2Clean\baseq2\maps\fact2.bsp --convert-top 1 --output .tmp\q2aas\reference-candidates-fact2 --json-out .tmp\q2aas\reference-candidates-fact2.json --markdown-out .tmp\q2aas\reference-candidates-fact2.md
meson compile -C builddir-win q2aas-staged-smoke
python -m unittest tools.bot_scenarios.test_audit_movement_reference_gaps tools.q2aas.test_discover_reference_candidates tools.q2aas.test_validate_worr_q2aas
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win worr_ded_x86_64
meson compile -C builddir-win q2aas-stage-aas
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
python tools\bot_scenarios\run_bot_scenarios.py --scenario movement_hazard_context --artifact-dir .tmp\bot_scenarios\movement_hazard_context_fact2 --json-out .tmp\bot_scenarios\movement_hazard_context_fact2.json --markdown-out .tmp\bot_scenarios\movement_hazard_context_fact2.md --format both --timeout 40
python tools\bot_scenarios\audit_movement_reference_gaps.py --q2aas-report .tmp\q2aas\validation-report.json --json-out .tmp\bot_scenarios\movement_reference_gap_audit.json --markdown-out .tmp\bot_scenarios\movement_reference_gap_audit.md
```

## Follow-Up

Natural crouch is the remaining reference-map movement gap. The next M6 slice
should find or author a map that generates `TRAVEL_CROUCH > 0`, add it to
`crouch_reference`, and promote `movement_crouch_gap` only after the audit marks
that row ready.
