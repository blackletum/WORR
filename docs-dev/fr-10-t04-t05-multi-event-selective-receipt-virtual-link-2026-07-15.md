# FR-10 Multi-Event Selective-Receipt Virtual-Link Proof

Date: 2026-07-15

Project tasks: `FR-10-T04`, `FR-10-T05`

## Outcome

The production-hook virtual link now proves selective semantic acknowledgement
across a true event-order gap. Three separately retained canonical legacy
entity-event candidates share one descriptor. Event 2 is accepted by the
server transport but intentionally lost before client delivery; the scheduler
then sends the still-unsent event 3. The client admission endpoint accepts
event 3 out of semantic order, reports it in the selective receipt window, and
does not advance its presentation cursor across the gap.

The client ACK for that state releases event 3 at the server while event 2
remains retained. When event 2 reaches its resend deadline, its retry closes
the gap and advances the client receipt contiguously through event 3. Its final
ACK leaves no retained event. This establishes that the complete production
adapter path does not treat an acknowledgement for a later event as cumulative
authority for an earlier lost event.

## Exercised boundary

`tools/networking/native_event_virtual_link_test.cpp` continues to call the
real client and server netchan lifecycle callbacks. The test’s cgame export is
a deliberately small receipt-owning runtime model: it uses the shared
`Worr_EventReceiptMarkV1` / `Worr_EventReceiptContainsV1` rules and exposes the
same status contract that native admission validates. That permits the link to
exercise semantic reordering without inventing a second wire or acknowledgement
format.

The scenario checks all of the following through decoded WTC1/WNE1 packet
identity and live server status:

- descriptor authority is accepted before all three events are promoted;
- server DATA identities arrive as event 1, lost event 2, then event 3;
- after event 3, the client receipt is `{ highest_contiguous = event 1,
  selective_mask = 0b10 }`, and the presentation cursor remains event 2;
- that receipt retires event 3 only, leaving exactly event 2 retained;
- the event-2 retry produces `{ highest_contiguous = event 3,
  selective_mask = 0 }`, then the final ACK clears retention.

The prior single-event loss/retry/duplicate/cancellation scenarios remain in
the same executable and retain their stable aggregate diagnostic digest; this
new assertion-only scenario deliberately does not relabel that historic metric
as a statistical impairment golden.

## Scope and remaining work

This is default-off shadow evidence, not native authority or cgame presentation
cutover. It does not add temporary-event, muzzle-flash, spatial-audio, direct
sgame multi-event, or predicted-local-action production. It also does not
replace the reusable `NetImpair` matrix, live multi-process/load/soak evidence,
adaptive batching, or broad event-family coverage. `q2proto/`, legacy packets,
demos, snapshots, and legacy presenters remain unchanged.

## Verification

- Focused headless readiness/server-shadow/event virtual-link gate: 5/5 pass.
- Production client engine, dedicated-server engine, cgame, and sgame build:
  pass.
- Full headless networking suite: 121/121 pass.
- Three consecutive headless suite repetitions: 363/363 pass.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64`: pass; staged
  16 root runtime files, one root dependency, 342 packaged source assets,
  31 botfiles, and 215 RmlUi assets.

Roadmap completion remains unchanged: 68 of 180 tasks complete (37.8%, 112
open); `FR-10` remains 3 of 16 complete (18.75%, 13 open).
