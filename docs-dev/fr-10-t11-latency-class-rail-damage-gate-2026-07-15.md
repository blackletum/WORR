# FR-10-T11 latency-class rail damage gate

Date: 2026-07-15  
Project task: `FR-10-T11` (depends on `FR-10-T10`)

## Outcome

The headless real-weapon fairness gate now covers three distinct authoritative
acknowledgement-age classes in addition to the invalid-current-frame negative
case. It invokes the production `fire_rail()`, `pierce_trace()`,
`LagCompensation_TraceLine()`, and current-authority `Damage()` path; it does
not inject a hit result, a target identity, or a rewind timestamp into that
path.

| Class | Selected source | Required applied time | Required result |
|---|---|---|---|
| Rejected | Current server frame | None | Current-world miss and no damage |
| Near | Recorded frame at or below half the configured rewind window | Exact recorded frame time | Historical rail hit |
| Bounded | Recorded frame above half and at or below the configured rewind window | Exact recorded frame time | Historical rail hit |
| Capped | Recorded frame older than the configured rewind window | `current_time - max_rewind` | Historical rail hit at the policy cap |

The fixture keeps the target's three historical poses identical, moves only
the target's current collider off the rail line, and therefore separates the
rewind-time decision from a changing aim point. Each valid rail call deals 10
points. A pass requires all three hits and exactly 30 current-authority damage;
the invalid current-frame call must leave health unchanged.

This remains a legacy-ack diagnostic. The server-console fixture has no real
client-command callback, so it deliberately exercises the normal non-bot
legacy acknowledgement branch rather than claiming canonical live-command
coverage. It strengthens the zero/low/high timing slice but does not close the
canonical-command, other-weapon, mover-damage, abuse/load, or release gates.

## Implementation

`LagCompensation_RunRailDamageRuntimeProbe()` selects eligible samples only
from the shooter's normally captured legacy-frame history and verifies an
alive, linked, damageable target pose at each selected time. The capped class
also proves that the selected sample was older than the max window and that the
recorded observation used the clamped time, not the original sample time.

The probe finds each observation by its immutable sequence range and semantic
content. This handles rail piercing correctly: a historical player hit can be
followed by the ordinary terminal trace miss without confusing the gate.
`sg_worr_rewind_rail_damage_selftest_status` is now schema
`worr.networking.rewind-rail-damage-runtime.v2`; its status includes the three
hit bits, three fractions, candidate count, and exact cumulative damage.

The runner invokes only `.install/worr_ded_x86_64.exe`, sets an isolated staged
working directory, redirects stdin to `DEVNULL`, and uses Windows no-window
creation flags. It neither launches a client nor initializes input or captures
the mouse. This is also now an explicit workspace-wide automated-test rule in
`AGENTS.md`.

## Acceptance result

Three independent dedicated-server repeats produced the identical status:

```text
pass:1:1:1:1:1:1:1:1:1:1:1:1:1:2:30:1000000:17574:17574:17574:0
```

All repeats reported two historical candidates, a full current-world miss,
three historical fractions of `0.017574`, exact 30 damage, failure code zero,
empty stderr, and identical stdout SHA-256 values.

## Validation

```powershell
python -m unittest tools/networking/test_run_rewind_rail_damage_runtime_gate.py tools/networking/test_lag_compensation_rail_damage_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/networking/run_rewind_rail_damage_runtime_gate.py --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/fr10-t11-rail-damage-v2.json --repeat 3 --timeout 30
```

The gate output is a temporary artifact under `.tmp/`, as required. No client
or interactive process was launched.

## Remaining scope

The parent task remains incomplete. Required next evidence includes canonical
live-command weapon damage, supported-weapon/mover/lifecycle matrices,
operator guidance, adversarial and load budgets, platform matrix, and rollout
promotion.
