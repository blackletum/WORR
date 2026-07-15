# FR-10-T11 headless railgun damage and fairness gate

Date: 2026-07-15  
Project task: `FR-10-T11` (depends on `FR-10-T10`)

> Historical baseline: this document records the original bounded-only gate.
> The current three-class near/bounded/capped gate and its V2 status contract
> are documented in `docs-dev/fr-10-t11-latency-class-rail-damage-gate-2026-07-15.md`.

## Outcome

`FR-10-T11` now has a deterministic, dedicated-server acceptance gate that
drives the production railgun path through `fire_rail()`, `pierce_trace()`,
`LagCompensation_TraceLine()`, and the ordinary `Damage()` callback. It
demonstrates both sides of the live fairness decision without accepting a
client-authored hit result:

1. an acknowledgement at the current server frame is rejected as a rewind
   authority source, so the production rail trace remains current-world, misses
   the displaced target, and applies no damage; and
2. an older acknowledged frame from the shooter's ordinary server-frame
   capture resolves an eligible historical target pose, hits that pose under
   the railgun policy, and applies 20 points of normal railgun damage to the
   current authoritative target only after the collision query returns.

The gate is intentionally a legacy-ack diagnostic, because an `sv` console
command does not run inside a real client command callback and therefore must
not synthesize canonical command authority. It complements, rather than
replaces, the existing canonical-scene coverage. `g_lag_compensation` remains
default-off in all ordinary server configurations.

## Implementation

`LagCompensation_ArmRailDamageRuntimeProbe()` takes two real dedicated-server
bots, places them apart, and temporarily gives them zero gravity while ordinary
end-frame capture accumulates player poses and server-frame mappings. The
collision-only fixture map deliberately has no world floor; this is a fixture
survival measure, not gameplay behavior.

`LagCompensation_RunRailDamageRuntimeProbe()` then:

- selects a bounded, older server-frame record from the shooter's actual
  legacy-frame history and verifies that the target has a compatible linked,
  damageable, alive historical bounds pose at that time;
- moves the current shooter onto a ray toward that historical pose and moves
  the current target 96 units off the ray, proving a native current-world
  trace is fully unblocked;
- clears the fixture shooter's `SVF_BOT` bit only for the two synchronous
  weapon calls, so the normal non-bot acknowledgement eligibility branch is
  exercised in a headless server with no human client; the marker, command,
  pose, linkage, velocity, gravity, and target health are restored before the
  command returns;
- calls the real railgun with `serverFrame == currentFrame`, requiring the
  `AUTHORITY_REJECTED` current-world fallback and unchanged target health; and
- calls the same weapon again with the recorded older frame, requiring a
  legacy historical railgun hit on the target identity, an unchanged
  collision-authority guard during the query, unchanged current target
  geometry, and positive post-query damage.

The probe searches new immutable observation records by sequence and content,
rather than assuming the final journal record is the hit. This is required for
the real railgun: after piercing a player it performs one further trace with
that player in the per-ray ignore set, which correctly terminates as a miss.

The console commands are deliberately server-only:

- `sv worr_rewind_rail_damage_arm`
- `sv worr_rewind_rail_damage_selftest`

The second command publishes a stable `CVAR_NOSET` status through
`sg_worr_rewind_rail_damage_selftest_status`. The dedicated runner is
`tools/networking/run_rewind_rail_damage_runtime_gate.py`; it enables the
otherwise-default-off master cvar and observation journal only for the isolated
fixture process.

## Acceptance result

Three independent headless dedicated runs produced the identical status:

```text
pass:1:1:1:1:1:1:1:1:1:1:1:2:20:1000000:17574:0
```

This proves: setup and ordinary history were present; the current-world
baseline missed; the invalid acknowledgement fell back and dealt no damage;
the bounded legacy route selected the railgun policy and found the historical
target; real damage occurred; collision geometry remained current; and the
query authority guard was unchanged. The hit used two historical player
candidates, dealt 20 damage, and had a stable historical fraction of `0.017574`.
All three runs had empty stderr and identical stdout SHA-256 values.

## Validation

The following validation passed:

```powershell
ninja -C builddir-win sgame_x86_64.dll
python tools/networking/test_run_rewind_rail_damage_runtime_gate.py
python tools/networking/test_lag_compensation_rail_damage_contract.py
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
meson test -C builddir-win --no-rebuild network-rewind-rail-damage-runtime-gate-parser network-lag-compensation-rail-damage-contract
python tools/networking/run_rewind_rail_damage_runtime_gate.py --dedicated-exe .install/worr_ded_x86_64.exe --working-dir .install --output .tmp/networking/rewind-rail-damage-runtime.json --repeat 3 --timeout 45
```

The focused Meson run passed `2/2`. The `q2proto/` subtree remained unchanged.
The final full headless Meson suite passed `134/134`.

## Remaining FR-10-T11 scope

This is one real weapon/damage/fairness slice, not task completion. The
canonical client-command live weapon path, the remaining supported weapons,
historical movers during weapon damage, operator guidance, abuse/load budget,
and release-promotion gates remain open.
