# Live client snapshot, prediction, and demo-clock integration

Date: 2026-07-12  
Project tasks: `FR-10-T06`, `FR-10-T07`, `FR-10-T08`, `FR-10-T09`,
`FR-10-T13`

## Outcome

The legacy q2proto client path now feeds a live canonical shadow without
changing any file under `q2proto/`. Accepted frame headers, baselines, entity
deltas, area data, player state, inferred legacy events, and the exact
server-consumed command cursor are projected into immutable snapshot V2
storage. Parity-qualified views are copied into the external cgame timeline;
legacy rendering and presentation remain authoritative while rollout evidence
is accumulated.

Prediction now resolves an immutable input range from the consumed-command
cursor carried by the accepted snapshot. Once that cursor is established,
transport packet acknowledgement cannot replace it. The range contains the
exact finalized successors plus an optional copied pending command, so cgame
does not retain mutable engine command-ring pointers.

## Negotiated cursor carrier

The live capability mask advertises only the legacy command sideband and
consumed-command cursor. The server stages five adjacent signed
`SVC_SETTING` pairs and the q2proto frame header as one transaction. The client
requires that tuple for every negotiated frame, binds the first nonzero cursor
to the confirmed session epoch, and rejects partial tuples, packet-boundary
crossing, service interposition, regression, epoch aliasing, or `{0,0}` after
canonical establishment.

`{0,0}` remains the sole pre-establishment bootstrap value. During that
bounded phase, and for peers without the capability, prediction uses an
explicitly tagged legacy packet-ack fallback. It cannot silently downgrade
after canonical authority has been observed.

## Canonical snapshot shadow and cgame timeline

The client shadow owns connection-sized storage and reconstructs exact
q2proto delta lineage before normal presentation, during precache, and while
seeking. It independently canonicalizes the accepted legacy frame for parity
comparison instead of comparing projector-owned data with itself. Consumer
delivery is value/range based and generation checked; a zero-flag publication
may retain lineage without reading `cl.frame` or presenting it.

The external cgame consumer copies bounded snapshot/player/entity/event data
into its own canonical timeline. It exposes clock and pair selection, entity
sampling, safe copy-out, event iteration, reset reasons, prediction replay and
correction counters, and audit diagnostics. Rendering and canonical event
presentation have not yet cut over.

## Server-time continuity

Live frames use a stateful canonical clock. The first accepted frame anchors
the connection; later frames integrate frame deltas with the interval active
for that frame. This avoids the discontinuity caused by recomputing all time as
`frame_number * current_interval` after an `SVS_FPS` change. Regressing frames
fail closed unless an explicit seek/reset boundary establishes new lineage.

The legacy wire does not carry an absolute server time. A loss gap spanning an
unobserved rate change therefore remains an adapter approximation until the
native snapshot transport supplies exact time directly.

## Exact in-memory demo seek anchors

Stored seek snapshots can begin with backup frames older than the currently
presented frame, including during a user-visible forward seek. Reconstructing
those backups from only the latest FPS loses the accumulated pre-change clock
offset. The client therefore emits a private, client-generated six-setting
demo-clock tuple for every stored backup frame:

- reserved indices `-31780` through `-31775`;
- schema version and exact server frame;
- 64-bit canonical server time split into two words;
- field checksum; and
- independent commit word.

The clock tuple, existing consumed-cursor tuple, frame header, and all entity
deltas are staged atomically. The clock tuple precedes the cursor so the cursor
remains adjacent to the frame. It is accepted only while the client has
explicitly armed the next in-memory seek-snapshot packet. Live traffic and
ordinary sequential demo packets cannot use this reserved range. Parsing
requires a valid FPS anchor before the first exact clock, exact frame-number
matching, complete packet transactions, monotonic exact time, and only the
validated consumed-cursor tuple between a committed clock and its frame.

Selecting any stored snapshot resets the projection epoch and canonical clock,
even when seeking forward; sequential forward skipping without a stored
snapshot retains clock continuity. Legacy demos and protocols without the
private carrier continue through the stateful fallback.

## Failure behavior

- Invalid q2proto data, partial capability/cursor/clock tuples, or inconsistent
  exact times drop the packet/demo path before canonical publication.
- Projection and parity failures retain legacy authority and suppress cgame
  promotion.
- Missing, duplicate, discontinuous, invalid, or expired canonical command
  history clears local prediction caches and snaps presentation to the current
  authoritative player state. It does not replay an ambiguous range.
- The current local hard resync does not request an out-of-band full snapshot;
  transport-level keyframe recovery remains future `FR-10-T08/T16` work.

## Verification

The wrapped Windows Clang production configuration linked:

- `worr_engine_x86_64.dll`;
- `worr_ded_engine_x86_64.dll`;
- `cgame_x86_64.dll`; and
- `sgame_x86_64.dll`.

The ordinary Meson networking suite passed `49/49`. A ten-repeat high-risk
subset passed `70/70`, covering command context, q2proto projection, demo clock
codec/runtime, consumed cursor, prediction range, and rewind core. A separate
`.tmp/build-network-fps` configuration with `-Dvariable-fps=true` compiled the
client parse, demo, snapshot-shadow, demo-clock, and main translation units.
C and C++ layout probes pin the new demo-clock and snapshot-shadow contracts.

The deterministic baseline and staged headless loopback targets also passed,
writing `.tmp/networking/impairment-baseline.json` and
`.tmp/networking/impairment-runtime.json`. The required install refresh copied
the current engine, dedicated-server, cgame, and sgame binaries, rebuilt
`basew/pak0.pkz`, and validated `.install/` for `windows-x86_64`.

An unrestricted all-target Ninja build progressed through the networking and
game targets but stopped in unrelated concurrent OpenGL/RmlUi work at
`src/renderer/rmlui_bridge.cpp`: the existing `IF_REPEAT | IF_NOSCRAP`
expression has type `int`, while `IMG_Find()` requires `imageflags_t`. That
dirty-worktree renderer source was not changed by this networking task. The
four networking-relevant production DLL targets were then built explicitly and
all linked successfully. Ninja continues to emit its pre-existing
`premature end of file; recovering` warning despite successful target exits.

## Remaining scope

The server peer snapshot shadow, exact sent-ref retention, 100,000-frame live
parity gate, adaptive interpolation/extrapolation promotion, classic-cgame
migration, predictable weapon/gameplay state, audiovisual replay correlation,
native keyframe requests, MVD/GTV/spectator matrices, native demo schema, and
the `FR-10-T14/T15` load/release gates remain open.
