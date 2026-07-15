# FR-10-T11 canonical-command shotgun-damage acceptance seam

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T14`, and `FR-10-T15`.

## Purpose

This increment extends the real-command, two-client rewind fixture to the
ordinary shotgun callback. It proves the multi-pellet weapon path consumes a
real client attack within a canonical command scope, rewinds only the query,
and applies the complete current-authority damage to the historical target.
It does not accept a client hit result or let the fixture choose a trace,
rewind time, or weapon callback.

## Production seam

`LagCompensation_PrepareCanonicalWeaponDamageCommand` continues to run at the
beginning of `ClientThink`, before button latching. Once it sees an active
valid canonical command and a received `BUTTON_ATTACK`, the armed fixture
configures only its isolated server-owned state. Normal `Item::weaponThink`
then dispatches the standard weapon code.

The new shotgun arm command selects:

- `WORR_REWIND_WEAPON_SHOTGUN` (policy `3`);
- `IT_WEAPON_SHOTGUN`; and
- an exact expected target-health change of `48` (twelve pellets at four
  damage each).

The fixture keeps the target 112 units from the shooter. That keeps the
ordinary shotgun muzzle projection inside the close-target branch used by the
bullet family, while preserving the normal `fire_shotgun` pellet loop. The
status record requires the observed policy, expected damage, and measured
damage to agree, so a partial pellet result (for example 44 rather than 48)
cannot pass.

## Headless acceptance gate

`tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon shotgun`
arms `sv worr_rewind_canonical_shotgun_damage_arm` and polls the shotgun
status cvar. It starts a dedicated server and two real loopback UDP clients in
separate runtime homes. Both clients use `win_headless=1`, `in_enable=0`,
`in_grab=0`, `s_enable=0`, `stdin=DEVNULL`, and Windows no-window creation.
The only post-start actions are loopback RCON commands; physical input is not
initialized and the mouse cannot be captured.

## Evidence

Three staged-runtime repetitions passed in
`.tmp/networking/canonical-shotgun-damage-runtime.json`. Every row reports:

- `status=pass`, `failure_code=0`, and a genuine received attack in a valid
  canonical scope;
- policy `3`, a historical target hit, no fallback, and a positive 56 ms
  applied age;
- exactly 48 observed current-authority damage; and
- unchanged current target geometry with zero capture-append rejections.

The paired Railgun regression also passed three times from
`.tmp/networking/canonical-rail-damage-runtime-v3.json`, retaining policy
`5` and exact 80 damage at positive ages of 40--56 ms. This confirms the
shared configuration did not regress the prior piercing path.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon shotgun --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-shotgun-damage-runtime.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Results: the focused contracts passed 17/17, the three shotgun repeats passed,
and the complete suite passed 137/137.

## Remaining scope

This completes the canonical-command shotgun acceptance seam, not
`FR-10-T11`. Plasma-beam and thunderbolt
command-to-damage scenarios; mover and lifecycle matrices; fairness/abuse/load
budgets; platform evidence; and release promotion remain open.
