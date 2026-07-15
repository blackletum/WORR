# FR-10-T11 canonical-command machinegun-damage acceptance seam

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T14`, and `FR-10-T15`.

## Purpose

The canonical Railgun gate proved a single piercing weapon path. This increment
extends that proof to the machinegun's ordinary bullet path without accepting a
client hit result, a client-selected rewind instant, or a fixture-invoked
weapon/trace callback.

## Production seam

The isolated real-command fixture is now configured at arming time with an
explicit weapon-policy tag, item, expected damage, and normal idle gun frame.
`LagCompensation_PrepareCanonicalWeaponDamageCommand` remains at the beginning
of `ClientThink`, before command button latching. It only activates after an
active valid command scope and a received `BUTTON_ATTACK` bit. It sets only
fixture-owned server state; ordinary `Item::weaponThink` remains the sole
weapon dispatch path.

The machinegun arm command selects:

- `WORR_REWIND_WEAPON_MACHINEGUN` (policy `1`);
- `IT_WEAPON_MACHINEGUN` with ordinary bullets; and
- exact expected current-authority damage of `8`.

The shared status record now includes the observed weapon policy, configured
expected damage, and measured target-health damage. The runtime gate rejects a
pass unless all three values agree, in addition to canonical scope, positive
age, historical target identity, and unchanged current-world geometry.

The common fixture separates shooter and target by 112 units. This keeps the
bullet-family test inside `P_ProjectSource`'s close-target branch, ensuring
the standard muzzle-projection and subsequent bullet trace use the same
ordinary forward line. It does not alter the received command, generate a
trace result, or invoke weapon code directly.

## Headless acceptance gate

`tools/networking/run_canonical_rail_damage_runtime_gate.py` now accepts a
`--weapon` mode. `--weapon machinegun` arms
`sv worr_rewind_canonical_machinegun_damage_arm` and polls the machinegun
status cvar. `--weapon railgun` preserves the Railgun mode against the same
generic fixture.

The gate starts a dedicated server and two real UDP clients in isolated runtime
homes. Each client starts with `win_headless=1`, `in_enable=0`, `in_grab=0`,
`s_enable=0`, no stdin, and Windows no-window creation. All post-start
interaction is loopback RCON; only the admitted shooter receives `+attack`.
No automated launch initializes client input or captures the mouse.

## Evidence

Three independent machinegun repeats passed from the staged runtime. Every
row reported:

- `status=pass` and `failure_code=0`;
- a valid canonical scope and a real received attack;
- policy `1`, a canonical historical target hit with no fallback, and a
  positive applied age of 56 ms;
- exactly 8 observed damage against the current authoritative target; and
- unchanged live target geometry and zero capture-append rejections.

The machine-readable temporary evidence is
`.tmp/networking/canonical-machinegun-damage-runtime.json`. A three-repeat
Railgun regression from the same shared fixture also passed with policy `5`
and exact 80 damage in `.tmp/networking/canonical-rail-damage-runtime-v2.json`.

## Verification

```powershell
python -m unittest tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon machinegun --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-machinegun-damage-runtime.json `
  --repeat 3 --timeout 35
```

Final verification also passed:

```powershell
meson test -C builddir-win --no-rebuild
```

Result: 137/137 tests passed.

## Remaining scope

This completes the canonical-command machinegun acceptance seam, not
`FR-10-T11`. Plasma-beam and thunderbolt
command-to-damage scenarios; mover and lifecycle matrices;
fairness/abuse/load budgets; platform evidence; and release promotion remain
open.
