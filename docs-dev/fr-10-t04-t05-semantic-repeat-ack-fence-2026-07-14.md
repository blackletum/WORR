# FR-10 Semantic Repeat and ACK-Authority Fence

Date: 2026-07-14  
Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T14`

## Outcome

The native event admission foundation now owns the complete ACK-authority
lifecycle for canonical event and event-stream descriptor records. A generic
transport caller can neither commit one of those completed records into the
retained ACK ledger nor refresh its receipt after an
`ALREADY_COMMITTED` observation. Both operations must pass through the event
admission transaction and prove fresh cgame authority.

This closes the prerequisite safety gap identified before connecting a live
server-originated event lane. Production negotiation and behavior are still
unchanged: the public capability mask remains `3`, private readiness remains
`0x13`, and no production hook emits or admits class-3/class-4 native DATA.

## Why the original repeat shape was unsafe

A process-local API that accepted a copied receipt-history identity could
rearm an ACK without observing a new network packet. A second issue was more
subtle: reconciling the entire RX history while refreshing one repeat could
silently restore ACK authority for a different event after cgame had lost that
semantic record. Finally, declining to refresh the repeated receipt was not
enough if an older event ACK still had proactive handoffs remaining when the
cgame endpoint entered resync.

The resulting invariant is stricter:

> Event or descriptor ACK authority exists only after the same transaction has
> observed an exact transport packet and proven the corresponding live cgame
> descriptor/receipt. Losing semantic authority retires every pending semantic
> ACK before another send can prepare it.

## Architecture

### Initial commit fence

`Worr_NativeCarrierSessionCommitRetainedV1` remains the public transport
bridge for command and snapshot records. A completed event or event-stream
descriptor slot now returns
`WORR_NATIVE_RX_SEMANTIC_ADMISSION_REQUIRED` byte-identically.

`Worr_NativeEventAdmissionCommitCompletedV1` uses a common-net-private commit
primitive on staged state. It is the only production retained-ledger bridge
allowed to authorize a semantic commit. It publishes the staged RX session,
slots, and ACK ledger only after the existing descriptor reset/status or event
submit/status proof succeeds. The private primitive rejects non-semantic
records, keeping its authority narrow.

### Observation-bound repeat transaction

`Worr_NativeEventAdmissionRevalidateCommittedRepeatV1` now accepts the exact
WTC1 packet, DATA-entry index, caller clock, live RX objects, and cgame
consumer. It performs the following bounded transaction:

1. Validate disjoint ownership, session binding, reserved capability bit 5,
   active event owner, payload-arena geometry, and ACK-ledger availability.
2. Decode WTC1 and WNE1 without mutating state.
3. Match record reference, whole-message payload CRC, payload size,
   fragmentation stride/count, priority, transport epoch, and message sequence
   to one exact committed RX-history entry.
4. Run the raw DATA acceptor on private session/slot copies and require the
   newly observed result to be exactly `WORR_NATIVE_RX_ALREADY_COMMITTED`.
5. Derive the one-message repeat acknowledgement internally and refresh only
   that exact ledger identity on a private copy.
6. Query the cgame endpoint synchronously. A descriptor repeat must match the
   active semantic epoch and baseline; an event repeat must additionally exist
   in the fresh selective receipt.
7. Publish the staged session, slots, and ledger together. No reset or submit
   callback is invoked on repeat success.

The nonmutating history precheck is important because the raw RX acceptor owns
caller-provided payload storage for new fragments. Proving that the record is
already committed before invoking it guarantees the live arena cannot be
written on a rejected new-message attempt. The proof uses whole-message
identity and therefore supports any one fragment of an already committed
multi-fragment message; it does not require the complete payload to survive.

### Generic repeat fence

`Worr_NativeCarrierSessionAcceptDataRetainedV1` still handles command and
snapshot repeats. When the exact retained class is event or descriptor, it now
returns
`WORR_NATIVE_CARRIER_SESSION_SEMANTIC_REVALIDATION_REQUIRED` without publishing
the staged RX observation, touching outputs, or refreshing the ledger.

### Exact-only receipt capture

ACK-ledger reconciliation no longer imports every identity retained by the RX
session. Commit and repeat paths now:

- prune receipts whose exact RX authority has expired;
- insert or refresh only the message sequence proven by the current operation;
- leave every other absent or exhausted identity unchanged.

This prevents an unrelated command repeat from reauthorizing an event or
descriptor receipt.

### Fail-closed resync

Any post-callback or repeat-status uncertainty still advances the event owner
into its resync barrier and best-effort resets the consumer. It now also
retires every event/descriptor receipt from the live ACK ledger while
preserving command and snapshot receipts. Session, slot, and payload state
remain uncommitted. Consequently, `PeekDue`/prepare cannot emit a stale
semantic ACK after cgame authority was scrubbed.

One event owner must stay attached to the same cgame activation lineage.
Replacing or resetting that endpoint out of band requires quiescing admission
and resetting/advancing its connection state. Retired transport banks may
release late outbound ACKs only; they must never admit semantic DATA or
semantic repeats.

## Validation

Focused coverage exercises the public initial-commit and generic-repeat gates,
exact-only ledger isolation, descriptor and event repeat success, no
reset/submit callback on repeat success, stale/missing cgame receipt resync,
semantic receipt retirement, active-emission serialization, unsupported
classes, hostile new-fragment rollback, and fragmented committed repeats.

Validation passed on the current Windows Clang build:

- `meson test -C builddir-win network-native-carrier-ack
  network-native-event-admission --print-errorlogs`: 2/2;
- the event-admission target's dedicated three-repeat check: 3/3;
- `meson test -C builddir-win --suite networking --print-errorlogs`: 118/118;
- `meson test -C builddir-win --suite networking --repeat 3
  --print-errorlogs`: 354/354;
- `meson compile -C builddir-win`: passed, including the production dedicated
  engine relink against the changed common cores;
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64`: passed with 16
  root runtime files, one dependency, 329 packaged assets, 31 botfiles, and
  215 RmlUi assets;
- build/stage SHA-256 equality: client engine
  `D3B937A3A2FF08FA04550138D2712AEFD1CEAD62E1800922DF15D89FBA4F0BBC`,
  dedicated engine
  `1F536F265CA087E8208F5BACD84E2BE80E0D8F4E437D3A0C5618A76D94DC5FDB`,
  cgame `ADB180DE2C0CF4D40F09C005B37D6AD341DA80BEAF2CA67DB6EDF44D85F9C142`,
  and sgame `24ACA8B141E42DCE2FBB4F53391E2A9D8FBC170F9C6ADD59A772BAF78058214F`.

The independent repeat-invariant audit found no blocking gap. `q2proto/`
remains byte-untouched.

## Scope and next critical path

This slice deliberately does not enable class-3/class-4 production traffic and
does not change `q2proto/`. The next `FR-10-T04/T05/T16` slice can now build the
default-off per-peer event producer and full-duplex mixed carrier without an
ACK-authority bypass. It still needs the explicit client-active confirmation,
visibility-safe final-emission event projection, descriptor-before-event ACK
gate, idle ACK/DATA wakes, current/retired-bank lifecycle, and headless
directional impairment evidence.

`FR-10-T04`, `FR-10-T05`, and `FR-10-T14` remain In Progress.
