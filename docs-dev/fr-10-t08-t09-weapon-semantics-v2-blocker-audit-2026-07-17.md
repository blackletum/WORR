# Weapon firing-semantics and local-action V2 blocker audit

Date: 2026-07-17

Project tasks: `FR-10-T08`, `FR-10-T09`, `FR-10-T14`

Status: implemented shadow-only capability boundary; no gameplay or
presentation promotion.

## Outcome

WORR now has a third pointer-free layer for the 22 ordinary Rerelease weapons.
The stable catalog names each weapon, the frame profile freezes its legacy
driver timeline, and the new firing-semantics descriptor records the production
callback's trigger family, emission family, direct state dependencies, special
behavior, nominal ammo-debit range, and emission-count range.

The complete ordered firing-semantics digest is `a5d823b554b31ee8`.
Validation cross-checks every descriptor against both the catalog and frame
profile before exact-comparing the canonical entry. All 22 production wrappers
now validate both immutable layers before invoking the unchanged legacy driver
and callback.

This descriptor is a capability audit, not a replacement weapon simulator.
Every entry carries an explicit nonzero local-action V2 blocker mask. The audit
therefore reports `v2_representable=0` for all 22 weapons and prevents catalog
identity or superficially similar cadence from being treated as parity.

## Exact callback bounds

Ammo and emission bounds are per legacy fire-callback invocation and include
ordinary callback early-return paths. The frame profile separately owns how
many callbacks an accepted trigger schedules; this distinction matters for
Phalanx and BFG.

| Weapon | Trigger family | Emission family | Ammo debit | Emissions |
|---|---|---|---:|---:|
| Blaster | single press | straight projectile | 0 | 1 |
| Chainfist | held repeat | melee trace / ProBall ballistic override | 0 | 0-1 |
| Shotgun | single press | hitscan pellets | 1 | 11-12 |
| Super Shotgun | single press | hitscan pellets | 2 | 20 |
| Machinegun | held repeat | hitscan | 0-1 | 0-1 |
| ETF Rifle | held repeat | straight projectile | 0-1 | 0-1 |
| Chaingun | spin repeat | hitscan | 0-3 | 0-3 |
| Hand Grenades | throw hold/release | ballistic projectile | 1 | 1 |
| Trap | throw hold/release | ballistic deployable | 1 | 1 |
| Tesla Mine | throw hold/release | ballistic deployable | 1 | 1 |
| Grenade Launcher | single press | ballistic projectile | 1 | 1 |
| Prox Launcher | single press | ballistic deployable | 1 | 1 |
| Rocket Launcher | single press | straight projectile | 1 | 1 |
| HyperBlaster | held repeat | straight projectile | 0-1 | 0-1 |
| Ion Ripper | single press | straight-projectile burst | 0-10 | 0-15 |
| Plasma Gun | held repeat | straight projectile | 0-1 | 0-1 |
| Plasma Beam | held repeat | continuous beam | 0-2 | 0-1 |
| Thunderbolt | held repeat | continuous beam / water discharge | 0-1 | 0-1 |
| Railgun | single press | piercing beam | 1 | 1 |
| Phalanx | staged multi-frame | straight projectile | 0-1 | 1 per callback |
| BFG10K | windup/commit | straight projectile | 0-50 | 0-1 |
| Disruptor | single press | targeted projectile | 1 | 1 |

The trigger-family totals are 9 single press, 7 held repeat, 1 spin repeat,
3 throw hold/release, 1 staged multi-frame, and 1 windup/commit.

## V2 representability result

The current `worr_local_action_weapon_rule_v2` has one fixed ammo debit, one
fixed refire duration, one ready/fire frame, and one gameplay/audio/effect
emitter. It cannot express the production callbacks above.

Every descriptor has these common blockers:

- authenticated command-time ownership does not yet cover the later
  `ClientBeginServerFrame` weapon advance;
- the complete legacy frame timeline is richer than V2's phase timer;
- collision, entity, damage, and target results remain server authoritative;
- exact production asset/event mapping and presenter suppression are absent.

Weapon-specific masks additionally name held-repeat state, hold/release,
variable ammo or emission counts, staged fire, ruleset branches, client-frame
state, random streams, ProBall override, server-entity lifecycles, and
historical queries. A successful audit copies the exact mask into caller-owned
zeroed output; invalid or corrupted descriptors leave the output byte-identical.
A zero mask would only be a necessary condition for V2 promotion, never an
authorization by itself.

## Production safety boundary

The production wrapper check only validates immutable catalog/profile/
semantics facts. It does not call `Worr_LocalActionBuildTransactionV2`, move
weapon timing into command replay, spend ammo, create a trace or entity, emit
an event, suppress audio/effects, write a packet/snapshot/demo, or enable a
cvar. The existing legacy drivers and callbacks remain the sole gameplay and
presentation authority. Grapple remains outside this catalog in the separate
Hook interaction contract, and `q2proto/` is unchanged.

## Verification

All executable checks use the repository's no-window, input-free process
policy.

- `network-local-action-weapon-semantics` checks all 22 exact descriptors,
  trigger totals, nonzero common and weapon-specific blockers, special
  Shotgun/Chaingun/Hand Grenade/Ion Ripper/Plasma Beam/Thunderbolt/Phalanx/BFG
  bounds, corrupt-descriptor rejection, byte-identical failed output, and
  golden digest `a5d823b554b31ee8`.
- `sgame_x86_64.dll` compiles and links with production wrapper validation.
- The catalog (`e857eec08cfa9c00`) and frame-profile
  (`4f723f6fddf5bf52`) tests remain independent gates.

## Next dependency

The bounded observation-only lease now owns and records at most one exact
post-command weapon advance, with a fail-closed joined lookup when the scoped
and leased intermediate states match. Next, prove that production path in a
dedicated headless server fixture and use the joined oracle to design a richer
local-action state/event model. Do not project any weapon into V2 merely
because its common case has a fixed ammo cost. Live shadow parity, presenter
de-duplication, correction budgets, impairment/load evidence, and explicit
promotion gates remain required by `FR-10-T08`.
