# FR-10-T12 Thunderbolt underwater-discharge acceptance

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T14`, and
`FR-10-T15`.

## Purpose

This increment verifies the bounded underwater branch of the production
Thunderbolt. It is deliberately a current-authority radius/self-damage policy,
not a historical-hitscan claim: `fire_thunderbolt` detects underwater muzzle
contents and returns before its main/side beam traces. A real client command
must therefore reach ordinary weapon dispatch, drain the loaded cells, and
apply the exact production self damage without inventing a rewind observation
that the weapon never makes.

## Production contract

`worr_rewind_canonical_thunderbolt_discharge_damage_arm` selects two admitted
real clients, retains six ordinary target poses, and waits for an active
canonical `BUTTON_ATTACK` command. The fixture supplies no input, trace,
damage result, or direct weapon call. It only arranges the isolated
server-owned fixture state: the shooter starts inside the map's bounded
world-water leaf, has eight Cells, and the unrelated target is kept outside
the discharge radius.

The deterministic collision map retains its narrow inline `func_water` for
the water-retrace modes and now also has a bounded world-water leaf and brush.
The latter is necessary because `gi.pointContents(start)`—the production
underwater test—uses world contents rather than an inline entity trace. The
native real-BSP parity probe still passes its ten geometric cases and four
transactional identity rejections with generated map SHA-256
`bdc1a88bd7c83ddc7e52bd674856594113b2f09e798d2401522c06b33d404d53`.

On the real branch, `fire_thunderbolt` consumes all Cells, computes
`35 * 8 = 280` discharge damage, excludes the shooter from `RadiusDamage`,
then calls `Damage` with its intended 140 self-damage value. The shared damage
path applies its normal self-damage reduction, yielding exactly 70 lost health.
The acceptance status therefore requires all of the following:

- active canonical command scope and a received real attack;
- the explicit observer called after the real production branch's authority
  damage (a no-op when the console-only fixture is not armed);
- all eight Cells drained; and
- exactly 70 current-authority self damage.

The report is schema
`worr.networking.canonical-weapon-damage-runtime.v6`. It explicitly records
`thunderbolt_discharge_required`, `thunderbolt_discharge_ammo_drained`, and
`thunderbolt_discharge_observed`. It intentionally does not require a positive
rewind age, historical target hit, or unchanged target geometry: production
discharge returns before `LagCompensation_TraceLine`, so those would be a false
contract for this current-authority branch.

## Evidence

`.tmp/networking/canonical-thunderbolt-discharge-damage-runtime.json` records
three independent staged repetitions. Each reports policy `8`, `pass`, exact
expected and observed damage `70`, drained Cells, and observed production
discharge. Each dedicated server and both hidden clients were terminated by the
gate; no `worr_x86_64` or `worr_ded_x86_64` test process remained.

The focused source/parser contracts pass 38/38 and the Windows Meson suite
passes 137/137. `ninja -C builddir-win sgame_x86_64.dll` succeeds and the
refreshed `.install` stage validates.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/networking/generate_rewind_collision_bsp_fixture.py `
  --output assets/maps/worr_fr10_rewind_mover.bsp --json
ninja -C builddir-win rewind_collision_real_bsp_test.exe
python tools/networking/run_rewind_collision_real_bsp_test.py `
  --probe builddir-win/rewind_collision_real_bsp_test.exe --repo-root .
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon thunderbolt-discharge --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-thunderbolt-discharge-damage-runtime.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Every launched client uses `win_headless=1`, `in_enable=0`, `in_grab=0`,
`s_enable=0`, an isolated runtime home, `stdin=DEVNULL`, and Windows no-window
creation. The server uses the dedicated executable. Automated tests neither
initialize physical input nor capture the mouse.

## Remaining scope

This closes only the bounded eight-Cell underwater self-discharge seam. It
does not establish general splash/radius multi-target fairness, underwater
beam target interactions, long-held beam behavior, mover/lifecycle matrices,
projectile simulation, melee, deployables, triggers, cooperative interactions,
abuse/load budgets, platform evidence, or release promotion. `FR-10-T11` and
`FR-10-T12` remain incomplete.
