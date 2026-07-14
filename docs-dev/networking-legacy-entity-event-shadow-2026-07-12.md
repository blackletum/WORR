# Legacy Entity-Event Canonical Shadow Pipeline

Task: `FR-10-T05`

Date: 2026-07-12

Status: non-authoritative production shadow slice implemented and tested;
`FR-10-T05` remains in progress.

## Outcome

The live sgame-to-engine path now mirrors the final legacy `entity_state_t.event`
value from each authoritative server tick into the canonical event core. This
is an observational shadow only:

- legacy `s.event` remains the packet and presentation authority;
- the shadow never clears, rewrites, or gates a legacy event;
- no snapshot, packet, demo, or `q2proto/` format changed;
- shadow rejection, overflow, or an unavailable extension is a no-op for live
  gameplay and packet construction.

The slice exercises the canonical ABI and journal with real sgame producers
while retaining a clean rollback boundary.

## Optional Named Import Extension

`inc/shared/event_shadow.h` defines the C-compatible named extension
`WORR_CANONICAL_EVENT_SHADOW_IMPORT_V1`. The API has an explicit structure size
and version and is discovered through the existing `game_import_t.GetExtension`
function. No field was added to `game_import_t`, and neither the game nor cgame
API version was changed.

The extension accepts an ID-less canonical candidate by const pointer, copies
it, assigns authority inside the engine, and returns a bounded result enum. It
also exposes:

- a fixed-size telemetry snapshot;
- a copied record query by age from the newest retained record;
- retained journal state and the fieldwise canonical record hash.

The query is a developer/debug seam. It does not expose a mutable engine
pointer or grant the shadow data packet authority.

## Engine Ownership

`src/server/event_shadow.c` owns one fixed 4096-slot canonical journal in static
storage. Both listen-server and dedicated engine libraries link the existing
`canonical_event_core` static library; cgame and unrelated binaries do not.

Immediately before each `ge->SpawnEntities` call, the server resets the shadow
for the new map. Epochs start non-zero and increase monotonically for the life
of the engine process. Sequence zero is reserved. Assignment is committed only
after validation and successful journal insertion, so rejected candidates do
not create stream gaps.

At `UINT32_MAX` sequence, the allocator advances the epoch and starts at
sequence one. It clears the prior-epoch retained view as required by the
single-epoch journal. Epoch exhaustion fails closed and permanently disables
subsequent submissions in that process instead of reusing an identity.

Before assigning a new ID, the engine detects an identical retained candidate
by its complete semantic tuple. A repeated scan therefore returns duplicate
without consuming a sequence. Same-source, same-tick candidates remain
distinct when their source ordinals or payloads differ.

The engine validates the fully authoritative copy through
`Worr_EventRecordValidateV1`, inserts through
`Worr_EventJournalInsertAuthoritativeV1`, and records its fieldwise hash.
Capacity exhaustion leaves the next ID unchanged and increments an explicit
failure counter. This first shadow intentionally retains the map's first 4096
records and then fails closed; retention/eviction policy requires later traffic
measurement rather than an implicit live behavior change.

Telemetry includes reset, attempt, accepted, duplicate, invalid, capacity,
conflict, ID exhaustion, sequence wrap, and query counts, plus current epoch,
occupancy, last sequence/result, and last record hash.

## Sgame Mapping and Timing

`src/game/sgame/network/event_shadow.cpp` discovers the extension once and is a
no-op when it is absent or malformed. `G_RunFrame` scans once after all internal
simulation substeps and end-of-frame gameplay producers have settled. The next
`G_PrepFrame` still clears legacy events exactly as before.

Entities are visited in ascending slot order, which makes global assignment
order deterministic. Every in-use entity with a non-zero final `s.event` maps
to a `LEGACY_BRIDGE` record with:

- engine server frame as source tick;
- deterministic sgame time in integer microseconds;
- source entity index and generation;
- transient delivery and one-tick expiry;
- authoritative-only prediction class for this migration stage;
- `U32X4` payload containing raw legacy event, entity index, generation, and
  source ordinal.

The existing `spawn_count` starts at zero and increments when a slot is freed.
Canonical generation zero is reserved, so the mapper uses
`uint32_t(spawn_count) + 1`. The wrapping value is rejected rather than
aliasing generation zero. A compile-time assertion also guarantees every
current legacy value through `EV_LADDER_STEP` fits the mapper's bounded event
mask.

Per-entity caller-owned mapper state records the event values already observed
for one `(source tick, generation)`. Repeating the scan is idempotent. A
different event value in that same tick receives the next deterministic source
ordinal, and changing the generation resets the ordinal domain.

This is deliberately **legacy final-state shadow semantics**. The single
`s.event` field can be overwritten by another producer before the end-of-tick
scan, so this slice cannot recover multiple events that never coexist in the
legacy field. The duplicate mask is not the future typed-producer guarantee;
direct producer migration must emit each canonical event at creation time.

## Deterministic Evidence

`tools/networking/event_shadow_test.c` covers:

- exact `LEGACY_BRIDGE` mapping, bounded payload, zero authority candidate ID,
  source time/tick, expiry, and generation;
- duplicate scan suppression and atomic no-mutation on duplicate/invalid map;
- distinct same-source/same-tick values receiving ordered ordinals;
- entity slot reuse producing a different generation;
- expiry across tick wrap;
- optional extension shape and copied debug queries;
- monotonic ID assignment and newest-to-oldest global order;
- duplicate, invalid, reset, and telemetry counters;
- capacity and capacity-plus-one behavior across all 4096 retained slots;
- sequence wrap into the next epoch and fail-closed epoch exhaustion.

The shared C and C++ layout tests now include the shadow input, source state,
and telemetry records.

Targeted validation on the Windows Clang build:

```text
meson setup --reconfigure .tmp/build-event-core
meson compile -C .tmp/build-event-core event_shadow_test \
  event_schema_layout_c_test event_schema_layout_cpp_test
meson test -C .tmp/build-event-core network-event-shadow \
  network-event-journal network-event-schema-layout-c \
  network-event-schema-layout-cpp --repeat 3 --print-errorlogs
meson compile -C .tmp/build-event-core worr_engine_x86_64 \
  worr_ded_engine_x86_64 sgame_x86_64
```

The four tests passed for three repetitions (12/12). The listen-server engine,
dedicated engine, and sgame DLL all compiled and linked with the production
shadow path.

## Remaining `FR-10-T05` Work

This shadow does not complete `FR-10-T05`. Remaining work includes:

1. Migrate temp events, muzzle flashes, positioned/local sounds, and other
   out-of-band effect producers into typed canonical payloads.
2. Replace single-field final-state scanning with direct multi-event producer
   submission so no same-tick event can be overwritten before capture.
3. Add command-derived prediction keys and prove real predicted weapon/effect
   matching rather than authoritative-only legacy bridging.
4. Add server retention/resend policy and carry selective receipt state in the
   negotiated WORR transport under `FR-10-T04`.
5. Feed immutable canonical event ranges to cgame, migrate presentation, and
   retire the local raw legacy journal only after semantic shadow parity.
6. Validate legacy server, client, and demo behavior plus the full impairment
   matrix with shadow comparison evidence.
7. Profile journal occupancy, scan cost, and capacity under `FR-10-T14` before
   selecting release retention budgets.

No `.install/` refresh was performed for this targeted shadow build; the root
integration build owns distributable staging after all concurrent networking
changes settle.

