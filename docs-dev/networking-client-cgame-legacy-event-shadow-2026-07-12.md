# Client-to-Cgame Legacy Event Shadow Range

Task: `FR-10-T05`

Date: 2026-07-12

Status: non-authoritative client audit slice implemented and tested;
`FR-10-T05` remains in progress.

## Outcome

The client engine now exposes the final legacy entity-event values from every
accepted snapshot to the external cgame as one immutable, callback-scoped
canonical range. The range is a shadow observation only:

- `parse_entity_event` remains the only presenter and retains its existing
  timing and behavior;
- the shadow is delivered only after cgame has applied the accepted entity
  states and completed the legacy event pass;
- cgame records scalar audit counters and deterministic hashes only;
- the bridge cannot change snapshot acceptance, entity state, effects, sound,
  packet contents, demo contents, or legacy protocol behavior;
- an absent or incompatible extension, invalid range, capacity failure, or
  exhausted identity counter is a no-op for live frame handling.

No `q2proto/` source, wire format, demo format, or public cgame API version was
changed.

## Optional Named Export

`inc/shared/cgame_event_shadow.h` defines the C-compatible named cgame export
`WORR_CGAME_CANONICAL_EVENT_SHADOW_EXPORT_V1`. The client discovers it through
the existing `cgame_export_t.GetExtension` function and checks the structure
size, API version, and every required callback. Classic cgame and older
external modules simply run without a consumer.

The v1 export has three operations:

1. `Reset` establishes the engine-owned stream epoch and explains the reset
   reason.
2. `ConsumeCanonicalEventRange` receives a const range valid only for the
   duration of that callback.
3. `GetAuditStatus` copies scalar audit state to the caller.

The client owns the range and record storage. It zeros all delivered scratch
records immediately when the callback returns, so retaining the pointer cannot
appear to work. The current cgame consumer neither retains the pointer nor
copies event records; it reduces the range synchronously to counters and
hashes.

## Engine-Owned Lifecycle Identity

`src/client/event_shadow.cpp` owns fixed-capacity storage for 8192 observed
entities and 512 records. It does not allocate while accepting a frame.

Every visible non-world entity participates in lifecycle tracking, including
entities whose legacy event is zero. The first observation assigns generation
one. Absence from a later accepted frame marks the entity not present; a later
reappearance of the same entity index increments its observed generation.
This deliberately describes the client's visible lifecycle and is independent
of the sgame slot generation used by the server-side shadow. Entity index zero
is rejected rather than being assigned a lifecycle identity.

Each delivered range has engine-owned, monotonically increasing batch and
carrier sequence values. Each event record has a non-zero monotonically
increasing carrier ordinal. Exhaustion fails closed instead of wrapping inside
an epoch. Legacy shadow records remain non-authoritative: their event ID is
`{0, 0}`, they do not set `HAS_AUTHORITY_ID`, and their prediction class is
authoritative-only at this migration stage.

Source frame ticks are strictly increasing within an epoch; they do not use
modular comparison. A numeric rewind or wrap creates a new epoch before the
new tick is observed. Duplicate ticks are ignored without mutation.

## Reset Boundaries

The client advances the shadow epoch and clears observed generations,
ordinals, batches, and scratch state at the following boundaries:

- `CL_ClearState`, covering serverdata, map changes, disconnect/reconnect, and
  demo start/stop state replacement;
- explicit demo seek/restart before seek processing begins;
- an observed numeric frame rewind or wrap.

Consumer attachment is also reported through `Reset`, allowing a freshly
loaded cgame to align its audit with the current engine epoch without
inventing an event identity.

## Mapping and Deterministic Comparison

After `cgame_entity->DeltaFrame()` returns for a valid accepted frame, the
client scans the accepted parsed-entity range in its existing order. A known
non-zero legacy event through `EV_LADDER_STEP` maps to an ID-less
`LEGACY_BRIDGE` record with:

- accepted frame number and deterministic server time;
- engine-observed source entity generation;
- transient, present-once, one-tick delivery metadata;
- a `U32X4` payload containing raw legacy value, entity index, observed
  generation, and scan order.

