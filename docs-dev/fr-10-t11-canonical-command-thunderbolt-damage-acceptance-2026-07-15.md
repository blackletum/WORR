# FR-10-T11/T12 canonical-command Thunderbolt first-tick acceptance seam

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T14`, and
`FR-10-T15`.

## Purpose

The Thunderbolt resolves a complete main-and-side-ray lightning footprint in a
single repeating weapon tick. This increment proves the first dry-world tick
from a genuine held canonical attack command: normal `Weapon_Thunderbolt`
dispatch enters `fire_thunderbolt`, the complete footprint queries historical
player geometry, and normal target de-duplication applies exactly one
current-authority damage result.

This is not a broad continuous-lightning completion claim. A bounded
three-tick dry-world cadence and bounded release are now covered separately;
underwater discharge, water-retrace behavior, longer held duration, and the
remaining beam and interaction matrices remain separate `FR-10-T12` work.

## Production seam

The shared real-command fixture adds
`worr_rewind_canonical_thunderbolt_damage_arm`, selecting
`WORR_REWIND_WEAPON_THUNDERBOLT` (policy `8`), `IT_WEAPON_THUNDERBOLT`, and
the ordinary deathmatch first-tick damage of `8`. It stages only isolated
fixture player state. The received held attack still drives normal
`Item::weaponThink`, `Weapon_Thunderbolt_Fire`, `P_ProjectSource`, and
`fire_thunderbolt`; no fixture code supplies rays, chooses a hit, or applies
damage.

`fire_thunderbolt` owns all main/side-ray traces before any damage mutation and
de-duplicates a target across that footprint. The contract therefore requires
exactly 8 damage, rejecting a synthetic 16/24-style multi-ray accumulation as
well as a missed tick. It also requires the received command identity, policy
`8`, a positive-age canonical historical target hit, no fallback, and
unchanged current target geometry at query time.

## Headless acceptance gate

`tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon
thunderbolt` runs a dedicated server and two real loopback UDP clients. Every
automated process uses an isolated home, `win_headless=1`, `in_enable=0`,
`in_grab=0`, `s_enable=0`, `stdin=DEVNULL`, and Windows no-window creation.
No automated client initializes input or captures the mouse.

## Evidence

`.tmp/networking/canonical-thunderbolt-damage-runtime.json` records three
successful staged-runtime repetitions using
`worr.networking.canonical-weapon-damage-runtime.v3`. Every row reports policy
`8`, exact 8 damage, a positive 56 ms applied rewind age, canonical historical
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
  --weapon thunderbolt --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-thunderbolt-damage-runtime.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Results: focused contracts 27/27; all three staged Thunderbolt repetitions
passed; complete suite 137/137.

## Remaining scope

This closes only the dry-world first real-command Thunderbolt tick seam. The
bounded three-tick cadence and release are documented separately. `FR-10-T11`
and `FR-10-T12` remain incomplete for underwater discharge, water-retrace,
longer held coverage, mover and lifecycle matrices, fairness/abuse/load
budgets, platform evidence, and release promotion.
