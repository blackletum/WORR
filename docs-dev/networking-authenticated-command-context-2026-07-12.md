# Authenticated server command context (2026-07-12)

## Tasks and scope

This note records the live engine-to-sgame authority seam developed for
`FR-10-T09`, `FR-10-T10`, and `FR-10-T11`. It does not complete any of those
tasks.

The server exposes the intentionally stable discovery name
`WORR_AUTHORITATIVE_COMMAND_CONTEXT_IMPORT_V1` while it is synchronously
executing one canonical command. The import table is now API version 2 and 24
bytes, adding `GetScopeState()` beside `GetCurrent()`. Scope is explicitly
`INACTIVE_LEGACY`, `ACTIVE_VALID`, or `ACTIVE_REJECTED`. The 256-byte,
pointer-free context copies the canonical command record, the current trusted
snapshot-time view, and the server-built rewind mapping proof. Sgame can copy
the value; it cannot retain engine storage or query a context outside that
callback. A canonical rejection is therefore distinguishable from an inactive
legacy callback and cannot silently enter packet-ACK fallback.

Subsequent integration status: server-synthesized gap commands now enter
`ACTIVE_REJECTED`; they do not receive a trusted zero-error proof. The
post-callback consumed cursor is live in negotiated snapshots/demos. Sgame
resets per-map/client policy state and now builds one sealed common
player-bounds rewind scene per command from the common 512-pose history, using
generation-checked revalidation and per-ray ignore sets. The source snapshot
is still authenticated through a server-owned legacy frame-ring projection,
not a materialized canonical server snapshot store.

## Authority construction

`SV_SetLastFrame` accepts only an acknowledgement that maps to an exact live
entry in the server-owned per-client frame ring. It retains the authoritative
simulation tick and the contiguous snapshot interval stored when that frame
was built. Client wire frame numbers are never used as simulation time.

For a canonical command, the server constructs:

- a current snapshot ID from the server-owned nonzero map epoch and current
  simulation tick;
- a source snapshot ID from the same map epoch and the authenticated
  acknowledged simulation tick;
- an exact consumed-command identity equal to the command being committed for
  execution; and
- a mapping proof whose watermark fields must byte-semantically match the
  retained canonical command.

For a legacy packet-shared watermark, the mapped time subtracts the durations
of later commands already present in the canonical batch and half of the exact
contiguous source-snapshot interval. Its error bound includes one server tick,
the command duration, and that interpolation half-interval. A zero or stale
source acknowledgement makes the context unavailable; it is not converted
into client-authored time.

The source interval is taken directly from the exact acknowledgement captured
by `SV_SetLastFrame`. An earlier implementation scanned ring entries using only
the simulation tick, which was ambiguous because stale slots from a prior map
can reuse the same tick number.

## ABI and lifecycle hardening

The shared ABI pins the command, current-snapshot, and mapping-proof offsets in
both C and C++. The engine validates the full relationship before publishing a
context:

- command and consumed-cursor identities match exactly;
- snapshot/source IDs share an epoch and do not regress;
- source tick/time do not exceed the current trusted snapshot;
- proof and snapshot tick intervals match; and
- proof provenance, flags, tick, and times match the canonical watermark.

Only one context may be active. The server clears the scope before and after
every normal server `ClientThink` callback, at map spawn, and before game-DLL
shutdown; a canonical callback installs its validated context only after the
pre-entry clear. Clearing before entry is important because the engine has
non-local error paths: an abandoned callback can no longer leak the previous
client's authority into the next callback.

Session and map epoch allocators no longer wrap from `UINT32_MAX` to 1. Session
capability selection fails closed when exhausted; exhausting the server map
epoch is fatal because reusing a canonical map identity would make historical
authority ambiguous.

## Verification

The four touched server translation units passed syntax-only checks using the
dedicated-server entries from `builddir-win/compile_commands.json`. A C++20
header probe passed with warnings as errors. A focused lifecycle/binding test
at `.tmp/command_context_audit_test.c` was compiled against the real command,
snapshot, event, and rewind cores and passed these cases:

- inactive, begin, copy, nested-begin rejection, end, and inactive-again;
- source-snapshot epoch mismatch rejection;
- consumed-command mismatch rejection; and
- proof/watermark time mismatch rejection.

No full build was run during that focused audit so it would not race the parent
integration build. The subsequent integration pass linked the engine,
dedicated-server, cgame, and sgame DLLs; passed all `49/49` networking tests;
passed a ten-repeat high-risk subset `70/70` including this context seam; and
passed the staged headless networking runtime smoke.

## Remaining work

This seam is a live authenticated adapter, not yet the final native snapshot
authority path. Current remaining work is:

- source snapshot IDs are still a server-owned projection over the legacy
  client-frame ring, rather than lookup records in a materialized canonical
  server snapshot store (`FR-10-T06`/`FR-10-T10`);
- native exact per-command render watermarks remain `FR-10-T09` work;
- server-synthesized gaps correctly reject historical rewind, but their bounded
  timing semantics still need an explicit deterministic acceptance matrix;
- the common scene still needs brush-mover collision assets, mover-relative
  live capture, and the declared fairness/load matrices; and
- the native T04 transport must carry the same authority without creating a
  second command/snapshot schema.

The callback marks the executing command as the committed consumed boundary so
lag-compensation policy can resolve during that command. The stream cursor is
advanced immediately after the callback returns, and only that post-callback
cursor is published in later negotiated snapshots.
