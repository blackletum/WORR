# Q3A BotLib Hazard Context Gap Round - 2026-06-28

Task IDs: `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`

## Summary

This round adds the first dedicated hazard-context scenario row and tightens
runtime hazard classification. The staged reference-map set still has no
slime/lava brushes or `trigger_hurt` entities, so the row is intentionally a
gap proof rather than an accepted hazard traversal proof.

## Implementation

- Added reserved bot frame-command smoke mode `96` for
  `movement_hazard_context_gap`.
- Mode `96` runs one bot on packaged map `base2`, emits the normal reserved
  scenario begin marker, and validates route command health plus runtime
  interaction context.
- Added `movement_hazard_context_gap` to the scenario harness. The row checks
  that `base2` exposes live interaction context, reports trigger-rich map
  entities, and explicitly records `interaction_world_hazards=0` for the
  current packaged content gap.
- Broadened runtime hazard classification in `bot_nav.cpp` and
  `bot_runtime.cpp` so `target_laser` and `misc_lavaball` are counted beside
  `trigger_hurt`, `trigger_lava`, and `trigger_slime`.
- Extended `tools/bot_scenarios/test_run_bot_scenarios.py` fixtures and catalog
  tests for mode `96`.

## Evidence

- `python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -q`
  passed: `55 passed`.
- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64`
  passed.
- `python tools\refresh_install.py --build-dir builddir-win --platform-id windows-x86_64 --package-q2aas-aas`
  passed and refreshed `.install/`.
- Focused `movement_hazard_context_gap` validation passed from
  `.tmp\bot_scenarios\movement_hazard_context_gap\20260628T083930Z`.
- Full implemented scenario validation passed from
  `.tmp\bot_scenarios\implemented_hazard_context\20260628T083945Z` with
  `114` passed rows, `0` failed rows, `0` timeouts, `0` errors, and
  `0` pending rows.

## Remaining Gap

The current packaged map set still cannot prove accepted hazard traversal or
avoidance through real slime/lava or hurt-trigger geometry. The next hazard
promotion needs a staged BSP/AAS pair with one or more of:

- nonzero slime/lava brush contents;
- a live `trigger_hurt`, `target_laser`, or equivalent hazard entity;
- routeable AAS context adjacent to the hazard so bots can prove avoidance,
  detour, or enviro-suit/teleporter escape behavior.

Until then, mode `96` keeps the absence visible in the automated catalog.
