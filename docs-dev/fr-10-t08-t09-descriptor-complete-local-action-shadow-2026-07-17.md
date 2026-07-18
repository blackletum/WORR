# Descriptor-complete local-action shadow

Date: 2026-07-17

Project tasks: `FR-10-T08`, `FR-10-T09`, `FR-10-T14`

Status: shared model and live sgame evidence implemented; prediction,
presentation, and Snapshot V2 promotion remain disabled.

## Outcome

WORR now has a pointer-free shared record that binds one joined legacy weapon
observation to the exact frozen facts for its Rerelease catalog entry. The
record combines:

- canonical command identity and the complete before/after observation;
- the exact 22-entry catalog fact;
- the exact legacy frame profile;
- the exact callback-capability semantics and V2 blocker mask;
- all three catalog-wide revision digests; and
- descriptor and record hashes.

The model is named `worr_local_action_shadow_v1`. It is deliberately distinct
from `worr_local_action_transaction_v2`: it reports what production authority
did and why the weapon remains blocked, but does not invent an emission,
damage result, projectile, event asset, random result, or presentation cue.

## Shared contract

`inc/shared/local_action_shadow.h` freezes a 528-byte C ABI with no pointers,
ownership, allocator state, or compiler-dependent payload. Its nested records
remain independently validated. `Worr_LocalActionShadowBuildV1` requires a
valid observation and copies the exact catalog, profile, and semantics entries
for the supplied catalog identity.

The builder also enforces the catalog inventory class at the observed input
boundary:

- no-ammo weapons must expose no ammo item and zero ammo;
- weapon-plus-ammo entries require a separate nonzero ammo item; and
- selectable-ammo entries require the active item to own its ammo count.

Flags report only directly observed facts: descriptor completeness,
shadow-only promotion, nonzero V2 blockers, attack-held state, active-weapon
stability, and whether the ammo delta is comparable. A switch makes the delta
non-comparable rather than guessing across two inventories.

All failure paths are byte-atomic. Validation rebuilds the complete record
from its nested observation and requires byte equality, so corruption of any
descriptor, digest, flag, delta, semantic observation, or hash fails closed.

## Sgame adapter

`SG_LocalActionBuildShadowFromObservation` resolves the opaque observed item
through the production 22-weapon adapter and additionally requires the exact
sgame ammo item. Grapple continues to resolve to `NONE` because Hook is owned
by the independent local-interaction contract.

`SG_LocalActionObservationCopyShadowForCommand` first obtains the existing
scoped-plus-leased joined record, then constructs the shared shadow record.
There is no second command lookup, gameplay callback, state mutation, event
submission, or packet writer in this path.

## Deterministic evidence

The shared test constructs one valid record for every catalog entry and
freezes the ordered 22-record digest:

```text
catalog digest:   e857eec08cfa9c00
profile digest:   4f723f6fddf5bf52
semantics digest: a5d823b554b31ee8
shadow digest:    1270244792631122
```

The suite also covers weapon switching, non-comparable ammo, stale output,
invalid catalog identity, nested descriptor corruption, shared inventory-class
rejection, exact sgame ammo binding, and Grapple separation.

## Three-repeat live evidence

The final report is
`.tmp/networking/local-action-shadow-runtime-final.json`, schema
`worr.networking.canonical-weapon-damage-runtime.v30`.

| Run | Command | Offers | Supersedes | Claims | Expired | Flags | Blockers | Record hash |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | `1:295` | 428 | 47 | 411 | 17 | 127 | 4367 | 10370735818637322309 |
| 2 | `1:351` | 489 | 40 | 470 | 19 | 127 | 4367 | 10769990459690302000 |
| 3 | `1:313` | 458 | 29 | 443 | 15 | 127 | 4367 | 13803608422251563939 |

Every run used one dedicated server and two hidden, input-disabled clients.
The shooter supplied ordinary `+attack`; production `ClientThink`,
`ClientBeginServerFrame`, `Think_Weapon`, and Blaster callback paths retained
authority. Every run reports catalog `1`, descriptor-complete shadow evidence,
exact scoped/leased/continuity/join proofs, zero duplicates, zero rebases, and
zero rejection. The varying record hashes are expected because command
identity and the observed transition are semantic input.

## Verification

- `network-local-action-shadow`: passed.
- `network-local-action-weapon-catalog-adapter`: passed.
- `network-local-action-observation`: passed.
- runtime parser and validation contracts: 42/42 passed.
- `sgame_x86_64.dll`: compiled and linked cleanly.
- three-repeat hidden-client runtime gate: passed.
- refreshed `windows-x86_64` `.install/`: 16 root runtime files, one root
  dependency, 524 packaged assets, and staged validation passed.
- `q2proto/`: unchanged.

## Remaining boundary

This closes the missing descriptor-complete authoritative shadow model; it
does not complete `FR-10-T08` or `FR-10-T09`. No real weapon is representable
by the current V2 rule, no cgame transaction has been derived from this
record, and no predicted event is submitted or suppressed. The next safe
milestone is to carry the immutable shadow record to a cgame-side audit owner
and prove command-correlated receipt/parity without granting gameplay or
presentation authority. Live reconnect rebase evidence also remains open.
