# Deterministic Prediction Core and Replay Contract (2026-07-12)

Task: `FR-10-T02`.

Strategic project:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Living plan:
`docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`.

Status: complete for `FR-10-T02`. Implementation, Windows Clang/Linux GCC
replay-corpus parity, full Windows integration, staged runtime, and legacy
command-adapter gates passed on 2026-07-12.

Subsequent integration note: `FR-10-T08/T09` now supply a live, versioned
engine-to-cgame authoritative input-range boundary. Once the negotiated
server-consumed cursor is established, cgame maps that exact canonical ID to
retained local history, verifies and replays only its successors plus a copied
pending command, and does not use packet ACK. Negotiated `{0,0}` bootstrap and
truly legacy peers keep an explicit provisional fallback; missing, ambiguous,
discontinuous, invalid, or over-capacity history causes a local hard resync.
This does not extend the completed T02 claim to weapon/gameplay prediction or
side-effect suppression.

## Outcome

WORR now has a versioned, exactly typed prediction boundary instead of relying
on coincidentally similar C and C++ object layouts across the engine/cgame DLL
boundary. The client and authoritative game link the same position-independent
C++20 movement-core archive. Cgame feeds that core through pointer-free state,
command, configuration, plane, and trace records; sgame calls the same native
core directly.

This slice deliberately defines the movement/stance prediction component. At
this revision it did not claim weapon-state or interaction prediction,
canonical consumed-input acknowledgement, or the final immutable cgame API.
The later authoritative input-range integration is summarized above; weapon
and interaction prediction, audiovisual side-effect suppression, and final
presentation migration remain open.

No file under `q2proto/` changed.

## Rejected Boundary

The previous path constructed the engine C `pmove_t`, passed its address through
the cgame extension, and interpreted the bytes as the unrelated C++ `PMove`.
Size/offset assertions could not make the callback types, returned trace
aggregates, entity/surface pointer types, or 32-bit calling convention safe.

That path has been removed. The external game/cgame API versions advance to
`2025`/`2028`, and the cgame entity extension advances to API 4 with v3 extension
names, so a stale mixed binary fails at module loading instead of silently using
the removed entry point.

The built-in classic C PMove path remains available for legacy servers and
demos. The modern WORR path does not modify or redirect that compatibility
adapter.

## Shared Build Artifact

Meson builds `p_move.cpp` and `prediction_abi.cpp` once into the PIC static
`prediction_core` library with one C++20 language mode. Both cgame and sgame link
that archive whole. The old independent C++20/cgame and C++23/sgame compilations
of `p_move.cpp` are gone.

The core receives movement configuration by value on every call. N64 physics,
Quake 3 overbounce, and air acceleration are no longer hidden reads from the
module policy global. Thread-local invocation scratch is protected by a stack
scope and restored across nested calls and exceptions. Prediction hot paths use
templated trace callables rather than constructing `std::function`, and stuck
resolution selects a candidate with a total ordering: squared displacement,
axial side, then lexicographic position.

## Prediction ABI v1

`inc/shared/prediction_abi.h` is valid C11 and C++20. Its persistent records are
standard field-defined values with explicit sizes, schema versions, reserved
fields, stable numeric entity/surface identities, and no runtime pointers:

- `worr_prediction_state_v1` contains movement type, origin, velocity, flags,
  timer, gravity, view height, and delta angles;
- `worr_prediction_command_v1` contains only simulation input: duration,
  buttons, view angles, and planar movement;
- `worr_prediction_config_v1` contains an explicit movement-model revision and
  the movement-rule inputs;
- trace/plane records contain bounded normalized results and stable identifiers.

The transient step envelope contains callbacks and an opaque context and is
therefore process-local. It is explicitly excluded from serialization and raw
hashing.

Every input and callback result is validated before it can become authoritative
output. Unknown schema/model revisions, unknown flags, non-zero reserved fields,
non-finite vectors, invalid fractions/planes, conflicting stable IDs, token
capacity overflow, and callback exceptions fail closed. A failed step leaves the
caller's record unchanged.

The bridge owns fixed 512-entry entity-token and surface registries. PMove sees
stable opaque native pointers during one invocation, but no engine/game pointer
enters a retained canonical record.

## Canonical Commands and Time

The simulation command excludes packet acknowledgement, snapshot frame, rewind
time, and transport identity. Those fields do not affect movement and belong to
the command/timeline contract in `FR-10-T09`.

Before simulation, view angles round-trip through signed 16-bit short-angle
quantization decoded by the supported legacy transports. Forward/side movement
truncates to the signed integer world-unit range used by Q2REPRO/Q2PRO batched
commands (`-512..511`; ordinary input is already clamped to `-400..400`). Cgame
therefore replays the input the server can decode, while retaining
full-resolution local view angles for rendering outside the simulation record.

Command duration is the explicit deterministic time input. Replay uses modular
`uint32_t` command identity and a count-based `(acknowledged, current]` plan;
relational loops that fail at `UINT32_MAX` are no longer used.

