# FR-10-T05/T07 cgame canonical event presentation journal

Date: 2026-07-13  
Project tasks: `FR-10-T05`, `FR-10-T07`  
Related tasks: `FR-10-T06`, `FR-10-T13`

## Purpose

The typed cgame event-range extension previously validated each transient
engine callback and then discarded its records. This change adds a cgame-owned,
bounded, value-only presentation journal behind that same validated feed.

Legacy temp-entity, muzzle, spatial-audio, and entity-frame presenters remain
authoritative. The journal is the durable ordering and lifetime layer required
before those presenters can be progressively replaced without retaining decode
buffers or dispatching an event twice.

## Ingestion contract

The V2 range consumer now performs these operations in order:

1. Validate the complete stream epoch, carrier sequence, batch generation,
   arrival ordinal, chunk flags, carrier kind, record shape, entity generation,
   and semantic chain through `Worr_CGameEventRangeAuditConsumeV2`.
2. Compute every record's canonical semantic hash before mutating the journal.
3. Copy accepted records and carrier metadata into cgame-owned storage.
4. Account for accepted empty carriers and rejected adapters even though they
   intentionally create no presentation entry.

No `range` or `records` pointer survives the callback. Tests overwrite the
producer scratch arena after delivery and prove the retained record is
unchanged.

## Ordering and bounded lifetime

The journal holds 2,048 presentation entries. Each entry records:

- a cgame-local monotonic journal serial;
- the producer stream epoch and batch generation;
- carrier sequence and exact arrival ordinal;
- carrier tick and simulation time;
- phase, flags, carrier kind, and adapter status;
- canonical semantic hash; and
- a complete pointer-free `worr_event_record_v1` value.

Producer ordering remains authoritative: the V2 range audit requires contiguous
carrier and arrival sequences before the journal accepts a range. Duplicate
entity frames are rejected by the producer before delivery. Authoritative IDs
and prediction keys remain part of the copied record when native producers
supply them; the journal does not fabricate either for legacy carriers.

When full, the ring overwrites its oldest entry and increments an explicit
counter. A reader whose next serial was overwritten receives
`CG_CANONICAL_EVENT_PRESENTATION_CURSOR_OVERRUN`; it is never silently advanced.

## Reader API

The cgame-private API provides:

- `CG_CanonicalEventPresentationBegin`: cursor at the oldest retained entry;
- `CG_CanonicalEventPresentationTail`: cursor after the newest entry;
- `CG_CanonicalEventPresentationNext`: copied next entry plus copied advanced
  cursor; and
- `CG_CanonicalEventPresentationGetStatus`: journal and V2 range-audit
  telemetry in one value record.

`CG_CanonicalEventPresentationAdvanceAudit` is the first presentation-clock
consumer. It advances a single ordered, present-once shadow cursor only while
the head record's simulation time is at or before the selected canonical render
time. A future head blocks later records, preserving producer order. Overrun is
reported, rebased to the oldest retained record, and counted before any later
drain. The live cgame frame calls this scheduler after selecting its immutable
snapshot pair; no wire parser is involved in that decision.

Cursors carry the stream epoch and journal generation. Consumer attach, client
reset, demo restart/rewind, or local serial-space reset makes earlier cursors
explicitly stale. Empty, stale, overrun, corrupt, and invalid cases are distinct
results.

## Verification

`tools/networking/cgame_event_presentation_test.cpp` drives the real V2 builder
into the real cgame consumer and verifies:

- ordered frame-event delivery and arrival ordinals;
- complete value ownership after producer scratch corruption;
- empty carrier accounting;
- duplicate frame suppression;
- multi-record ordering;
- invalid range rejection without publication;
- stream-reset cursor invalidation;
- deterministic overwrite/overrun behavior after 2,049 records;
- future-event blocking at the canonical render time; and
- ordered one-time audit dispatch plus explicit overrun recovery.

The focused executable compiled with Clang C11/C++20 strict floating-point
options and passed:

```text
cgame canonical event presentation tests passed
```

The registered Meson target/test name is
`network-cgame-event-presentation`. After the independent server snapshot
source landed, the regenerated production cgame DLL and both focused targets
built successfully. The registered combined run passed 2/2:

```text
network-cgame-event-presentation  OK
network-server-snapshot-shadow    OK
```

## Remaining promotion work

- Add canonical adapters that invoke cgame presentation functions for each
  payload kind without reparsing wire messages.
- Correlate predicted and authoritative records by prediction key and cancel or
  confirm presentation without replaying audio/effects.
- Extend the now-live canonical render-clock readiness seam with expiry,
  predicted confirmation/cancellation, and full demo-rate/seek evidence.
- Run legacy-versus-canonical effect/audio parity under loss, duplicate packets,
  frame gaps, entity reuse, and demo seek before suppressing any legacy
  presenter.
- Record native authoritative event IDs in snapshots and demos; legacy records
  remain explicitly ID-less.
