# Rerelease weapon catalog identity and sgame boundary

Date: 2026-07-17

Project tasks: `FR-10-T08`, `FR-10-T09`

Status: implemented catalog/mapping prerequisite; shadow-only, with no gameplay
or presentation promotion.

## Outcome

WORR now has one pointer-free, transport-neutral identity catalog for all 22
ordinary Quake II Rerelease weapons. The shared catalog freezes each weapon's
stable ID, current legacy state-machine driver, inventory ownership contract,
and promotion state. Every entry is explicitly `SHADOW_ONLY`: the catalog is
not evidence that the simplified local-action v2 rule model reproduces the
weapon, and it cannot authorize predicted gameplay or presentation.

A second pointer-free profile keyed by the same IDs freezes the exact frame
skeleton supplied to the legacy driver: activation/fire/idle/deactivation
boundaries, pause frames, generic-driver fire frames, throw prime/hold/release
frames, the hand-grenade post-driver frame skip, the BFG Quake 3 Arena
alternate fire-frame set, and Ion Ripper's 1,000 ms minimum refire override.
Its complete ordered digest is `4f723f6fddf5bf52`.

A third pointer-free descriptor freezes each production firing callback's
trigger/emission family, direct dependencies, special behavior, per-callback
ammo/emission bounds, and exact reasons it cannot be projected into the current
local-action V2 rule. Its ordered digest is `a5d823b554b31ee8`; all 22 entries
are explicitly non-representable by V2.

The selected-weapon Grapple is deliberately outside this 22-entry catalog.
Mapped `+hook` input and authoritative Hook receipts remain owned by the
independent local-interaction contract. The sgame adapter rejects Grapple in
both item-to-catalog and selection-to-catalog directions.

## Catalog

