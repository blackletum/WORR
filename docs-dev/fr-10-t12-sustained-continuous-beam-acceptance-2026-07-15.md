# FR-10-T12 sustained continuous-beam acceptance

Date: 2026-07-15  
Project tasks: `FR-10-T11`, `FR-10-T12`

## Outcome

The real-command acceptance gate now proves a bounded sustained held attack
for Plasma Beam policy `7` and dry-world Thunderbolt policy `8`. Three
headless, input-disabled two-client repeats per weapon retain one ordinary
`+attack` state, select a positive-age canonical historical target, and apply
exactly 256 current-authority damage across 32 normal 8-damage ticks. Every
terminal report has `sustained_hold_required=1` and
`sustained_hold_interrupted=0`.

This closes the bounded 32-tick continuous-beam cadence seam. It does not
claim indefinite holds, moving targets, multi-target interactions, underwater
radius fairness, abuse/load limits, platform matrices, or promotion readiness.

## Design

`run_canonical_rail_damage_runtime_gate.py` keeps client command finalization
on the independent physics cadence (`cl_async=1`) while preserving the
workspace launch policy: `win_headless=1`, `in_enable=0`, `in_grab=0`,
`s_enable=0`, hidden Windows processes, `stdin=DEVNULL`, and isolated runtime
homes. The sustained modes issue exactly one client-side `+attack`; unlike the
short held/release modes they do not refresh it with synthetic release/press
edges.

The acceptance fixture has two bounded stabilizers, neither of which supplies
combat authority:

- The shooter angle lock lasts six seconds, covering the five-second sustained
  deadline plus cadence tolerance. The received client command still supplies
  the held attack bit; normal `Item::weaponThink`, `Weapon_PlasmaBeam_Fire` /
  `Weapon_Thunderbolt_Fire`, and `fire_beams` / `fire_thunderbolt` own firing,
  tracing, and damage.
- After each normal damage frame the fixture restores only the target origin
  and velocity. It preserves health and all combat outcomes, preventing
  ordinary knockback from moving the controlled target outside the bounded
  acceptance ray before the next tick.

The common test budget is 33 Cells. Plasma Beam needs at least two Cells to
start each tick but consumes one; 33 therefore permits 32 ticks. The gate
passes immediately on tick 32, before Thunderbolt can consume the one spare
Cell.

## Evidence

Runtime schema `worr.networking.canonical-weapon-damage-runtime.v7` records
the sustained-required/interrupted terminal fields. Current staged reports:

- `.tmp/networking/canonical-plasma-beam-sustained-damage-runtime.json`
- `.tmp/networking/canonical-thunderbolt-sustained-damage-runtime.json`

For every repeat, the report records policy `7` or `8`, canonical scope,
historical target selection with a 56 ms applied age, unchanged current query
geometry, exact expected/observed 256 damage, and gate-owned termination of
both hidden clients and the dedicated server.

## Validation

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py tools/networking/test_run_canonical_rail_damage_runtime_gate.py tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-plasma-beam-sustained-damage-runtime.json --weapon plasma-beam-sustained --repeat 3 --port 28090 --timeout 35
python tools/networking/run_canonical_rail_damage_runtime_gate.py --client-exe .install/worr_x86_64.exe --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/canonical-thunderbolt-sustained-damage-runtime.json --weapon thunderbolt-sustained --repeat 3 --port 28089 --timeout 35
meson test -C builddir-win --print-errorlogs
```

Focused contracts passed 42/42 and the full Meson suite passed 137/137.
