# Canonical Event-Stream Descriptor and Transactional Admission Core

Date: 2026-07-14

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T07`, `FR-10-T14`

Follow-up: the later
`docs-dev/fr-10-t04-t05-semantic-repeat-ack-fence-2026-07-14.md` milestone
closes this document's then-open `ALREADY_COMMITTED` semantic-revalidation and
ACK-authority gap. Statements below describe the scope at this milestone's
original completion boundary.

## Outcome

WORR now has an explicit canonical event-stream authority contract and a
transport-neutral, transactional client admission core. The event epoch is no
longer something a future adapter must infer from a transport epoch, snapshot
epoch, server count, or connection counter. A reliable descriptor must
establish the semantic stream before any authoritative event record can be
admitted.

This is a production-linked common-core milestone, not a live protocol
promotion. The new event-stream capability is known but remains outside the
public legacy-stage mask. The private production readiness proof still binds
`0x13`, so no current connection negotiates or emits descriptor/event DATA.
Legacy packets, demos, presenters, and gameplay authority are unchanged.
No file under `q2proto/` changed.

## Why a separate semantic descriptor is required

Transport retention and event authority have different lifetimes:

- a transport epoch can rotate while the same event stream is retransmitted;
- a map or authority reset can require a new event epoch without reusing a
  transport identity;
- snapshot epochs describe immutable state lineage, not event receipt or
  presentation identity; and
- a full reconnect may legitimately reuse a lower numerical event epoch only
  after all process-local connection provenance has been replaced.

Binding these domains together would make retransmission, reconnect, demo seek,
and hard-resync behavior ambiguous. The canonical descriptor therefore carries
only semantic event state. Connection and transport provenance remain in the
native session binding and endpoint owners.

## Canonical descriptor

`worr_event_stream_descriptor_v1` is a fixed 24-byte value:

| Field | Meaning |
|---|---|
| `struct_size` / `schema_version` | Exact V1 ABI identity |
| `flags` | Must be zero in V1 |
| `stream_epoch` | Nonzero canonical event-authority epoch |
| `first_sequence` | Nonzero first admissible event sequence |
| `event_schema_version` | Exact canonical event ABI version |
| `model_revision` | Exact canonical event model revision |

`{0, 0}` is deliberately invalid on the wire. It remains a process-local
cgame teardown command only. Initialization is transactional, validation is
strict, equality is defined only for two valid descriptors, and static layout
assertions pin its size and offsets in C and C++.

The native codec carries the descriptor as reliable record class 4. Its exact
56-byte `WNC1` image consists of the existing 48-byte header plus an 8-byte
body. The envelope identity is exactly `{stream_epoch, first_sequence}`. It is
independent of the enclosing WNE1 message sequence and transport epoch.

## Capability policy

`WORR_NET_CAP_NATIVE_EVENT_STREAM_V1` is bit 5. It is included in the known
mask so private proof objects and tests can model the endpoint, but it is not
included in `WORR_NET_CAP_LEGACY_STAGE_MASK` and is not emitted by the current
readiness pilot.

Completed-message admission takes the exact immutable
`worr_native_session_binding_v1` used to establish endpoint provenance. It
requires both the native-envelope capability and the event-stream capability,
then binds the proof to the RX session, ACK ledger, transport epoch, and
connection-owner ID. A generic envelope-only binding returns
`WORR_NATIVE_EVENT_ADMISSION_NOT_NEGOTIATED` without mutating state or invoking
the cgame consumer.

## Connection owner and epoch rules

The 56-byte `worr_event_stream_owner_v1` is process-local and never serialized.
It retains:

- the active descriptor, if any;
- the highest event epoch observed for this connection incarnation;
- a mutation generation with fail-closed exhaustion; and
- a nonzero connection-owner ID.

Exact descriptor duplicates are idempotent. A descriptor with the active epoch
but different fields is a conflict. Lower epochs are stale. A resync scrubs the
active descriptor while retaining the epoch high-water, so recovery requires a
strictly newer descriptor. Only initialization with a fresh connection-owner ID
is a full reconnect and may reuse a lower numeric event epoch.

The penultimate mutation generation is the last one allowed to activate new
authority. The final generation is reserved for an irreversible-callback
failure to establish a resync barrier. An imported active owner that is already
generation-exhausted is rejected before any consumer callback.

## Transactional completed-message admission

`Worr_NativeEventAdmissionCommitCompletedV1` accepts only an exact retained
`MESSAGE_COMPLETE` proof. It owns no heap storage and uses fixed stack copies of
the owner, RX session, up to 16 slots, and ACK ledger.

The operation is ordered as follows:

1. Validate distinct input ranges, the trusted callback table, immutable
   binding, owner/session/ledger provenance, complete-slot identity, payload
   bounds, codec header, and exact envelope-to-codec record identity.
2. Require the event-stream capability and a non-exhausted semantic owner.
3. Decode only descriptor class 4 or event class 3. Other valid canonical
   classes are unsupported and remain uncommitted.
4. Apply retained RX commit and exact ACK-ledger mutation to private copies.
   If that precommit cannot succeed, return retryable without calling cgame.
5. Invoke the irreversible cgame operation only after the transport mutation
   is proven possible.
6. Fetch a fresh cgame status immediately after the operation. Never use a
   cached receipt.
7. Publish the staged session/slots/ledger only when fresh status proves the
   exact semantic result.

Descriptor activation calls `ResetAuthority(epoch, first)` and requires a
fresh active status with zero authority count, an empty selective mask, exact
next-presentation sequence, and `highest_contiguous == first - 1`. An exact
descriptor duplicate does not reset cgame, but it still requires fresh active
status at the correct epoch and at or beyond the descriptor baseline.

Event admission requires an active descriptor, exact stream epoch, and a
sequence at or above `first_sequence`. It checks active status before submit,
submits one canonical record through the engine-owned cgame endpoint, fetches
status again, and requires the exact event ID in the fresh receipt before the
transport commit and ACK become visible. Accepted reconciliation results map to
accepted, duplicate, or degraded admission results.

If any post-callback proof fails, transport state stays byte-identical and no
ACK is created. The event owner burns the attempted epoch, enters a resync
barrier, and best-effort scrubs cgame. The callback's process-local `opaque`
cookie must be non-null and directly disjoint from every admission argument;
the caller additionally guarantees that trusted callbacks hold no hidden alias
capable of mutating those objects.

## Engine/cgame lifecycle alignment

The engine remains the sole caller of the cgame event-runtime export. The
native consumer table routes through that engine owner rather than exposing the
DLL table directly.

Two zero-reset meanings are now separated:

- map/serverdata quiesce scrubs attached cgame authority, retains the
  connection-lifetime epoch high-water, and requires a newer descriptor; and
- full disconnect clears the high-water and permits a fresh connection to
  reuse lower numerical epochs.

`CL_ClearState` uses quiesce. `CL_Disconnect` performs the additional full
connection reset. Admission recovery maps its local `{0,0}` callback to
quiesce, preventing the inner engine owner from losing an epoch that the outer
event-stream owner has already fenced.

Any invoked nonzero reset is irreversible even if the cgame callback returns a
failure or reports incoherent status. The engine now burns that attempted epoch
before quarantining the consumer. This keeps direct descriptor observation and
future native admission on the same monotonic fence for first activation and
active-stream replacement failures.

## Why the existing server event shadow is not the live producer

The current server event shadow allocates global IDs before per-client
visibility filtering and exposes diagnostic newest-record queries rather than
per-peer reliable retention. Broadcasting every record would leak events a
client should not observe. Selectively omitting records would create sequence
gaps that an ordered receiver cannot distinguish from loss.

The live sender must therefore use a per-peer projected stream, or a future
explicit routing/gap model with equivalent privacy and ordering proof. This
milestone deliberately does not connect the global diagnostic shadow to native
DATA.

## Implementation inventory

- `inc/shared/event_stream.h` and `src/common/net/event_stream.c`: canonical
  descriptor ABI and validation.
- `inc/shared/native_envelope.h` and `src/common/net/native_envelope.c`: class-4
  record identity.
- `inc/common/net/native_codec.h` and `src/common/net/native_codec.c`: exact
  descriptor codec, inspection, and record-ref projection.
- `inc/common/net/capability.h`: reserved event-stream capability bit.
- `inc/common/net/event_stream_owner.h` and
  `src/common/net/event_stream_owner.c`: connection-local descriptor owner.
- `inc/common/net/native_event_admission.h` and
  `src/common/net/native_event_admission.c`: transactional completed-message
  admission.
- `src/common/net/native_carrier_ack.c`: exact receipt support for class 4.
- `inc/client/cgame_event_runtime.h` and
  `src/client/cgame_event_runtime.cpp`: descriptor observation, map quiesce,
  disconnect reset, native callback table, and aligned epoch fencing.
- `src/client/main.cpp`: correct map-transition versus disconnect boundaries.
- `meson.build`: production linkage and five new networking proof targets.

## Validation

Five new networking tests cover descriptor behavior/layout, codec wire bytes,
owner lifecycle, and transactional admission:

- `network-event-stream`;
- `network-event-stream-layout-c`;
- `network-event-stream-layout-cpp`;
- `network-event-stream-owner`; and
- `network-native-event-admission`.

The admission matrix covers descriptor activation, duplicate and conflict;
event accept, duplicate and degraded results; descriptor and first-sequence
gates; exact codec/envelope identity; unsupported canonical classes; ACK
precommit retry; reset/submit/status/receipt failure recovery; capability
isolation; regressed baselines; generation exhaustion; direct alias rejection;
and byte-identical transport rollback. Existing cgame-owner coverage now also
proves map quiesce, native callback routing, failed first activation, failed
replacement, attempted-epoch retention, and reconnect-only epoch reuse.

Final local evidence:

- focused descriptor/admission/owner selection: 6/6;
- complete networking suite: 118/118;
- three complete repetitions: 354/354;
- complete Windows Clang production build: pass;
- refreshed `windows-x86_64` `.install`: pass;
- staged runtime: 16 root runtime files plus one dependency;
- `basew/pak0.pkz`: 329 assets, including 31 botfiles and 215 RmlUi assets;
- build/stage SHA-256 equality: client engine, dedicated engine, cgame, and
  sgame all exact.

## Explicit limits and next work

This milestone does not complete `FR-10-T04`, `FR-10-T05`, `FR-10-T07`, or
`FR-10-T14`.

- No live readiness exchange negotiates bit 5.
- No production hook emits or admits descriptor/event DATA.
- The existing live hooks are direction-specific; server-originated DATA needs
  full-duplex mixed DATA+ACK integration for current and retired banks.
- `ALREADY_COMMITTED` transport repeats are not yet semantically revalidated
  against a fresh cgame receipt after authority loss. They must not be blindly
  re-ACKed by the future live adapter.
- There is no per-peer authoritative event projection/retention producer.
- Direct sgame multi-event and cgame predicted local-action producers remain
  absent.
- Legacy effect/audio presenters remain authoritative; the canonical runtime
  presenter is still no-effects.
- Native snapshot parity, broad impairment, demo/spectator, load, malformed,
  soak, and cross-platform evidence remain open.

The next safe slice is a per-peer authoritative event-stream producer plus
full-duplex mixed-carrier integration in default-off shadow. The server must
retain and retransmit the exact descriptor until its transport ACK, send no
event DATA before that ACK, retain reliable event payloads per peer, and expose
enough routing semantics that visibility filtering cannot create unexplained
sequence gaps. The client must apply completed DATA through this admission core
and revalidate every repeat before authorizing another ACK. Only after that
path passes loss/reorder/duplication, reconnect/map-rotation, privacy, budget,
and present-once parity gates should bit 5 be added to any live readiness mask.
