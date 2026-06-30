# Q3A BotLib Crouch Reference Promotion

Date: 2026-06-30

Tasks: `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This slice promotes natural crouch from an explicit movement-reference gap to an accepted map-backed route proof. The previous diagnostic row proved that the scenario harness could surface a missing `TRAVEL_CROUCH` reference, but q2aas was still labeling equal-floor and small-step crouch transitions as `TRAVEL_WALK`. The generator now preserves crouch travel type when either side of the reachability is crouch-only, and the runtime AAS movement smoke accepts crouch presence when no normal presence floor origin exists.

## Implementation

- Added `tools/q2aas/reference_maps/worr_crouch_ref.map` and its compiled `worr_crouch_ref.bsp` as a WORR-owned developer reference map with two normal-height rooms connected by a crouch-only passage.
- Updated `tools/q2aas/deps/botlib/be_aas_reach.c` so equal-floor, step-up, and small step-down reachabilities become `TRAVEL_CROUCH` when either connected area is crouch-only.
- Added `worr_crouch_ref` to `tools/q2aas/validation_manifest.json` as a required `crouch_reference` row with minimum area, reachability, cluster, and crouch travel-count gates.
- Updated `tools/stage_install.py` and package tests so q2aas reference BSPs are copied into `.install/<base_game>/maps` during install refreshes.
- Promoted scenario mode `92` from `movement_crouch_gap` to `movement_crouch_route` on `worr_crouch_ref`, requiring natural crouch support, a supported `TRAVEL_CROUCH` goal, crouch movement-state commands, and clean route output.
- Updated the movement reference gap audit so `natural_crouch` is accepted when `movement_crouch_route` is present and the q2aas `crouch_reference` gate passes.
- Made the Q3A runtime AAS movement smoke search normal presence first and crouch presence second, then pass the selected presence type into `AAS_OnGround()` and `AAS_PredictClientMovement()`.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --scenario movement_crouch_route --artifact-dir .tmp\bot_scenarios\movement_crouch_route --json-out .tmp\bot_scenarios\movement_crouch_route.json --markdown-out .tmp\bot_scenarios\movement_crouch_route.md --format both --timeout 60 --base-port 28120`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_scenarios.test_audit_movement_reference_gaps tools.q2aas.test_validate_worr_q2aas tools.q2aas.test_discover_reference_candidates`
- `python tools\test_package_assets.py`
- `meson compile -C builddir-win q2aas-staged-smoke`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`

Focused mode `92` now passes with `commands=60`, `route_commands=60`, `route_failures=0`, `last_reachability_type=3`, and `movement_state_crouch_commands>=1`. The movement reference gap audit reports both `natural_crouch` and `hazard_context` as `accepted`, with no remaining blockers.

## Notes

No new upstream source snapshot was imported. The q2aas reachability change modifies the existing vendored BSPC/BotLib file already tracked in the credits ledger; the reference map is WORR-authored developer validation content and is staged only as a reference BSP for AAS generation and scenario proof.
