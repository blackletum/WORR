# Local-Action Shadow Authority Receipt

Date: 2026-07-17

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T08`, `FR-10-T09`,
`FR-10-T14`

## Scope

This slice carries the descriptor-complete, observation-only local-action
shadow through the existing private native event carrier and gives cgame a
bounded command-correlation owner. It remains default-off and does not make a
weapon transaction predictive or authoritative.

The new `worr_local_action_shadow_authority_receipt_v1` is a pointer-free
64-byte value. It binds one canonical command identity and semantic command
hash to the exact catalog identity, descriptor hash, blocker mask, shadow
flags, and full shadow record hash. Validation reconstructs the frozen global
descriptor hash and exact per-catalog blocker mask; unknown flags, reserved
bits, invalid command/catalog identities, or zero hashes fail closed.

## Ownership and data flow

1. The leased sgame weapon observation builds the existing 528-byte immutable
   shadow only after the scoped and post-command records join exactly.
2. The default-off `sg_local_action_shadow_receipts 1` control allows sgame to
   publish the compact receipt through
   `WORR_LOCAL_ACTION_SHADOW_AUTHORITY_IMPORT_V1`. Gameplay, ammo, callbacks,
   event presentation, and weapon timing are unchanged.
3. The engine owns a 32-entry mailbox per client. Exact duplicates are
   idempotent, a different receipt for the same command is rejected, and
   receipts drain in publish order. The server peeks before native enqueue and
   consumes only after enqueue succeeds, so sender backpressure cannot destroy
   a critical receipt. Map reset, client reset, direct reconnect, and bot-slot
   reuse clear the affected mailbox.
4. The server projects the receipt as critical, reliable-ordered,
   authoritative-only event payload kind `12`. Both entity references remain
   absent. The existing WNE1 codec writes and reads every receipt field
   explicitly; no raw-struct wire copy or `q2proto/` change is involved.
5. Cgame classifies both authority-receipt payload families as private and
   non-presentable. Its 128-entry action-shadow owner records semantic hashes
   for exact canonical commands from the production prediction-input range and
   accepts receipt-first or command-first arrival. An exact hash matches;
   conflict, mismatch, expiration, invalid input, or capacity exhaustion
   latches the shared event-runtime resync state.

This owner is an audit boundary only. It never predicts a weapon callback,
changes player state, creates a side effect, or presents audio/visual output.

## Compatibility and security

- The sgame producer is default-off and the existing native event lane remains
  separately negotiated/default-off. Legacy clients, servers, snapshots,
  demos, and gameplay behavior are unchanged.
- `q2proto/` is untouched.
- Mailboxes and cgame correlation storage are fixed-capacity and allocation
  free. Inputs are validated before storage and exact byte conflicts fail
  closed.
- A private receipt can advance only the audit ledger. It cannot use entity
  presentation paths and a correlation failure degrades authority rather than
  silently accepting a different command.
- Native sender retry, exact event receipts, epoch cancellation, and descriptor
  gates remain owned by the established event carrier.

## Deterministic verification

Focused build:

```powershell
ninja -C builddir-win local_action_shadow_test.exe `
  local_action_shadow_authority_test.exe cgame_local_interaction_test.exe `
  cgame_event_runtime_test.exe native_codec_test.exe `
  native_event_virtual_link_test.exe event_schema_layout_c_test.exe `
  event_schema_layout_cpp_test.exe cgame_x86_64.dll sgame_x86_64.dll `
  worr_engine_x86_64.dll worr_ded_engine_x86_64.dll
```

Focused execution:

```powershell
builddir-win\local_action_shadow_test.exe
builddir-win\local_action_shadow_authority_test.exe
builddir-win\cgame_local_interaction_test.exe
builddir-win\cgame_event_runtime_test.exe
builddir-win\native_codec_test.exe
builddir-win\native_event_virtual_link_test.exe
builddir-win\event_schema_layout_c_test.exe
builddir-win\event_schema_layout_cpp_test.exe
```

The focused results prove:

- the unchanged 22-entry shadow golden `1270244792631122`;
- mailbox capacity `32`, exact FIFO order, idempotent duplicates, conflicting
  command rejection, failed-enqueue retention, map reset, and reconnect reset;
- command-first exact match, receipt-first exact match, duplicate idempotence,
  and mismatch/conflict resync in the cgame owner;
- private runtime admission and terminal skip with zero presentation;
- fieldwise native encode/decode of payload kind `12` and C/C++ ABI layout;
- one real in-process native virtual link from descriptor admission through
  WNE1 delivery, cgame dispatch, exact canonical command-hash match, semantic
  ACK, and server retention release. The established impairment digest remains
  `be9724b38fb5f682`.

Final integration validation also passes:

- complete production build, including both engines, cgame, sgame, launchers,
  all renderers, and updater;
- complete headless networking suite: `157/157` in `383.93` seconds, with the
  two-pass production snapshot corpus row passing in `289.24` seconds;
- packaged-asset tests: `16/16`;
- release bootstrap headless contract: `1/1`; and
- refreshed and validated Windows x86-64 `.install`: 16 root runtime files,
  one dependency, a 524-file `basew/pak0.pkz`, 31 botfile payloads, 215 RmlUi
  assets, and one q2aas reference map.

The 2026-07-18 live continuation additionally passes three independent,
network-only headless client/server repeats after an in-session shooter
reconnect. The final schema-v32 report records exact epoch-3 command/shadow
correlation, 45 receipt matches for 45 receipts, zero unmatched/outstanding,
zero mismatch/conflict/resync, and exact normal 15-damage Blaster execution in
every repeat. See
`docs-dev/fr-10-t04-t05-t08-t09-t14-live-reconnect-local-action-authority-2026-07-18.md`.

The archive count includes unrelated renderer-parity assets already present in
the shared working tree; this slice claims only that the final combined stage
validated.

## Remaining work

This closes no parent FR-10 task. Live cross-process receipt parity and an
in-session disconnect/reconnect rebase are now proven, but this does not prove
a V2 weapon transaction, audiovisual prediction/de-duplication,
sustained correction budgets, dual-adapter load, demo/spectator behavior,
malformed-input scale, soak, or cross-platform release acceptance. No catalog
entry may be promoted while its exact V2 blocker mask is nonzero.