Model revision 1 intentionally does not round every command to a forced fixed
physics quantum. Integer millisecond duration is replayed exactly, which keeps
established Quake II/Rerelease acceleration, jump, and timer behavior while
removing wall-clock dependence. The corpus covers short, boundary, and long
durations. Decoupling input sampling, simulation cadence, and packet cadence is
tracked by `FR-10-T16`; any later fixed-quantum movement model requires an
explicit revision and new golden evidence rather than a silent physics change.

## Live Wire Command Adapter

The engine-owned `NetUsercmd_*` C interface is now the single adapter between
the canonical command and q2proto's delta record. Client command finalization
canonicalizes the completed command before placing it in prediction history;
ordinary and batch send paths then use the shared delta builder. The server's
ordinary and batch readers use the matching delta applier before gameplay sees
the command. The local render view angles remain full precision.

Transport canonicalization is explicit. Rerelease/Q2REPRO retains the modern
button representation. Vanilla and Q2PRO map jump/crouch through legacy
`upmove`, strip the unrepresentable holster bit, and deterministically map a
simultaneous jump+crouch request back to neither button because its legacy
vertical movement cancels to zero. Invalid finite/range input fails before an
output command or delta is mutated.

`network-usercmd-live-wire-parity` proves the production adapter through real
q2proto entry points without changing `q2proto/`. It serializes serverdata into
a bounded memory stream and initializes the client by parsing that stream, then
round-trips canonical command chains with `q2proto_client_write` and
`q2proto_server_read` for Vanilla MOVE, Q2PRO MOVE/BATCH_MOVE, and Q2REPRO
MOVE/BATCH_MOVE. Coverage includes duration values `0`, `1`, `7`, `8`, `9`,
`66`, `250`, and `255`; movement endpoints `-512` and `511`; every button
mapping; accumulated multi-turn angles; exhaustive stability of all 65,536
signed short-angle encodings; delta inheritance; and atomic rejection.

The test's `--json` mode records protocol/message semantics, exact canonical
and decoded float bits, canonical prediction hashes, wire byte lengths and
SHA-256 hashes, canonicalization idempotence, decoded equality, and direct
Q2PRO/Q2REPRO MOVE-versus-BATCH_MOVE equality for the same canonical prefix.

## Hash and Transcript Contract

State, command, and configuration hashes use domain-separated FNV-1a-64 over
explicit little-endian fields. Padding, object addresses, wall clock,
allocations, renderer state, screen blend, and smoothing are never hashed.
Negative zero and NaN payloads have canonical bit representations, although
non-finite simulation inputs are rejected.

Every movement-relevant collision request and validated result contributes to a
separate ordered collision transcript. Its terminator includes the query count,
so prefix collisions cannot masquerade as a complete transcript. The final
view-origin contents query used only for screen blend/renderer flags travels
through a separate non-hashing callback.

Replay-chain hashes bind the prior chain, exact command sequence, canonical
command hash, collision transcript, and resulting state hash. This detects
otherwise invisible command reorder/duplication while remaining a diagnostic,
not a security primitive.

## Cgame Integration

Cgame converts the authoritative player movement state into ABI v1, adapts
engine traces through out-parameters, maps entity pointers to checked numeric
indices immediately, and passes an explicit config copy for every command.

The 128-entry prediction history now stores the exact command sequence, full
canonical state, state hash, collision hash, config hash, and replay-chain hash.
Slot reuse is accepted only when the retained sequence matches. Configuration
hash changes suppress comparisons against an incompatible prediction history.

Reconciliation compares every canonical movement field plus the state hash.
Only position error is visually smoothed; collision state always restarts from
the newly authoritative snapshot. Packet acknowledgement was the explicitly
provisional watermark in this T02 slice. The subsequent T08/T09 range import
uses the server-consumed command cursor after authority is established and
permits packet ACK only for tagged bootstrap or legacy fallback.

## Sgame Integration

Authoritative movement now copies `pm_config` into each native `PMove` call.
Haste has one persisted representation, `PMF_HASTE`; the duplicate Boolean was
removed from both C and C++ movement state. Raw `memcmp` of padded movement
state was replaced with fieldwise semantic comparison.

Sgame still uses native trace records because gameplay touch dispatch needs the
real current entity and trace. This is intentional: the shared movement code is
the same archive, while damage, triggers, linking, weapons, and touch side
effects stay outside the deterministic movement-state contract.

## Validation and Evidence

Final checked-in evidence is generated by
`tools/networking/run_prediction_parity_baseline.py` and written locally under
`.tmp/networking/`. Rebaselining requires the explicit `--rebaseline` switch and
an intentional movement-model/schema review.

The runner treats prediction and live-wire command evidence as one reviewed
contract. It repeats both executables, validates the exact six focused movement
fixtures and five protocol/message rows, and embeds both reports in the same
golden object. Evidence records normalized matrix/golden paths and SHA-256
hashes. A rebaseline run truthfully records `golden_verified: false` and
`rebaselined: true`; only a separate matching run records verification.

