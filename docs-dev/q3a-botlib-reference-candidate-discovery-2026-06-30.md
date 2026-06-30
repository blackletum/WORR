# Q3A BotLib Reference Candidate Discovery

Date: 2026-06-30

Tasks: `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Scope

This slice advances the M6 movement/hazard reference-map roadmap item. The
previous audit made crouch and hazard gaps machine-checkable, but it still left
candidate acquisition as manual BSP spelunking. This round adds repeatable
candidate discovery, promotes one validated slime reference, and keeps the
runtime hazard scenario blocked until a promotable hurt/laser hazard map exists.

## Implementation

- Added `tools/q2aas/discover_reference_candidates.py` to scan Quake II BSPs for
  water, slime, lava, ladder, door, teleport, and runtime hazard-entity signals.
  It can optionally convert the top candidates through `validate_worr_q2aas.py`
  and writes JSON/Markdown reports under `.tmp/q2aas/`.
- Added `tools/q2aas/test_discover_reference_candidates.py` for candidate
  scoring, invalid-BSP exclusion, conversion selection, and Markdown rendering.
- Promoted optional `q2dm7` into `tools/q2aas/validation_manifest.json` as a
  local id deathmatch slime reference. On this machine
  `.install\basew\maps\q2dm7.bsp` was staged from the local clean Quake II
  install and `q2aas-stage-aas` generated `.install\basew\maps\q2dm7.aas`.
- Tightened `tools/bot_scenarios/audit_movement_reference_gaps.py` so slime/lava
  AAS reference readiness no longer incorrectly promotes the existing runtime
  hazard scenario. Mode `96` still requires `trigger_hurt`, `target_laser`, or
  `misc_lavaball` entity evidence before it can lose its gap contract.

## Current Result

Fresh q2aas staged validation now validates nine local staged maps when `q2dm7`
is present. The feature gates now read:

- `slime_reference`: passed through `q2dm7`, with `48` slime brushes and mapped
  AAS slime area semantics.
- `crouch_reference`: still incomplete because no staged map generates
  `TRAVEL_CROUCH`.
- `lava_reference`: still incomplete because no promotable staged map is
  declared yet.

The broad local candidate scan over `E:\Games\q2Clean\baseq2\maps` inspected
`3951` BSP files and found `3888` valid Quake II BSPs. The strongest scratch
lava/runtime hazard candidate was `dark010.bsp`; a conversion-only proof passed
with `73` lava brushes, `73` `trigger_hurt` entities, `465` AAS lava areas,
`112` swim routes, and `216` water-jump routes. It was intentionally not added
to the manifest in this slice because it needs provenance/licensing review or a
better canonical replacement before staging.

The movement reference audit remains blocked:

- `natural_crouch`: `blocked_no_reference_content`.
- `hazard_context`: `blocked_no_runtime_hazard_content`, with a note that slime
  AAS coverage exists but runtime hurt/laser hazard entities are still missing
  from the accepted staged map set.

## Later Hazard Promotion Update

A later same-day slice used the discovery workflow on the official Quake II
campaign map `fact2`, then promoted it as the canonical lava/runtime hazard
reference instead of carrying forward the scratch `dark010` candidate. With
local `q2dm7` and `fact2` staged, q2aas now validates ten maps, and
`slime_reference`, `lava_reference`, and `runtime_hazard_entity_reference`
all pass. Mode `96` is now accepted as `movement_hazard_context` on `fact2`.

The movement reference audit now reports:

- `natural_crouch`: `blocked_no_reference_content`.
- `hazard_context`: `accepted`.

Implementation log:
`docs-dev/q3a-botlib-hazard-reference-promotion-2026-06-30.md`.

## Artifacts

- `.tmp\q2aas\reference-candidates.json`
- `.tmp\q2aas\reference-candidates.md`
- `.tmp\q2aas\reference-candidates-q2dm7.json`
- `.tmp\q2aas\reference-candidates-q2dm7.md`
- `.tmp\q2aas\reference-candidates-dark010.json`
- `.tmp\q2aas\reference-candidates-dark010.md`
- `.tmp\q2aas\reference-candidates-fact2.json`
- `.tmp\q2aas\reference-candidates-fact2.md`
- `.tmp\q2aas\validation-report.json`
- `.tmp\q2aas\stage-report.json`
- `.tmp\bot_scenarios\movement_reference_gap_audit.json`
- `.tmp\bot_scenarios\movement_reference_gap_audit.md`

## Validation

```powershell
python -m py_compile tools\q2aas\discover_reference_candidates.py tools\q2aas\test_discover_reference_candidates.py tools\q2aas\validate_worr_q2aas.py tools\bot_scenarios\audit_movement_reference_gaps.py
python -m unittest tools.bot_scenarios.test_audit_movement_reference_gaps tools.q2aas.test_discover_reference_candidates tools.q2aas.test_validate_worr_q2aas
python tools\q2aas\discover_reference_candidates.py --map .install\basew\maps\q2dm7.bsp --convert-top 1 --output .tmp\q2aas\reference-candidates-q2dm7 --json-out .tmp\q2aas\reference-candidates-q2dm7.json --markdown-out .tmp\q2aas\reference-candidates-q2dm7.md
python tools\q2aas\discover_reference_candidates.py --root E:\Games\q2Clean\baseq2\maps --output .tmp\q2aas\reference-candidates --json-out .tmp\q2aas\reference-candidates.json --markdown-out .tmp\q2aas\reference-candidates.md
python tools\q2aas\discover_reference_candidates.py --map E:\Games\q2Clean\baseq2\maps\dark010.bsp --convert-top 1 --output .tmp\q2aas\reference-candidates-dark010 --json-out .tmp\q2aas\reference-candidates-dark010.json --markdown-out .tmp\q2aas\reference-candidates-dark010.md
python tools\q2aas\discover_reference_candidates.py --map E:\Games\q2Clean\baseq2\maps\fact2.bsp --convert-top 1 --output .tmp\q2aas\reference-candidates-fact2 --json-out .tmp\q2aas\reference-candidates-fact2.json --markdown-out .tmp\q2aas\reference-candidates-fact2.md
meson compile -C builddir-win q2aas-staged-smoke
meson compile -C builddir-win q2aas-stage-aas
python tools\bot_scenarios\audit_movement_reference_gaps.py --q2aas-report .tmp\q2aas\validation-report.json --json-out .tmp\bot_scenarios\movement_reference_gap_audit.json --markdown-out .tmp\bot_scenarios\movement_reference_gap_audit.md
```

## Follow-Up

- Find or author a natural crouch map that produces `TRAVEL_CROUCH > 0`.
- Add runtime acceptance coverage for AAS slime/lava area semantics separately
  from entity-driven `trigger_hurt` hazard interaction.
