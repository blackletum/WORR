# FR-10-T11 canonical-command Disruptor damage acceptance seam

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T12`, `FR-10-T14`, and
`FR-10-T15`.

## Purpose

The Disruptor is a convergence-selected homing projectile rather than an
instant hitscan damage callback. This increment proves the entire real-command
path: canonical historical convergence chooses the target, normal
`Weapon_Disruptor_Fire` spawns the projectile, ordinary live projectile flight
reaches that selected target, and its normal 500 ms pain daemon applies the
intended current-authority damage.

## Production seam

The new arm selects `WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE` (policy `6`),
`IT_WEAPON_DISRUPTOR`, and exact expected damage of `45` in deathmatch. The
fixture neither supplies the convergence result nor calls `fire_disruptor`.
It only waits up to 1.5 seconds after the real command before finalizing the
status, allowing normal projectile travel and delayed damage processing to
complete. Immediate weapons retain their zero-delay terminal policy.

This is an explicit hybrid boundary: historical pose data selects a valid
authoritative target at command time, while the projectile and its delayed
damage remain live-world authority. The terminal result still requires the
same command identity, canonical historical hit, no fallback, and unchanged
live geometry at query time.

## Headless acceptance gate

`tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon
disruptor` arms `sv worr_rewind_canonical_disruptor_damage_arm` and polls the
Disruptor status cvar. The report schema is now
`worr.networking.canonical-weapon-damage-runtime.v3`, accurately covering both
instant and delayed command-to-damage paths.

The server runs dedicated and both real UDP clients use isolated homes,
`win_headless=1`, `in_enable=0`, `in_grab=0`, `s_enable=0`, `stdin=DEVNULL`,
and Windows no-window creation. Loopback RCON is the only post-start control;
no automated client initializes input or captures the mouse.

## Evidence

`.tmp/networking/canonical-disruptor-damage-runtime-v3.json` contains three
successful staged-runtime repetitions. Every row reports policy `6`, exact 45
damage, a 56 ms positive applied rewind age, canonical historical target hit,
no fallback, unchanged current geometry, zero capture-append rejections, and
ordinary target end-frame callbacks during the delayed lifecycle.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon disruptor --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-disruptor-damage-runtime-v3.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Results: focused contracts 23/23; all three staged Disruptor repetitions
passed; complete suite 137/137.

## Remaining scope

This completes the canonical-command Disruptor convergence-and-damage seam,
not `FR-10-T11` or `FR-10-T12`. Plasma-beam and thunderbolt command-to-damage
scenarios; mover and lifecycle matrices; fairness/abuse/load budgets; platform
evidence; and release promotion remain open.
