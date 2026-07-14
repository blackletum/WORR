# FR-10-T04 Native Packet Carrier Trailer Core

Date: 2026-07-13  
Primary project task: `FR-10-T04`  
Supporting tasks: `FR-10-T14`, `FR-10-T16`  
Status: WTC1 carrier core complete and now used by the default-off one-command
production observation linked below; general client/server adapters,
capability advertisement, and dual-adapter acceptance remain open

## Outcome

WORR now has a bounded, allocation-free packet carrier for transporting the
existing WNE1 native envelope and its exact message receipts alongside an
unchanged legacy application payload. This is the first concrete wire bridge
between the isolated native envelope/session work and future live adapters.

At this core slice's original validation point, the carrier was intentionally
not connected to a production packet path:

- `WORR_NET_CAP_NATIVE_ENVELOPE_V1` remains reserved and unadvertised;
- no client, server, demo, cgame, or sgame path emitted or consumed WTC1;
- legacy Q2/q2proto traffic remains the only live authority;
- `q2proto/` is unchanged; and
- no fallback or compatibility decision is made by this library. A future live
  adapter must invoke it only under the confirmed connection capability and
  transport epoch.

The public pointer-free contract is
`inc/common/net/native_carrier.h`; the strict C11 implementation is
`src/common/net/native_carrier.c`.

## Hybrid packet architecture

WTC1 uses a hybrid transition shape: an optional byte-for-byte legacy prefix,
followed by typed native entries, followed by one fixed terminal footer. This
lets a future shadow adapter preserve the established legacy projection while
carrying native canonical data and receipts in the same bounded application
datagram. It does not require a change inside `q2proto/` and it does not define
a second command, event, or snapshot model.

The prefix means the complete final application stream assembled inside the
netchan, including any pending reliable bytes prepended ahead of the caller's
unreliable message. `NetchanNew_Transmit` currently performs that concatenation
after its caller supplies `data`; wrapping only `data` would make the footer's
`legacy_bytes` disagree with the received `reliable || data || WTC1` packet.
Live integration therefore requires a reviewed sideband-aware post-assembly
hook (or an equivalent assembly API), not a wrapper bolted around `msg_write`
before netchan transmission.

The complete legacy-prefix-plus-carrier packet is capped at 1,200 bytes. That
is an absolute carrier ceiling, not permission to exceed a direction's live
netchan application budget. The client currently configures a 1,024-byte
ceiling while the server uses its negotiated `params.maxlength`; a future
adapter must use `min(1200, netchan.maxpacketlen)` before reserving the full
legacy prefix and carrier overhead. The carrier accepts at most eight entries.
An encoder must retain any entry that cannot fit; it may not split or truncate
one to meet the cap.

### Entry framing

Every entry begins with this eight-byte little-endian header:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 1 | entry type |
| 1 | 1 | flags, required to be zero |
| 2 | 2 | complete entry byte count |
| 4 | 4 | reserved, required to be zero |

Two entry types are accepted:

- `DATA_V1` appends exactly one complete WNE1 datagram. Nested envelope decode
  must succeed, and its non-zero transport epoch must equal the WTC1 footer
  epoch. The datagram is atomic at this layer.
- `ACK_V1` is exactly 16 bytes and appends an inclusive, non-empty
  `[first_message_sequence,last_message_sequence]` range. It acknowledges only
  those exact sequences; it does not imply receipt outside the declared range.
  Multiple ACK entries may represent disjoint exact ranges.

### Fixed footer

The 32-byte footer is always the final packet region:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 2 | WTC1 wire version, exactly 1 |
| 2 | 2 | footer bytes, exactly 32 |
| 4 | 4 | carrier bytes, entries plus footer |
| 8 | 4 | exact legacy-prefix byte count |
| 12 | 4 | non-zero confirmed transport epoch |
| 16 | 2 | entry count, 1 through 8 |
| 18 | 2 | flags, required to be zero |
| 20 | 4 | carrier CRC-32 |
| 24 | 8 | terminal marker `WORRWTC1` |

The decoder first inspects only the final eight bytes. An absent marker returns
`NO_CARRIER` without probing legacy contents. Once the complete marker matches,
the packet is a WTC1 candidate: malformed, unsupported, corrupt, over-limit,
or epoch-inconsistent input fails explicitly and is never silently
reinterpreted as legacy traffic.

## Integrity and security boundary

The reflected IEEE CRC-32 covers every carrier entry and the complete footer,
with the stored CRC field treated as zero. The legacy prefix is intentionally
excluded; the footer binds its exact length, not its contents. A golden packet
test pins this byte image and proves that changing only a legacy byte leaves
the carrier CRC valid and WTC1 decoding successful.

CRC-32 is corruption detection, not authentication. WTC1 therefore does not
grant authority by itself. The future adapter must require the confirmed
capability/epoch, normal sequenced netchan admission, WNE1 validation, codec
identity validation, and canonical store/journal validation before committing
native data. Legacy payload integrity and authority remain properties of the
existing live path until a separately reviewed cutover.

The strict footer and eight-byte marker reduce accidental suffix ambiguity,
while fail-closed candidate handling prevents malformed WTC1-shaped data from
downgrading itself after recognition.

