# FR-10 native snapshot presentation authority

Date: 2026-07-18  
Project tasks: `FR-10-T06`, `FR-10-T07`, `FR-10-T14`  
Related task: `FR-10-T04`

## Outcome

The production cgame can now use the privately admitted native snapshot
timeline as actual remote-entity transform authority. The feature remains an
explicit default-off diagnostic policy, local prediction remains authoritative
for the controlled player, and every unsafe or unavailable remote sample falls
back independently to the existing renderer.

This is the first live process-boundary proof of native snapshot presentation;
it is not a public protocol rollout or completion of `FR-10-T07`.

## Source-owned presentation gate

The engine publishes an internal read-only, non-archived
`cl_worr_native_snapshot_timeline_owned` latch. It becomes `1` only after a
fresh native snapshot challenge binds the timeline epoch. It remains latched
through diagnostic `DRAIN`, preventing legacy and native producers from
alternating inside one epoch, and returns to `0` only at map/serverdata or the
matching connection boundary.

`cg_snapshot_timeline_render=3` is the native-authority policy. Cgame refuses
that policy unless the engine-owned latch is set. Modes `0` through `2` keep
their previous legacy, audit, and parity-promotion behavior.

Semantic ACK pacing means the newest admitted native snapshot can legitimately
trail the simultaneously received legacy render timestamp. Mode 3 therefore
selects the earlier of the canonical render clock and the newest admitted
native server time. It does not compare different server instants merely to
satisfy a legacy-parity gate. The selected immutable pair is still subject to
generation, component, visibility, teleport, velocity, and policy blocks.

The renderer promotes only copied remote transforms. It never substitutes the
controlled player's predicted origin or angles. Aggregate diagnostics add
`native=<samples>/<blocks>` and require promoted transforms to equal accepted
native-authority samples.

## Fragmented mixed-packet correction

The live remote-entity fixture enlarged snapshots from one fragment to two and
found a transport defect below presentation. A dispatch begun by an async
zero-legacy packet froze a zero-byte legacy reserve; the next ordinary packet
with a legacy prefix was classified as `INVALID_ARGUMENT`, draining the
snapshot pilot.

`Worr_NativeCarrierSessionDispatchPreparePacketV1` now reports that changing
packet budget as `OUTPUT_TOO_SMALL`. The production adapter treats it as a
reversible deferral and retries the unchanged fragment on a compatible async
packet. A direct carrier-session regression proves gate/dispatch/output state
remains unchanged, and the carrier, mixed carrier, sender, server-pilot, and
production virtual-link rows all pass.

## Headless live acceptance

Schema v33 adds `blaster-native-snapshot-presentation`. The fixture launches:

- one dedicated server with private snapshot support;
- one ordinary input-free shooter that supplies the real remote player and
  Blaster action but does not join the snapshot pilot;
- one input-disabled hidden-render target that negotiates exact private
  `0x57`, enables policy mode 3, and owns presentation evidence.

The minimal rewind BSP has no useful cross-player PVS, so this isolated mode
sets server diagnostic `sv_novis=1`. That changes only which real entities are
included in both legacy and native snapshots; it does not alter collision,
damage, command identity, canonical projection, or protocol authority.

Final artifact:
`.tmp/networking/native-snapshot-presentation-acceptance.json`.

| Repeat | Queued / ACK / release | Confirmed packets | Samples | Native/promoted | Native blocks |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 124 / 39 / 53 | 80 | 303 | 84/84 | 219 |
| 2 | 128 / 39 / 54 | 79 | 303 | 287/287 | 17 |
| 3 | 112 / 33 / 47 | 67 | 261 | 189/189 | 77 |

Every repeat has a nonzero snapshot epoch, target-side semantic admission,
server ACK/release, and actual transform promotion. Clock failures, pair
failures, alignment failures, parity mismatches, event-audit failures, native
client/server failures, and maximum transform errors are all zero. Blocks are
expected per-entity fail-safe decisions and do not prevent safe peers from
being promoted.

## Aggregate verification

The final tree passes the production build, 17/17 focused native snapshot and
carrier rows, 93/93 runtime-gate contracts, and the complete 157/157 headless
networking suite in 783.7 seconds. Package/release/bootstrap contracts pass
30/30. The final `.install/` refresh validates the Windows x86-64 stage with
16 root runtime files, one runtime dependency, a 525-file `pak0.pkz`, one
q2aas reference map, 31 botfile payloads, and 215 RmlUi assets.

## Remaining scope

`FR-10-T06`, `FR-10-T07`, and `FR-10-T14` remain incomplete. The current
renderer still begins from the legacy current-frame visibility set, so
previous-only entities are not presented. Event/effect/audio cutover, bounded
extrapolation, adaptive interpolation, large dynamic arenas, demos/seeking,
full loss/reorder/load/soak matrices, visual validation, supported-platform
coverage, public defaults, and release rollback gates remain open.
