# Q3A BotLib Movement Reference Gap Audit

Date: 2026-06-30

Tasks: `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Scope

This slice closes the next M6 roadmap ambiguity: the remaining natural crouch
and hazard movement rows were expected-blocked, but the tooling did not have a
single repeatable answer for whether the staged reference maps were ready to
promote them.

The implementation adds that answer without inventing fake map coverage.

## Implementation

- `tools/q2aas/validate_worr_q2aas.py` now treats `crouch` as a first-class
  reference feature, backed by generated `TRAVEL_CROUCH` counts.
- `tools/q2aas/validation_manifest.json` now declares `crouch_reference` beside
  the existing `slime_reference` and `lava_reference` feature gates, with the
  pending reference map list updated for natural crouch reachability.
- `tools/bot_scenarios/audit_movement_reference_gaps.py` reads the q2aas staged
  validation report and the current bot scenario catalog, then reports whether
  `movement_crouch_gap` and `movement_hazard_context_gap` are blocked,
  promotable, or accepted.
- `tools/bot_scenarios/test_audit_movement_reference_gaps.py` covers the current
  blocked state, future crouch-reference promotion readiness, future runtime
  hazard-entity readiness, slime/lava feature readiness, and the accepted state
  once rows are no longer gap-tagged.

## Initial Result

The first q2aas staged validation pass still passed, but reported three explicit
reference-feature gaps:

- `crouch_reference`: no candidate map declared and all eight staged maps report
  `TRAVEL_CROUCH = 0`.
- `slime_reference`: no candidate map declared.
- `lava_reference`: no candidate map declared.

The movement reference audit is therefore blocked:

- `natural_crouch` is blocked because `crouch_reference` does not pass and no
  staged map reports generated crouch reachability.
- `hazard_context` is blocked because neither slime nor lava reference passes
  and no staged map reports `trigger_hurt`, `target_laser`, or `misc_lavaball`
  runtime hazard entities.

## Follow-Up Candidate Discovery Result

A later 2026-06-30 candidate-discovery slice added
`tools/q2aas/discover_reference_candidates.py` and promoted optional `q2dm7`
as the first staged slime reference. With local `q2dm7.bsp` staged,
`q2aas-staged-smoke` now validates nine maps and `slime_reference` passes with
`48` slime brushes and mapped AAS slime semantics.

The audit remains blocked, but for narrower reasons:

- `natural_crouch` remains `blocked_no_reference_content` because no staged map
  reports generated `TRAVEL_CROUCH` reachability.
- `hazard_context` is now `blocked_no_runtime_hazard_content`: slime/lava AAS
  reference coverage exists, but the existing mode `96` scenario still needs
  a promotable staged map with `trigger_hurt`, `target_laser`, or
  `misc_lavaball` runtime hazard entities.

Implementation log:
`docs-dev/q3a-botlib-reference-candidate-discovery-2026-06-30.md`.

## Hazard Reference Promotion Result

A later same-day hazard-reference promotion staged the official Quake II
campaign map `fact2` as the lava/runtime hazard reference. With local `q2dm7`
and `fact2` BSPs staged, `q2aas-staged-smoke` now validates ten maps,
`slime_reference`, `lava_reference`, and `runtime_hazard_entity_reference`
pass, and the bot scenario catalog promotes mode `96` to accepted
`movement_hazard_context` on `fact2`.

The movement reference audit now reports:

- `natural_crouch`: `blocked_no_reference_content` because no staged map
  reports generated `TRAVEL_CROUCH` reachability.
- `hazard_context`: `accepted`.

Implementation log:
`docs-dev/q3a-botlib-hazard-reference-promotion-2026-06-30.md`.

## Artifacts

- `.tmp\q2aas\validation-report.json`
- `.tmp\bot_scenarios\movement_reference_gap_audit.json`
- `.tmp\bot_scenarios\movement_reference_gap_audit.md`

## Validation

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py tools\bot_scenarios\audit_movement_reference_gaps.py tools\bot_scenarios\test_audit_movement_reference_gaps.py
python -m unittest tools.q2aas.test_validate_worr_q2aas tools.bot_scenarios.test_audit_movement_reference_gaps
meson compile -C builddir-win q2aas-staged-smoke
python tools\bot_scenarios\audit_movement_reference_gaps.py --q2aas-report .tmp\q2aas\validation-report.json --json-out .tmp\bot_scenarios\movement_reference_gap_audit.json --markdown-out .tmp\bot_scenarios\movement_reference_gap_audit.md --format text
```

## Follow-Up

The remaining accepted movement promotion needs real crouch reference content
first:

- stage or generate a map that produces `TRAVEL_CROUCH > 0` and add it to
  `crouch_reference`;
- rerun `q2aas-staged-smoke`;
- rerun the movement reference gap audit;
- only then replace the expected-blocked crouch scenario contract with accepted
  traversal proof.