## Ownership and transactional behavior

The API retains no pointer and allocates no memory. Decoded DATA locations are
packet-relative offsets in a fixed 256-byte view. A successful view is valid
only while the caller-owned immutable packet remains available.

Encoding stages the complete packet in a fixed 1,200-byte stack image before
commit. This permits overlap with legacy and DATA byte inputs, including
in-place replacement. The output-size pointer may not overlap the encoded
packet range. Invalid input, insufficient capacity, alias rejection, or any
other encode failure leaves both outputs byte-identical.

Decode similarly builds a local view, validates the entire carrier and every
nested WNE1 datagram, requires the entry walk to end exactly at the footer, and
commits only after success. `NO_CARRIER` and every failure leave the output
view byte-identical.

The exact complete-packet boundaries are proven in both useful extremes:

- a 1,040-byte legacy prefix plus eight 16-byte ACK entries plus the 32-byte
  footer is exactly 1,200 bytes; and
- no legacy prefix plus one eight-byte DATA header, one 1,160-byte WNE1
  datagram, and the footer is exactly 1,200 bytes.

Consequently, a future envelope fragmenter must use the carrier's effective
inner budget rather than assuming every stand-alone 1,200-byte WNE1 datagram
can be wrapped. With a 1,200-byte direction budget, one DATA entry, and no
legacy prefix, that budget is 1,160 bytes; a smaller netchan budget, a non-empty
legacy prefix, or ACK entries lower it further. With one DATA and one ACK, the
WNE1 budget is `application_budget - legacy_reserve - 48` and must still fit
the 56-byte WNE1 header plus at least one payload byte. A retained message's
fragment stride is frozen at enqueue and cannot be silently replanned later.

## Validation evidence

Meson registers:

- `network-native-carrier` / `native_carrier_test.exe`; and
- `network-native-carrier-layout` / `native_carrier_layout_test.exe`.

Current Windows x86_64 Clang 20 evidence:

- the focused matrix passed 2/2 and three focused repetitions passed 6/6;
- the complete 86-test networking suite passed 86/86;
- three consecutive complete repetitions passed 258/258;
- strict C11 and C++20 builds passed with
  `-Wall -Wextra -Wpedantic -Werror`;
- the behavioral test passed under AddressSanitizer plus
  UndefinedBehaviorSanitizer;
- Clang static analysis completed cleanly for the carrier and its behavioral
  test; and
- the C11 implementation/test plus C++20 layout consumer passed independent
  `i686-pc-windows-msvc` warning-clean syntax/layout builds.

Behavioral coverage includes mixed DATA/ACK round trips, byte-exact golden
encoding, explicit CRC-domain behavior, all exact 1,200-byte boundaries,
eight-entry limits, in-place encoding, transactional failures, every
truncation that retains the terminal marker, corruption, unsupported versions
and types, reserved fields, inconsistent sizes/counts, invalid ACK ranges,
nested WNE1 corruption/version errors, epoch mismatch, alias rejection, and
`UINT32_MAX` ACK endpoints. C++ layout checks pin standard-layout,
trivially-copyable ABI sizes and offsets.

An independent source/API/wire review reported no P1/P2 correctness, security,
API, or wire-format findings. Its optional golden-vector and CRC-domain test
hardening was incorporated before the final 258/258 run.

Validation scratch artifacts are under
`.tmp/networking/native-carrier-validation/`.

## Exact remaining work at core validation

1. The isolated carrier DATA scheduling, transport-confirmed session accounting,
   and retained ACK-only integration are now complete in
   `docs-dev/fr-10-t04-transport-confirmed-session-carrier-and-receipts-2026-07-13.md`.
   Connect those cores to the reviewed live post-assembly boundary.
2. Add a default-off command-shadow adapter: preserve legacy command authority,
   add the carrier at a boundary that sees the complete post-netchan-assembly
   legacy prefix, decode before q2proto ownership, compare both canonical
   records, and return exact native receipts.
3. Add snapshot and authoritative-event adapters only after command-shadow
   parity proves lifecycle, downgrade, reconnect, corruption, and queue
   behavior.
4. Define the bounded native snapshot baseline/delta or range strategy needed
   for accepted canonical snapshots that exceed one codec/envelope payload.
5. Add deterministic loss, duplication, reorder, ACK-loss, PMTU, reconnect,
   mixed-version, and legacy/native semantic parity reports.
6. Complete the per-decoder 100,000-malformed-case, bandwidth, load, soak,
   cross-platform, rollback, and release gates required by `FR-10-T14/T15`.
7. Advertise `WORR_NET_CAP_NATIVE_ENVELOPE_V1` only after all compatibility
   and staged-release evidence is accepted.

## Production integration update (2026-07-14)

WTC1 is now connected default-off for one production command DATA entry and
one reverse ACK-only range. The client transaction is exactly 206 native bytes
(`WNC1` 110, complete `WNE1` 166); a legacy prefix of 818 bytes fits the
1,024-byte application ceiling and 819 bypasses. One ACK range costs 48 bytes,
so 976 fits and 977 bypasses. The unchanged prefix remains authoritative and
budget misses retry later. General adapters and mixed DATA/ACK remain open; see
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
