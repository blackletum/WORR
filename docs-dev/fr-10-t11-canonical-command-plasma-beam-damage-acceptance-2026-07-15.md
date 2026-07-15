# FR-10-T11/T12 canonical-command Plasma Beam first-tick acceptance seam

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T14`, and
`FR-10-T15`.

## Purpose

The Plasma Beam is a repeating continuous weapon, not a one-shot hitscan
weapon. This increment proves the first normal deathmatch beam tick produced
by a genuine held canonical attack command: historical query selection occurs
inside the normal `Weapon_PlasmaBeam`/`fire_beams` route and damage remains on
the current authoritative target.

It deliberately does not present one command as proof of a full continuous
beam lifecycle. A bounded three-tick cadence and bounded release are now
covered separately; longer held duration, water retrace, and the wider beam
interaction matrix remain separate work under `FR-10-T12`.

## Production seam

The shared real-command fixture adds
`worr_rewind_canonical_plasma_beam_damage_arm`, selecting
`WORR_REWIND_WEAPON_PLASMA_BEAM` (policy `7`), `IT_WEAPON_PLASMABEAM`, and
the ordinary deathmatch first-tick damage of `8`. It stages only isolated
fixture player state. The received attack remains held, and normal
`Item::weaponThink` enters the repeating Plasma Beam state; it alone calls
`Weapon_PlasmaBeam_Fire`, `P_ProjectSource`, and `fire_plasmabeam`/
`fire_beams`.

The terminal proof requires the received command identity, policy `7`, a
positive-age canonical historical target hit, no fallback, unchanged current
target geometry at query time, and exact current-authority 8 damage. It does
not inject a trace, tick result, or damage result.

## Headless acceptance gate

`tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon
plasma-beam` runs a dedicated server and two real loopback UDP clients. All
automated processes use isolated homes, `win_headless=1`, `in_enable=0`,
`in_grab=0`, `s_enable=0`, `stdin=DEVNULL`, and Windows no-window creation.
No automated client initializes input or captures the mouse.

## Evidence

`.tmp/networking/canonical-plasma-beam-damage-runtime.json` records three
successful staged-runtime repetitions using
`worr.networking.canonical-weapon-damage-runtime.v3`. Each reports policy
`7`, exact 8 damage, a positive 56 ms applied rewind age, canonical historical
target hit, no fallback, unchanged current geometry, zero capture-append
rejections, and normal target end-frame history callbacks.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon plasma-beam --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-plasma-beam-damage-runtime.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Results: focused contracts 25/25; all three staged Plasma Beam repetitions
passed; complete suite 137/137.

## Remaining scope

This closes the first real-command Plasma Beam tick seam only. The bounded
three-tick cadence and bounded release are documented separately. `FR-10-T11`
and `FR-10-T12` remain incomplete for longer sustained/water coverage, mover and
lifecycle matrices, fairness/abuse/load budgets, platform evidence, and
release promotion.
