# Q3A BotLib scenario pending gap report

Date: 2026-06-18

Task: DV-03-T05

## Summary

This round kept the scenario harness placeholders pending because the current
source-backed smoke report does not contain rows for `engage_enemy`,
`switch_weapons`, `health_armor_pickup`, or `team_objective`. Instead, the
harness now makes that source counter gap explicit.

`tools/bot_scenarios/run_bot_scenarios.py` now records planned smoke modes and
promotion-required status metrics for the four pending scenarios. It also adds a
lightweight `--pending-gap-report <report.json>` mode. The mode reads one
existing harness JSON report, does not launch the game, and reports whether each
pending scenario is ready for promotion or still blocked by missing fixture rows,
wrong smoke modes, pending fixture rows, or absent status/marker metrics.

## Pending promotion contracts

The pending catalog rows now expose these planned source-backed smoke modes:

| Scenario | Planned mode | Primary missing source proof |
| --- | ---: | --- |
| `engage_enemy` | `20` | Enemy acquisition, attack-button, fire, and damage metrics. |
| `switch_weapons` | `21` | Weapon-switch decision, request, completion, and expected-match metrics. |
| `health_armor_pickup` | `22` | Low-health/low-armor scoring, pickup completion, and pickup delta metrics. |
| `team_objective` | `23` | Objective assignment, route-command, reach, and flag-pickup metrics. |

The current `.tmp/bot_scenarios/latest_report.json` fixture still contains only
the four implemented navigation/reservation/map-repeat scenarios, so the new gap
report correctly marks all four pending rows as blocked.

A read-only source check also found no dedicated mode `20` through `23` pending
scenario implementation. Current bot action/combat/item counters are emitted on
`q3a_bot_action_status`, but there is not yet a source-backed scenario row or the
completion counters needed to promote any of the four placeholders.

## Usage

Generate a pending gap report from the latest real scenario report:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --format text --json-out .tmp\bot_scenarios\pending_gap_report.json --markdown-out .tmp\bot_scenarios\pending_gap_report.md
```

This command is safe for quick local checks because it exits before dedicated
server binary validation and never launches a game process.

## Validation

Command:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed.

Command:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed, `8` tests run. Coverage includes pending promotion catalog
fields, missing-row gap reporting, synthetic ready-state gap reporting, Markdown
gap output, and optional real fixture gap validation.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --scenario pending --format text --json-out .tmp\bot_scenarios\pending_catalog_with_promotions.json
```

Result: passed. The pending catalog reports planned modes `20` through `23` and
promotion-required status metrics for each pending scenario.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --format text --json-out .tmp\bot_scenarios\pending_gap_report.json --markdown-out .tmp\bot_scenarios\pending_gap_report.md
```

Result: passed. Summary was `0` ready, `4` blocked, `4` missing rows, and `42`
missing status metrics.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28100 --format text --json-out .tmp\bot_scenarios\implemented_after_pending_gap_report.json
```

Result: passed. All four implemented scenarios passed:
`spawn_route_to_item`, `recover_from_stall`, `multi_bot_reservation`, and
`map_change_repeat`.

## Residual risks

- This is not a source-side promotion. The four pending scenarios still need
  dedicated smoke modes and status counters before they can become implemented
  harness scenarios.
- Some action/combat/item decision counters already exist on
  `q3a_bot_action_status`. Promotion still needs deterministic smoke setup and
  completion counters such as damage, weapon switch completion, pickup deltas, or
  objective pickup.
- The gap report checks metric presence, not semantic pass criteria, for future
  source-backed rows. Once a pending scenario is promoted, normal `MetricCheck`
  pass/fail gates should enforce exact values.
- `team_objective` still depends on deterministic CTF objective setup and either
  source-managed map transition or future scenario-level map selection support.
