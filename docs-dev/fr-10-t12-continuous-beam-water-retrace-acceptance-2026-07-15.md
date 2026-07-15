# FR-10-T12 continuous-beam water-retrace acceptance

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T14`, and
`FR-10-T15`.

## Purpose

This increment validates the production water-crossing path for Plasma Beam
policy `7` and dry-shooter Thunderbolt policy `8`. A real admitted client fires
across the packaged `func_water` brush at a target whose current live position
is displaced from its retained historical position. Each weapon must cross
water, perform its ordinary water-excluding retrace, select the historical
target, and apply exactly four current-authority damage—the normal eight-damage
deathmatch tick halved by water.

The scope is deliberately a water crossing, not an underwater shooter. It does
not exercise the separate Thunderbolt self/radius discharge behavior.

## Production contract

Two fixture arms expose only setup and observation:

- `worr_rewind_canonical_plasma_beam_water_retrace_damage_arm`
- `worr_rewind_canonical_thunderbolt_water_retrace_damage_arm`

The fixture locates the map's real `func_water`, retains six ordinary target
poses, then places the shooter and historical target on opposite sides of the
water volume. It waits for a genuine active canonical `BUTTON_ATTACK` command;
it does not construct input, select a hit, trace, or call a weapon callback.

`fire_beams` and `fire_thunderbolt` remain the sole owners of the two trace
calls: the water-inclusive main trace and its water-excluding retrace. The
fixture requires positive-age canonical historical selection, no fallback,
unchanged current target geometry, and exact four damage. That exact result is
the production proof of the water branch: without a water hit the normal tick
is eight damage, while without the retrace the displaced target receives no
damage. Thunderbolt's mode additionally retains its normal production
main/side-ray target de-duplication.

The collision-observation journal does not guarantee an inline-water entity
reference for every trace result, so the terminal contract intentionally binds
to this stronger gameplay result rather than failing a correct weapon path on
optional diagnostic identity metadata.

The generated `worr_fr10_rewind_mover` map keeps water narrow in X/Y, away from
the mover and baseline player lanes, and extends it vertically through the
ordinary beam muzzle height. This makes water crossing deterministic without
putting the shooter or target inside water.

## Evidence

Three dedicated-server, two-client staged repetitions pass in each report:

- `.tmp/networking/canonical-plasma-beam-water-retrace-damage-runtime.json`
- `.tmp/networking/canonical-thunderbolt-water-retrace-damage-runtime.json`

Every row uses schema `worr.networking.canonical-weapon-damage-runtime.v4` and
reports policy `7` or `8`, positive 56 ms applied age, canonical historical
target hit, no fallback, unchanged current geometry, exact four damage, and
both `water_retrace_required=1` and `water_retrace_observed=1`. Each run
terminates its dedicated server and both hidden clients; no test process
remains afterward.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/networking/generate_rewind_collision_bsp_fixture.py `
  --output assets/maps/worr_fr10_rewind_mover.bsp --json
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon plasma-beam-water-retrace --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-plasma-beam-water-retrace-damage-runtime.json `
  --repeat 3 --timeout 35
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon thunderbolt-water-retrace --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-thunderbolt-water-retrace-damage-runtime.json `
  --repeat 3 --timeout 35
```

All launches are dedicated or `win_headless=1`, use isolated runtime homes,
`in_enable=0`, `in_grab=0`, `s_enable=0`, `stdin=DEVNULL`, and Windows
no-window creation. Automated test clients neither initialize input nor capture
the mouse.

Results: focused contracts 36/36; three Plasma Beam and three Thunderbolt
water-retrace repetitions passed; the full Meson suite passed 137/137.

## Remaining scope

`FR-10-T11` and `FR-10-T12` remain incomplete. Long held-beam behavior,
underwater Thunderbolt discharge, mover/lifecycle matrices, projectile
simulation, melee, splash/radius, deployables, triggers, cooperative
interactions, fairness/abuse/load budgets, platform evidence, and release
promotion remain open.
