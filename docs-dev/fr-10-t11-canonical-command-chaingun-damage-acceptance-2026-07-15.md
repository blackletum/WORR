# FR-10-T11 canonical-command Chaingun damage acceptance seam

Task references: `FR-10-T10`, `FR-10-T11`, `FR-10-T14`, and `FR-10-T15`.

## Purpose

This increment proves the normal Chaingun burst path from a real client command
through `ClientThink`, `Item::weaponThink`, the canonical rewind query, and
current-authority damage. It builds on the shared two-client fixture but does
not permit the fixture to create command authority, choose a trace result, or
invoke a weapon callback.

## Production seam

The Chaingun arm command selects `WORR_REWIND_WEAPON_CHAINGUN` (policy `2`),
`IT_WEAPON_CHAINGUN`, and exact expected damage of `18`. The fixture starts at
the standard Chaingun gun frame `14`; normal `Weapon_Chaingun_Fire` advances
to frame `15` and selects its regular three-round burst from the received
attack button. With deathmatch's six damage per round, the terminal result
must therefore be exactly 18 damage. One- or two-round outcomes cannot pass.

The fixture now also clears its synthetic players' `pers.healthBonus`. A
newly admitted player may otherwise retain spawn-health decay state, causing a
post-burst one-point health decrement that corrupts damage measurement. This
is fixture-owned lifecycle normalization; it neither alters the received
button nor changes production damage, trace, or rewind authority.

## Headless acceptance gate

`tools/networking/run_canonical_rail_damage_runtime_gate.py --weapon chaingun`
arms `sv worr_rewind_canonical_chaingun_damage_arm` and polls its dedicated
status cvar. The test runs a dedicated server plus two real UDP clients in
separate runtime homes. Both clients launch with `win_headless=1`,
`in_enable=0`, `in_grab=0`, `s_enable=0`, `stdin=DEVNULL`, and Windows
no-window creation. The only follow-up interaction is loopback RCON; client
input is never initialized and the mouse cannot be captured.

## Evidence

All three staged-runtime repetitions in
`.tmp/networking/canonical-chaingun-damage-runtime-v2.json` pass with:

- policy `2`, valid canonical scope, and a real received attack;
- a positive 56 ms applied rewind age, canonical historical target hit, and
  no fallback;
- exactly 18 observed current-authority damage; and
- unchanged live target geometry, zero capture-append rejections, and 95--97
  normal target end-frame capture callbacks.

The shared setup was then regressed three times per existing weapon from the
same freshly staged build: Railgun stayed policy `5`/80 damage in
`.tmp/networking/canonical-rail-damage-runtime-v4.json`, machinegun stayed
policy `1`/8 damage in `.tmp/networking/canonical-machinegun-damage-runtime-v3.json`,
and shotgun stayed policy `3`/48 damage in
`.tmp/networking/canonical-shotgun-damage-runtime-v2.json`. Every one of
these nine rows retained a positive-age historical hit, no fallback, and
unchanged current geometry.

## Verification

```powershell
python -m unittest tools/networking/test_lag_compensation_canonical_rail_contract.py `
  tools/networking/test_run_canonical_rail_damage_runtime_gate.py `
  tools/networking/test_headless_input_contract.py
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --weapon chaingun --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install `
  --output .tmp/networking/canonical-chaingun-damage-runtime-v2.json `
  --repeat 3 --timeout 35
meson test -C builddir-win --no-rebuild
```

Results: focused contracts 19/19; three Chaingun repetitions passed; complete
suite 137/137.

## Remaining scope

This completes the canonical-command Chaingun seam, not `FR-10-T11`.
Plasma-beam and thunderbolt command-to-damage scenarios;
mover and lifecycle matrices; fairness/abuse/load budgets; platform evidence;
and release promotion remain open.
