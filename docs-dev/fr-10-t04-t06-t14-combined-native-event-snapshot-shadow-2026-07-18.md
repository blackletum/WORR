# FR-10 combined native event and snapshot shadow

Date: 2026-07-18  
Project tasks: `FR-10-T04`, `FR-10-T06`, `FR-10-T14`  
Related tasks: `FR-10-T05`, `FR-10-T07`, `FR-10-T09`

## Outcome

The default-off native pilot can now negotiate and operate the private `0x77`
event-plus-snapshot mode. The public capability mask remains `0x03`, legacy
commands and gameplay remain authoritative, and no `q2proto/` source changed.

This removes the prior event-versus-snapshot configuration exclusion. It does
not advertise the native envelope publicly or complete any parent FR-10 task.

## Combined readiness and record routing

Enabling both event and snapshot controls now selects one readiness binding
containing the base envelope, event stream, snapshot, and epoch-cancellation
bits. The established fourth `CLIENT_ACTIVE_CONFIRM` record remains mandatory.

Event DATA uses the low transport message-sequence half and snapshot DATA uses
the high half. This is an explicit record-class discriminator for incoming ACK
ranges, whose wire representation intentionally contains transport identity
rather than canonical record class. ACK application therefore cannot release
the wrong semantic sender.

Both endpoints use fair lane selection:

- server DATA alternates between due event and snapshot lanes;
- client ACK preparation alternates between event and snapshot receipts;
- current and retired transport banks retain the existing fair selection;
- one temporarily blocked lane cannot prevent the other from being tried.

## Snapshot continuity and sender recovery

The combined live gate exposed several snapshot-path defects that narrower
fixtures had not exercised:

- client snapshot epoch rebinding now validates and rekeys retained projection
  state without discarding legacy expectations, delta bases, hashes, or
  discontinuity identity;
- server final-emission projection canonicalizes q2proto server-write origin
  unions into the absolute read form consumed by the canonical model;
- loop volume/attenuation flags are cleared when no sound component exists;
- a legal `entities_per_slot == max_entities` configuration is accepted;
- after any fragment has been sent, the active snapshot waits for its semantic
  ACK while the second payload bank coalesces only the newest pending view;
- if an incomplete receiver expires and can never authorize that ACK, a bounded
  1,000-tick horizon permits the abandoned active identity to be replaced.

Ordinary round-trip ACKs therefore release the retained snapshot instead of
arriving after frame-rate supersession, while incomplete-message loss still
recovers without permanent stop-and-wait deadlock.

## Diagnostics and live gate

The server command `sv_worr_native_shadow_status [slot]` now reports an exact
selected peer and includes `WORR_SNAPSHOT_EMISSION_STATUS_V1` plus the native
snapshot sender row. The client command includes
`WORR_NATIVE_CLIENT_SNAPSHOT_STATUS_V1`. Snapshot failure detail encodes
operation, sender result, and validation flags, which made queue, due, and
prepare failures distinguishable during live work.

Schema v33 of
`tools/networking/run_canonical_rail_damage_runtime_gate.py` adds
`blaster-local-action-lease-combined`. It reconnects the real shooter, proves
the exact local-action receipt through the event lane, and independently
requires semantic snapshot ACK/release traffic on both peers.

Final artifact:
`.tmp/networking/combined-native-shadow-acceptance8.json`.

| Repeat | Event receipts/matches | Slot 0 snapshot ACK/release | Slot 1 snapshot ACK/release | Slot 0 pending coalesces |
| --- | ---: | ---: | ---: | ---: |
| 1 | 32/32 | 59/77 | 390/392 | 135 |
| 2 | 32/32 | 59/75 | 387/389 | 135 |
| 3 | 35/35 | 57/76 | 388/390 | 130 |

All three runs have zero event mismatch/conflict/resync, zero native server or
client failures, and normal 15-damage Blaster authority. Aggregate event
parity is 99 receipts and 99 exact matches.

## Focused verification

The following focused rows pass after the combined work:

- native snapshot sender;
- native server shadow pilot;
- native client readiness pilot;
- native event production virtual link;
- native snapshot production virtual link;
- 45 schema-v33 Python gate contracts.

The production snapshot virtual link retains digest `abf2723d5f03cbe9` after
its fixture was corrected to model q2proto's direction-specific write/read
union rather than sharing one unrealistic carrier object.

## Aggregate verification

The final production and release pass completed with:

- an up-to-date successful `ninja -C builddir-win` production build;
- 17/17 focused snapshot/q2proto/codec/carrier/pilot/virtual-link rows;
- 93/93 lag-compensation plus schema-v33 runtime-gate Python contracts;
- 157/157 headless Meson networking rows in 783.7 seconds, including the
  100,000-frame final-projection corpus in 44.7 seconds and the serialized
  native production corpus in 781.6 seconds;
- 30/30 package/release/bootstrap contracts; and
- a refreshed, validated Windows x86-64 `.install/` containing 16 root runtime
  files, one runtime dependency, a 525-file `pak0.pkz`, one q2aas reference
  map, 31 botfile payloads, and 215 RmlUi assets.

The first aggregate run exposed one test-only direction-modeling defect in the
final-projection corpus: it shared client-decoded `read` coordinate unions with
the production server adapter. The fixture now constructs a separate server
`write.previous/current` carrier and keeps the independently decoded receiver
carrier. Its standalone 100,000-frame rerun passed before the clean 157/157
aggregate rerun.

## Remaining scope

`FR-10-T04`, `FR-10-T06`, and `FR-10-T14` remain incomplete. Public
advertisement, broad event-family coverage, sustained combined load, the full
impairment and supported-platform matrices, demos/spectators, security fuzzing,
and release promotion remain open.