| ID | Weapon | Legacy driver | Inventory contract | Sgame selected item | Active ammo item |
|---:|---|---|---|---|---|
| 1 | Blaster | `Weapon_Generic` | no ammo | `IT_WEAPON_BLASTER` | `IT_NULL` |
| 2 | Chainfist | `Weapon_Repeating` | no ammo | `IT_WEAPON_CHAINFIST` | `IT_NULL` |
| 3 | Shotgun | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_SHOTGUN` | `IT_AMMO_SHELLS` |
| 4 | Super Shotgun | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_SSHOTGUN` | `IT_AMMO_SHELLS` |
| 5 | Machinegun | `Weapon_Repeating` | weapon + ammo | `IT_WEAPON_MACHINEGUN` | `IT_AMMO_BULLETS` |
| 6 | ETF Rifle | `Weapon_Repeating` | weapon + ammo | `IT_WEAPON_ETF_RIFLE` | `IT_AMMO_FLECHETTES` |
| 7 | Chaingun | `Weapon_Repeating` | weapon + ammo | `IT_WEAPON_CHAINGUN` | `IT_AMMO_BULLETS` |
| 8 | Hand Grenades | `Throw_Generic` | selectable ammo | `IT_AMMO_GRENADES` | `IT_AMMO_GRENADES` |
| 9 | Trap | `Throw_Generic` | selectable ammo | `IT_AMMO_TRAP` | `IT_AMMO_TRAP` |
| 10 | Tesla Mine | `Throw_Generic` | selectable ammo | `IT_AMMO_TESLA` | `IT_AMMO_TESLA` |
| 11 | Grenade Launcher | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_GLAUNCHER` | `IT_AMMO_GRENADES` |
| 12 | Prox Launcher | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_PROXLAUNCHER` | `IT_AMMO_PROX` |
| 13 | Rocket Launcher | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_RLAUNCHER` | `IT_AMMO_ROCKETS` |
| 14 | HyperBlaster | `Weapon_Repeating` | weapon + ammo | `IT_WEAPON_HYPERBLASTER` | `IT_AMMO_CELLS` |
| 15 | Ion Ripper | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_IONRIPPER` | `IT_AMMO_CELLS` |
| 16 | Plasma Gun | `Weapon_Repeating` | weapon + ammo | `IT_WEAPON_PLASMAGUN` | `IT_AMMO_CELLS` |
| 17 | Plasma Beam | `Weapon_Repeating` | weapon + ammo | `IT_WEAPON_PLASMABEAM` | `IT_AMMO_CELLS` |
| 18 | Thunderbolt | `Weapon_Repeating` | weapon + ammo | `IT_WEAPON_THUNDERBOLT` | `IT_AMMO_CELLS` |
| 19 | Railgun | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_RAILGUN` | `IT_AMMO_SLUGS` |
| 20 | Phalanx | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_PHALANX` | `IT_AMMO_MAGSLUG` |
| 21 | BFG10K | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_BFG` | `IT_AMMO_CELLS` |
| 22 | Disruptor | `Weapon_Generic` | weapon + ammo | `IT_WEAPON_DISRUPTOR` | `IT_AMMO_ROUNDS` |

The complete ordered semantic digest is `e857eec08cfa9c00`. It includes the
catalog version/revision/count and every ID, driver, inventory contract, and
promotion state. The digest does not depend on padding or process pointers.

## Production integration

- `local_action_catalog.h` and `local_action_catalog.c` provide the C/C++
  pointer-free ABI, exact-copy validation, hostile fail-closed output rules,
  and complete-catalog digest.
- `local_action_weapon_profile.h` and `local_action_weapon_profile.c` provide
  the exact immutable frame profiles. All 22 ordinary production weapon
  wrappers now copy their parameters from those profiles before calling the
  unchanged legacy driver. Fixed-capacity local arrays add the sentinel needed
  by the legacy API, so no profile pointer or caller storage escapes the call.
  Grapple and ProBall's special ball-throw branch remain explicit exceptions.
- `local_action_weapon_semantics.h` and
  `local_action_weapon_semantics.c` provide the exact callback capability and
  V2-blocker audit. The production wrappers validate this descriptor beside
  the frame profile without moving callback authority.
- `local_action_weapon_catalog.cpp` binds the shared IDs to `Weapon`,
  `item_id_t`, and the exact ammo item. Runtime validation proves 22 unique
  round trips against `itemList`, a non-null weapon think function,
  `IF_WEAPON`, the expected `IF_AMMO` ownership shape, and the exact ammo item.
- Weapon-preference reconstruction and chat short-name mapping now use this
  adapter instead of maintaining two additional 22-way switch statements.
- The existing authenticated action-observation ledger validates the catalog
  at map reset and counts scoped records plus scoped/unscoped weapon thinks by
  stable catalog ID. Grapple and unexpected/unmapped items have separate
  counters. A read-only value-copy telemetry function exposes those counts;
  it cannot affect weapon state.

## Safety boundary

This change does not run `Worr_LocalActionBuildTransactionV2` in cgame or
sgame, replace `Think_Weapon`, alter command timing, spend ammo, spawn a
projectile, trace the world, create an event, suppress an effect, write a
packet/snapshot/demo, or enable a cvar. The legacy weapon implementation remains
the only gameplay and presentation authority.

All entries remain shadow-only because catalog identity, inventory mapping,
the legacy frame skeleton, and an exact callback capability audit are not a
deterministic shared weapon simulator. The audit proves that multi-shot
emission/ammo behavior, charge/cook/release, ruleset and powerup state,
projectile/hitscan/beam/deployable authority, asset mapping, command-time
ownership, death/respawn, and wheel transitions exceed local-action V2.

## Verification

All executable tests were launched with the shared no-window/input-free process
policy.

- `network-local-action-catalog`: passes all 22 exact entries, 11 generic / 8
  repeating / 3 throw drivers, 2 no-ammo / 17 weapon-and-ammo / 3 selectable-
  ammo contracts, exact digest, corrupted descriptor rejection, invalid ID
  rejection, and byte-identical failure output.
- `network-local-action-weapon-catalog-adapter`: passes every item, ammo, and
  `Weapon` round trip; proves Grapple exclusion; and detects one corrupted
  Rocket Launcher ammo binding.
- `network-local-action-weapon-profile`: passes all 22 exact profiles, special
  Hand Grenade/Tesla/Ion Ripper/BFG assertions, corrupted-profile rejection,
  byte-identical failure output, and digest `4f723f6fddf5bf52`.
- `network-local-action-weapon-semantics`: passes all 22 exact callback
  descriptors, six trigger-family totals, hostile failure output, a zero-V2-
  representability result, and digest `a5d823b554b31ee8`.
- The shipped `sgame_x86_64.dll` compiles and links with the catalog validator,
  production mapping consumers, and observation telemetry.
- `q2proto/` was not changed.

## Next dependency

The bounded observation-only lease now records at most one exact production
weapon advance after the authenticated command scope and joins it only across
a byte-identical intermediate state. Prove the placement through a real
headless server fixture, then design a richer shared action model from that
oracle. Local-action V2 must continue to fail closed for every catalog weapon.
Promotion remains blocked until exact command-by-command state/event parity
and correction budgets pass for every entry under the required impairment
matrix.