The known legacy domain is compile-time pinned to values zero through nine.
Unexpected decoded values are omitted by the production adapter and rejected
by the bounded builder, while the containing entity still participates in
visible lifecycle tracking.

Shadow comparison intentionally normalizes each record to exactly
`(source tick, raw legacy event, entity index, scan order)`. Authority IDs,
observed generations, carrier ordinals, arrival time, and native structure
padding cannot influence the normalized hash. This permits meaningful parity
comparison across independent server and client lifecycle domains without
mistaking transport or storage identity for presentation semantics.

## Failure Atomicity and Audit

`src/common/net/cgame_event_shadow.c` is a caller-owned C11 builder and audit
implementation shared by the client and cgame modules. It prevalidates a full
frame before changing lifecycle state. Duplicate entity indices, invalid scan
order, world entity input, unknown events, capacity overflow, tick rewind,
generation exhaustion, and ordinal exhaustion return a bounded result without
partially changing builder, observed, marker, or scratch storage.

The cgame audit validates the complete immutable record shape before accepting
a batch. Its externally visible status contains only stream cursor metadata,
reset/accepted/rejected counts, record counts, the normalized chain hash, and
the most recent batch hash. It has no presenter hook and no retained range or
record pointer.

## Deterministic Evidence

`tools/networking/cgame_event_shadow_test.c` covers:

- immutable callback ranges, immediate scratch destruction, and copied-record
  independence;
- reentrant delivery rejection;
- event IDs remaining zero and authority flags remaining absent;
- visible absence/reappearance generation changes, including PVS-style reuse;
- monotonic batch, carrier sequence, and carrier ordinal assignment;
- empty accepted frames, duplicate frames, invalid frames, and explicit reset;
- bytewise failure atomicity for builder, observed, marker, and scratch state;
- world entity exclusion and rejection of unknown legacy values;
- capacity, generation, ordinal, source-tick wrap, and epoch boundaries;
- exact normalized hashing over tick, raw value, entity index, and scan order;
- audit acceptance, rejection, reset, counter, and hash behavior.

The shared C11 and C++20 layout tests pin the 12-byte carrier, 12-byte observed
state, and 72-byte audit-status layouts.

Targeted validation on the Windows Clang build:

```text
clang -std=c11 -Wall -Wextra -Wpedantic -Wconversion \
  -Wsign-conversion -Iinc src/common/net/cgame_event_shadow.c \
  tools/networking/cgame_event_shadow_test.c \
  -o .tmp/event-core/cgame_event_shadow_test.exe
.tmp/event-core/cgame_event_shadow_test.exe

meson setup --reconfigure .tmp/build-event-core
meson compile -C .tmp/build-event-core cgame_event_shadow_test \
  event_shadow_test event_schema_layout_c_test \
  event_schema_layout_cpp_test worr_engine_x86_64 cgame_x86_64
meson test -C .tmp/build-event-core --repeat 3 --print-errorlogs \
  network-cgame-event-shadow network-event-shadow network-event-journal \
  network-event-schema-layout-c network-event-schema-layout-cpp
```

The standalone strict-warning harness passed. The five Meson tests passed for
three repetitions (15/15), and both the production client engine and external
cgame compiled and linked with the bridge.

## Remaining `FR-10-T05` Work

The separately named V2 range now captures typed temp events, muzzle flashes,
spatial sounds, and typed legacy entity events as documented in
`networking-client-cgame-typed-event-range-v2-2026-07-12.md`; V1 remains
frozen. Neither range is a presentation cutover. Remaining work includes:

1. Replace final-state legacy scanning with direct multi-event producer
   submission so same-tick overwrites cannot hide earlier events.
2. Add command-derived prediction keys and authoritative reconciliation for
   predicted weapon and effect events.
3. Carry authoritative immutable ranges through the negotiated WORR snapshot
   transport, with retention and selective receipt policy.
4. Compare server and client normalized audit streams under loss, reorder,
   duplication, PVS churn, reconnect, map transition, and demo seek.
5. Move cgame presentation to authoritative/predicted canonical records only
   after parity is proven, then retire legacy `parse_entity_event` ownership.

No `.install/` refresh was performed for this targeted slice; the root
integration build owns distributable staging after concurrent networking work
settles.
