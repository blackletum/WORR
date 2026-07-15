# FR-10-T11 canonical-command Super Shotgun damage acceptance seam

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T14`, and `FR-10-T15`.

## Purpose

This increment proves the normal two-barrel Super Shotgun callback from a real
client attack through the canonical rewind query and current-authority damage.
It keeps the established fixture boundary: the console only arms server-owned
geometry; it cannot construct command authority, choose a rewind instant,
supply a hit result, or invoke either weapon barrel.

## Production seam

The Super Shotgun arm selects `WORR_REWIND_WEAPON_SUPER_SHOTGUN` (policy `4`),
`IT_WEAPON_SSHOTGUN`, and exact expected damage of `120`. Normal
`Weapon_SuperShotgun_Fire` owns both `fire_shotgun` calls, each firing ten
six-damage pellets. The status therefore rejects partial single-barrel (60),
partial-pellet, or otherwise non-120 results.

The fixture selects a 64-unit historical target distance for this weapon only.
That is still an ordinary close player shot and preserves the normal
historical/current player geometry split, but keeps the standard ±5 degree
barrels and their pellet spread inside the target's normal player hull. It
does not enlarge collision, alter the weapon callback, or provide a trace
answer.

## Headless acceptance gate

`tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon
super-shotgun` arms `sv worr_rewind_canonical_super_shotgun_damage_arm` and
polls the dedicated status cvar. A dedicated server and two real UDP clients
run in isolated homes. Both clients use `win_headless=1`, `in_enable=0`,
`in_grab=0`, `s_enable=0`, `stdin=DEVNULL`, and Windows no-window creation.
Only loopback RCON follows admission; no automated launch initializes physical
input or can capture the mouse.

## Evidence

All three staged-runtime repetitions passed in
`.tmp/networking/canonical-super-shotgun-damage-runtime.json`. Every row
reports:

- valid canonical scope and a real received attack;
- policy `4`, a positive 56 ms applied age, canonical historical target hit,
  and no fallback;
- exactly 120 observed current-authority damage; and
- unchanged current target geometry, zero capture-append rejections, and
  95--96 ordinary target end-frame capture callbacks.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon super-shotgun --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-super-shotgun-damage-runtime.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Results: focused contracts 21/21; all three Super Shotgun repetitions passed;
the complete suite passed 137/137.

## Remaining scope

This completes the canonical-command Super Shotgun seam, not `FR-10-T11`.
Plasma-beam and thunderbolt command-to-damage scenarios; mover and lifecycle
matrices; fairness/abuse/load budgets; platform evidence; and release promotion
remain open.
