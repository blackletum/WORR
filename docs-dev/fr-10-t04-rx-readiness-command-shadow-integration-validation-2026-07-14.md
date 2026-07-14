# FR-10-T04 RX, Readiness, and Command-Shadow Integration Validation

Date: 2026-07-14

Tasks: `FR-10-T04`, `FR-10-T09`, `FR-10-T14`, `FR-10-T16`

Status: The unadvertised foundation and readiness integration are complete;
the later default-off one-command DATA/ACK production observation is linked
below. Advertisement and native authority remain open, so `FR-10-T04` remains
Incomplete.

## Delivered boundary

This slice closes four prerequisites without changing a live wire stream:

1. `Netchan_ProcessEx` provides a terminally handled post-admission RX seam
   symmetric with the dormant post-assembly TX seam.
2. The role-specific readiness core provides time-aware, sticky RX/TX gates for
   an exact `CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE` proof.
3. A packet-scoped 13-pair signed-setting sideband carries the proof through
   either existing legacy setting direction.
4. A default-off canonical command shadow builds, retains, compares, and joins
   native/legacy records without granting native data simulation authority.

The capability bit `WORR_NET_CAP_NATIVE_ENVELOPE_V1` remains excluded from the
live offer/support masks. At this readiness-only validation point, eligible
production connections registered BYPASS-only hooks and exchanged readiness
tuples only when both experimental endpoint cvars were enabled; WTC1 DATA/ACK
did not yet exist and native authority was absent.

Detailed contracts:

- `docs-dev/fr-10-t04-post-admission-netchan-rx-seam-2026-07-14.md`;
- `docs-dev/fr-10-t04-native-endpoint-readiness-core-2026-07-14.md`;
- `docs-dev/fr-10-t04-native-readiness-setting-sideband-2026-07-14.md`;
- `docs-dev/fr-10-t04-native-command-shadow-core-2026-07-14.md`; and
- `docs-dev/fr-10-t04-default-off-native-readiness-production-pilot-2026-07-14.md`.

## Independent review outcomes

The RX seam review found no P0/P1 defect. It prompted explicit terminal
rejection at every production call site, dedicated-only coverage, fragment and
reassembly boundary probes, truncated-header cases, self-unregistration, and
ABI/layout checks.

The readiness review found a deadline hazard in passive phase predicates. Both
admission gates now take the current tick and apply sticky clock/deadline checks
themselves. It also established the globally non-reused transport-epoch caller
contract and explicit terminal epoch exhaustion.

The command-shadow review found and closed:

- owner/output aliases that could mutate bytes promised untouched through
  telemetry counters;
- bytewise duplicate classification that disagreed with canonical signed-zero
  and legacy packet-shared semantic equality;
- an unreachable model-mismatch result that overstated V1 coverage;
- stored reports that were insufficiently bound to retained records, recomputed
  sample offsets, or the active comparator baseline;
- duplicate join keys and multiple surviving baseline-establishment claims.

The final command-shadow review reported no remaining P0-P2 finding. Its three
non-blocking boundary gaps were then added: pre-baseline mismatches, the
`0`/`UINT64_MAX` sample-offset boundary, and the accepted terminal command-
sequence transition.

The readiness-sideband review reported no P0-P2 finding. Full telemetry offsets,
an independent fixed `WRS1` checksum/commit vector, and an end-to-end readiness
exchange with duplicate, wrong-direction, stale-binding, and live-gate checks
were added afterward.

The production-adapter review found and closed a client hook-ownership defect:
the initial arming path could replace callbacks owned by another subsystem.
Client arming now requires all TX/RX callback and opaque fields to be empty,
and the production harness proves an occupied hook set remains unchanged. The
review also verified the deliberate `SERVERDATA` canonical packet restart in
both live and seek parsers, default-off byte identity, public mask `3`, private
readiness binding `0x13`, queue atomicity, and teardown ownership. No P0-P2
finding remains in the readiness pilot. The final server append hardening also
has direct 116/117-byte source and reliable-capacity boundary coverage,
including a non-empty current-message prefix and failure-without-mutation.

## Current validation

Windows Clang validation on the current tree passes:

- all 104 registered networking tests in one complete run;
- three consecutive complete runs, 312/312 total;
- strict C11/C++20 focused builds with warnings as errors;
- current ASan/UBSan behavior tests for readiness sideband, command shadow,
  and both production readiness adapters;
- current Clang static analysis for netchan, readiness, readiness sideband, and
  command shadow plus both production readiness adapters;
- i686 C/C++ adapter source and fixed-layout checks;
- client engine, dedicated engine, cgame, sgame, client launcher, and dedicated
  launcher production builds.

The refreshed `.install/` validates 16 root runtime files, the `basew` runtime,
a 308-asset `pak0.pkz`, 31 botfile package/loose paths, and 214 RmlUi
package/loose paths. SHA-256 hashes match between the build tree and staged
copies for all six engine, launcher, cgame, and sgame binaries.

The staged `worr.networking.impairment-runtime.v3` smoke records 387/387 clean
and 384/384 impaired projected/published/legacy-compared/cgame-consumed
snapshots with zero shadow mismatch, capture/frame failure, or consumer
rejection. An initial invocation exited before impaired live evidence; a clean
rerun produced the accepted report. This remains a short one-client Windows
smoke, not repeat, soak, or release evidence.

`q2proto/` remains unchanged.

## Historical next production slice

The next dependency-preserving slice is a default-off, client-to-server command
shadow with server-to-client ACK-only traffic, built on the now-live readiness
owners and hook lifecycle:

- retain the public legacy-only capability masks;
- carry exactly one unfragmented 110-byte command DATA entry client-to-server;
- carry ACK-only WTC1 server-to-client;
- feed native records only to the diagnostic join while legacy MOVE/BATCH_MOVE
  remains the sole simulation authority;
- exclude DATA/ACK piggyback, snapshots, events, native demos, and promotion.

The implementation must clear hooks before teardown, use process-static
non-reused owner/epoch/nonce allocation, preserve the old RX epoch during map
quiescence, handle the 1,024-byte client ceiling at the exact 818/819-byte legacy
prefix boundary, and stage RX session/join/receipt mutations atomically. Full
native/legacy impairment, bandwidth, load, demo/spectator, and platform parity
remain mandatory before advertisement or authority can change.

## Production integration update (2026-07-14)

The narrow next slice specified above is now wired and validated: one client
command DATA observation, one server ACK range, exact native/legacy join, and
current plus one retired epoch bank run through the production TX/RX seams.
The current evidence and remaining boundaries are recorded in
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
The public mask and legacy authority rules are unchanged; mixed DATA/ACK,
repeated native commands, real async-wake impairment, and release gates remain.
