# FR-10-T04 Native Command Shadow Core

Date: 2026-07-14

Tasks: `FR-10-T04`, `FR-10-T09`, `FR-10-T16`

Status: Core implemented and tested, then connected to the default-off one-
command production observation linked below. It has no capability advertisement
or simulation authority.

## Purpose and authority boundary

The first live WTC1 DATA path will carry client commands while the existing
legacy MOVE/BATCH_MOVE decode and server command stream remain authoritative.
The native path must prove delivery, retention, and parity before it can replace
any input source. This core provides the bounded state needed for that shadow
phase without creating a second command schema.

All records use the existing `worr_command_record_v1` validator and the existing
110-byte `WNC1` command codec. Native message sequence and payload handle remain
transport/storage identities only; neither is equated with the canonical
`{epoch, sequence}` command identity.

## Client record builder

The epoch-local builder accepts only the next exact nonzero command sequence.
It validates and canonicalizes the existing prediction-command payload, uses
the shared prediction model revision, and advances a checked cumulative
command-end sample clock. Duration multiplication and sample-time addition are
overflow checked, and sequence `UINT32_MAX` is terminal.

The initial native shadow cannot honestly reproduce the server's render
watermark: the finalized client command does not yet know which admitted legacy
MOVE will establish that server context. Builder output therefore sets
`WORR_COMMAND_RENDER_PROVENANCE_NONE`. It does not fabricate packet ACK or
render-time authority.

Build failure never advances command sequence/sample time and never writes the
candidate record. Rejection counters may change and saturate.

## Immutable payload registry

A caller-owned 64-slot registry encodes and retains the exact 110 codec bytes.
Each nonzero handle packs a six-bit slot index and a nonzero generation. The
registry provides exact byte/command-ID copy-out and exact-handle release.

Important lifetime rules are encoded rather than left to convention:

- an occupied slot is immutable;
- a released generation makes its old handle permanently stale;
- a slot is never reused while occupied;
- the final representable generation retires the slot permanently instead of
  wrapping to a stale handle;
- capacity stalls do not disturb the legacy command path.

The future client TX session may retain only registry handles whose payload
slots remain occupied. ACK reconciliation must release a handle only after no
retained TX slot references it.

## Honest comparator

The comparator checks only the information available at both endpoints:

1. exact canonical command ID;
2. exact canonical prediction-command semantic fields;
3. a connection-constant sample-time offset.

Both inputs first pass the shared canonical-record validator. V1 admits only
`WORR_PREDICTION_MODEL_REVISION`, so an unsupported or differing model revision
is an invalid record rather than a reachable comparison result. The report
still records both accepted revision values for audit clarity.

The first otherwise matching pair establishes an unsigned magnitude plus
explicit direction (`equal`, `legacy ahead`, or `native ahead`). Every later
pair must reproduce that offset. Direction avoids signed overflow at the
`uint64_t` boundaries.

Every successful report sets both
`WATERMARK_UNVERIFIED` and `FULL_RECORD_PARITY_NOT_CLAIMED`. This is a deliberate
promotion guard, not missing telemetry. Full semantic record parity requires a
negotiated stream sample-time baseline and a defined client render-watermark
capture rule.

## Native/legacy join

The fixed 64-slot join is keyed only by canonical command ID. It accepts either
arrival order, retains the first validated native and legacy halves, treats an
equal canonical semantic half idempotently, and classifies different semantic
content for an occupied side as a conflict without replacement. This includes
signed-zero equivalence and the command ABI's diagnostic-only legacy packet-
shared watermark timing; the first observation remains retained. The second
distinct side runs the shared comparator once and stores its immutable report.

Slots use caller-supplied monotonic 64-bit ticks. Explicit pruning removes any
slot whose age reaches the configured expiry; clock regression fails without
pruning or replacement. Full capacity stalls rather than evicting unobserved
evidence. The future adapter can therefore distinguish native-first,
legacy-first, retry, conflict, mismatch, expiry, and backpressure without
holding an RX reassembly slot open while waiting for legacy parsing.

The intended server ordering is:

1. preflight a join slot;
2. decode and validate the completed native command codec;
3. commit the native RX message and its exact receipt;
4. publish the native join half;
5. after the authoritative legacy adapter succeeds, publish each copied legacy
   record before its simulation-consume loop.

Native observations remain diagnostic in this stage. A mismatch records
evidence; it does not alter simulation or silently reinterpret bytes.

## Wire budget

One unfragmented command adds 206 bytes beyond the unchanged legacy prefix:

```text
 56  WNE1 envelope header
110  WNC1 command record
  8  WTC1 DATA entry
 32  WTC1 footer
---
206 bytes
```

At the current 1,024-byte client application ceiling, one command fits only
when the complete post-netchan legacy prefix is at most 818 bytes. Each
piggybacked ACK range consumes another 16 bytes. The TX callback must calculate
against `min(info.max_application_bytes, 1200)` and return token-free `BYPASS`
when the selected data does not fit; netchan fragmentation is not a fallback
for WTC1.

## Validation

Focused C and C++ tests cover:

- builder order, epoch, duration, zero-duration, sample overflow, sequence
  exhaustion including the accepted `UINT32_MAX - 1 -> UINT32_MAX` terminal
  transition, failure transactionality, and canonical record output;
- exact 110-byte codec decode, registry capacity, round-robin reuse, stale
  handles, final-generation retirement, copy/output bounds, and aliases;
- comparator ID/command mismatch, unsupported-model rejection, initial offset
  establishment only after a matching pair, both offset directions, the
  `0`/`UINT64_MAX` arithmetic boundary, offset drift, signed-zero normalization,
  and explicit watermark/full-parity disclaimers;
- native-first and legacy-first join, semantic duplicate and conflicting-
  duplicate behavior, mismatch reports, stable lookup, expiry boundary, clock
  regression, capacity stalls, pruning, counter saturation, and fixed layouts;
- output/owner alias rejection without telemetry mutation, report-to-retained-
  record consistency, offset recomputation, and duplicate-key rejection during
  deep state validation.

Configured Meson behavior/layout tests pass. Strict C/C++, ASan/UBSan, and i686
source/layout checks also pass.

## Historical remaining live work at core validation

This core does not yet observe `CL_FinalizeCmd`, own a per-connection payload
registry, enqueue a TX session record, decode admitted server DATA, observe
legacy stream records, or schedule ACK-only traffic. Those adapters remain
behind the native endpoint-readiness barrier. The native capability stays
unadvertised until both endpoints install all resources, complete the explicit
readiness proof, and pass byte-identity, parity, impairment, reconnect, demo,
MTU, capacity, and load gates.

## Production integration update (2026-07-14)

The builder, immutable payload registry, comparator, and join are now connected
to the default-off production pilot for one observational command per private
transport epoch. The client observes finalized commands, selects the newest
member of the exact encoded legacy range, and the server joins either arrival
order without granting native authority. Thus the first paragraph under
"Remaining live work" is historical for this one-shot scope; repeated native
delivery and full parity remain open. See
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
