# FR-10-T12 held continuous-beam cadence acceptance

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T14`, and
`FR-10-T15`.

## Purpose

The earlier real-command beam seams proved one first tick each. This increment
extends that evidence to a bounded three-tick held-command cadence for both
continuous weapons: Plasma Beam policy `7` and dry-world Thunderbolt policy
`8`. It proves that repeated real client attack commands retain normal weapon
dispatch, canonical historical queries, and current-authority damage across
the next two ordinary ticks.

This is bounded cadence evidence, not a claim that the whole continuous-beam
Definition of Done is complete. The following bounded release seam is covered
separately; long-duration behavior, water retrace, Thunderbolt underwater
discharge, and broader interaction policy matrices remain open.

## Production seam

The shared real-command fixture adds two arms:

- `worr_rewind_canonical_plasma_beam_held_damage_arm` requires exact 24 damage
  from three normal 8-damage Plasma Beam ticks.
- `worr_rewind_canonical_thunderbolt_held_damage_arm` requires exact 24 damage
  from three normal 8-damage, target-de-duplicated Thunderbolt footprints.

Both arms keep a distinct current target on the shooter’s aim ray. The first
command still proves its positive-age canonical historical hit against the
retained prior pose; the on-ray current placement lets later real commands
correctly query their newer history instead of missing the deliberately
off-axis one-shot fixture geometry. Fixture code supplies no trace, target
selection, or damage result.

The status stays pending after one or two ticks and fails closed if exact
cumulative damage has not arrived within 1.5 seconds. Delayed projectile
fixtures retain their separate deadline-only evaluation policy.

## Headless held-command delivery

The runner starts with a normal `+attack` command on the admitted hidden
shooter. While a cadence fixture is pending, it sends `-attack; +attack` only
to that same real client, creating a fresh client-side attack edge and ordinary
`BUTTON_ATTACK` user command for each refresh. It never writes server-side
input, manufactures command authority, or invokes a weapon callback.

The dedicated server and both clients use isolated homes, `win_headless=1`,
`in_enable=0`, `in_grab=0`, `s_enable=0`, `stdin=DEVNULL`, and Windows
no-window creation. No automated client initializes input or captures the
mouse.

## Evidence

Three staged-runtime repetitions each pass in:

- `.tmp/networking/canonical-plasma-beam-held-damage-runtime.json`
- `.tmp/networking/canonical-thunderbolt-held-damage-runtime.json`

Every row reports schema `worr.networking.canonical-weapon-damage-runtime.v3`,
its expected policy, exact 24 damage, a positive 56 ms applied rewind age,
canonical historical target hit, no fallback, unchanged current geometry, and
zero capture-append rejections. Thunderbolt’s 24-damage contract also rejects
any spurious multi-ray accumulation beyond one 8-damage result per tick.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon plasma-beam-held --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-plasma-beam-held-damage-runtime.json `
  --repeat 3 --timeout 35
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon thunderbolt-held --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-thunderbolt-held-damage-runtime.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Results: focused contracts 31/31; three Plasma Beam and three Thunderbolt
held-cadence repetitions passed; complete suite 137/137.

## Remaining scope

`FR-10-T11` and `FR-10-T12` remain incomplete. The evidence does not cover
long-running held attacks, water/retrace, Thunderbolt discharge,
projectile simulation, melee, splash/radius, movers, deployables, triggers,
cooperative interactions, fairness/abuse/load budgets, platform evidence, or
release promotion.