The completed local evidence records:

- C11 and C++20 schema-layout tests;
- ABI-client versus native-server-core state parity after every command;
- independent collision-transcript repeatability;
- movement-mode, collision, timer, configuration, and duration scenarios;
- authoritative correction followed by modular replay across `UINT32_MAX`;
- malformed input/callback and replay-range capacity fail-closed cases;
- three-run candidate creation followed by an independent three-run golden
  verification, including the live-wire report;
- successful C11/C++20 layout, prediction replay, and live-wire Meson tests;
- successful client engine, dedicated engine, cgame, and sgame builds.

The reviewed local candidate golden under `.tmp/networking/` has SHA-256
`f0988c10d475f9ff82be9e1348a79ccb44182c467fe7ec8db8dc5f2fe76713d9`;
the scenario matrix has SHA-256
`569f99f103a747d75e60012e6a42c292f938b84b31285619dcd5d10d553ad32e`.
The standalone repeatable wire evidence is
`.tmp/networking/usercmd-live-wire-parity.json` with SHA-256
`57f3cb2a9f473e341b2197aa00bb6cd7781a6707a05ad017232a3bced5d35dcf`.
Project review accepted that candidate into
`tools/networking/baselines/prediction-parity-golden-v1.json` after an
independent three-run verification.

The complete prediction harness was then built independently on Linux under
WSL Ubuntu with GCC/G++ 13.3, C11/C++20, `-O2`, `-fno-fast-math`, and
`-ffp-contract=off`. Its 60,914-byte JSON report is byte-identical to the
Windows Clang report, including the non-cardinal angle/delta and full-output
fixtures. Both have SHA-256
`c3a9d43e9194db22690dce3783a6bfa797990dcdc14207a0a6e4802cb43f9011`.
This is exact Windows/Linux x86_64 evidence, not a claim about unexecuted
architectures.

The final Windows integration gate then used the normal configured build and
staged runtime, not an isolated harness:

```text
tools\meson_setup.cmd setup --native-file meson.native.ini --reconfigure \
  builddir-win -Dbase-game=basew -Ddefault-game=basew -Drmlui=enabled
meson compile -C builddir-win
meson test -C builddir-win --suite networking --print-errorlogs --repeat 3
python tools\refresh_install.py --build-dir builddir-win --install-dir \
  .install --base-game basew --platform-id windows-x86_64
meson compile -C builddir-win networking-runtime-smoke
```

The full build completed all 622 scheduled steps. The networking suite passed
33/33 tests across three repetitions. The install refresh copied current client,
dedicated, cgame, and sgame binaries, repacked 280 assets, and passed the staged
Windows payload validator.

`.tmp/networking/impairment-runtime.json` records the live staged gate. Both
default-off and impaired loopback sessions loaded the current modules, reported
game API 2025, connected with protocol 1038, reached `cs_spawned`, remained alive
until harness termination, and wrote empty stderr. The default-off path recorded
no impairment activity. The impaired path recorded `seen=975`, `dropped=7`,
`reordered=6`, `duplicated=7`, `upstream_stalled=572`, and `overflow=0`. The
dedicated queue self-test also passed its capacity-seven overflow/ordering/reuse
checks.

A separate staged dedicated launch of `mm-rage` loaded
`basew/sgame_x86_64.dll`, reported API 2025, initialized the map, and exited zero.
The public q2proto wire harness is the legacy-command compatibility gate for this
task: Vanilla MOVE, Q2PRO MOVE/BATCH_MOVE, and Q2REPRO MOVE/BATCH_MOVE all
round-trip through production adapters. Full legacy demo/MVD release coverage is
not relabelled as prediction evidence; it remains `FR-10-T13`.

## Remaining Risk and Follow-on Work

Movement arithmetic remains IEEE-754 float-based and still uses the established
trigonometric and square-root helpers. The shared object, canonical wire input,
strict floating-point build policy, total ordering, semantic hashes, and golden
corpus make drift observable and remove client/server build-mode skew, but this
is not a claim that all platforms implement a fixed-point simulator. A future
movement-model revision may adopt fixed-point or table-driven math only with
movement-feel, performance, and compatibility evidence.

The fixed 512-token bridge capacity is intentionally bounded and fails closed.
`FR-10-T14` must measure its high-water use and the prediction-core CPU cost in
the mandatory load profiles.

## Native command-observation integration update (2026-07-14)

`NetUsercmd_ToPredictionCommandV1()` is now the shared alias-safe production
conversion used when the client builds the one-command native observation and
when server-side command code requires the same prediction shape. The client
records every finalized command but selects only the newest member of the exact
successfully written legacy range. This reuses the T02 command semantics; it
does not make native DATA authoritative or extend completed `FR-10-T02` to
weapon/gameplay prediction. The transport boundary is documented in
`docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
