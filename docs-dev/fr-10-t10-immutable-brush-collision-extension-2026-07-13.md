# FR-10-T10 Immutable Brush Collision Extension

Date: 2026-07-13

Project tasks: `FR-10-T10` (primary), `FR-10-T14` (real-fixture integration
evidence), with future integration boundaries for `FR-10-T11` and
`FR-10-T12`.

## Outcome

Stage A adds a versioned engine-to-game collision extension for resolving an
immutable BSP inline model into a map-scoped asset and tracing that asset at an
explicit caller-supplied transform. The provider never relinks, unlinks, or
temporarily edits a live edict. It does not yet capture movers, alter sgame lag
compensation, or promote any historical collision result into gameplay.

Stage B adds the previously deferred real-BSP parity gate. A deterministic
Quake II IBSP v38 is generated under `.tmp/`, loaded through the production
`BSP_Load`/`CM_LoadMap` code, and traced by both the extension and the actual
`SV_Clip` implementation at identical transforms. The gate covers geometric
results and guards an active, linked server-edict sentinel byte-for-byte across
both success and rejection paths.

This is the safe collision primitive needed by the later mover-history work in
`FR-10-T10`. Hitscan promotion remains `FR-10-T11`; projectile, trigger, and
other interaction policies remain `FR-10-T12`.

## Added ABI

`inc/shared/rewind_collision.h` defines
`WORR_REWIND_COLLISION_IMPORT_V1` and byte-checked C/C++ layouts for:

- a 16-byte immutable asset handle containing map epoch, asset ID, and asset
  hash;
- a 24-byte map identity containing the authoritative snapshot/map epoch,
  advertised collision-map checksum, and inline-model count;
- a 64-byte pointer-free asset descriptor with source model index and local
  bounds;
- a 104-byte explicit transformed-trace request;
- a pointer-size-native import table exposing `GetMapIdentity`,
  `ResolveInlineBrush`, and `TraceTransformed` (32 bytes on x86-64 and 20 bytes
  on x86), with relative callback offsets checked in C and C++.

The import is process-local. It is not a protocol structure and must not be
serialized into legacy snapshots or demos. `trace_t` remains the result type so
the game receives the same planes, contents, and map-owned surface metadata as
an ordinary engine trace. The provider always clears `trace_t::ent`; sgame must
attach only a current generation-validated entity in a later stage.

## Asset identity and lifecycle

The asset ID is the BSP inline-model ordinal. ID zero is reserved for invalid
or world collision and is rejected. A usable identity is the complete tuple:

```text
(server-owned map epoch, inline-model ordinal, stable asset hash)
```

The engine alone maps the game/configstring model index to the BSP ordinal. It
explicitly rejects the reserved player model index and accounts for the model
namespace hole when inline model ordinals reach that boundary. The inverse
mapping is also validated while constructing a descriptor.

The asset hash uses byte-order-independent FNV-1a accumulation over a domain
tag, schema version, epoch, asset ID, asset kind, source model index,
authoritative/advertised collision-map checksum, and canonical local bounds.
Negative zero is canonicalized before hashing. A map transition changes the
authoritative epoch, so a retained handle is rejected even if a later map
happens to use the same model ordinal and geometry.

Map and asset results are available only while an actual collision map is
loaded in `ss_loading` or `ss_game`. This permits server-owned resolution while
level entities spawn without treating cinematics, pictures, or an unloaded
server as collision maps.

## Non-mutating trace path

`src/server/rewind_collision.c` rebuilds the requested descriptor from the
current immutable BSP catalog and requires the supplied handle to match it
exactly. It then calls:

```text
CM_TransformedBoxTrace(
    request start/end/mins/maxs,
    immutable inline model headnode,
    request contents mask,
    request origin/angles,
    current server extended-collision mode)
```

It does not accept an edict pointer and does not call `SV_LinkEdict`,
`SV_UnlinkEdict`, `SV_HullForEntity`, or `CM_HeadnodeForBox`. The only pointers
that cross back in a successful result are the normal engine-owned collision
surface pointers already present in `trace_t`; no entity pointer is returned.

All callbacks construct results in local temporaries. Invalid schemas,
reserved fields, stale epochs, unknown IDs, hash mismatches, non-finite vectors,
inverted extents, overlapping request/output storage, invalid engine trace
fractions, and unavailable maps return `false` without modifying caller-owned
output.

`src/server/game.c::PF_GetExtension` contains the only integration change: a
lookup branch returning `SV_RewindCollisionImportV1()` for the new extension
name.

## Focused validation

The following standalone test sources were added for build-system integration:

- `tools/networking/rewind_collision_layout_c.c`
- `tools/networking/rewind_collision_layout_cpp.cpp`
- `tools/networking/server_rewind_collision_test.c`
- `tools/networking/generate_rewind_collision_bsp_fixture.py`
- `tools/networking/run_rewind_collision_real_bsp_test.py`
- `tools/networking/rewind_collision_real_bsp_test.c`

The provider test links the real provider against the actual server data
structures with controlled `sv`, `svs`, BSP model catalog, and a capture stub
for `CM_TransformedBoxTrace`. It verifies:

- import version and callback presence;
- map identity and inline-model count;
- deterministic asset descriptors and hashes;
- the reserved model index and inline-model namespace hole;
- stale epoch, world/zero model, range, null, and unavailable-map rejection;
- exact forwarding of the immutable headnode, contents mask, trace volume, and
  explicit origin/angles;
- current extended-collision mode forwarding;
- clearing of a sentinel edict result while retaining map surfaces;
- transactional rejection of bad hashes, bad epochs, reserved bytes, NaNs,
  inverted bounds, request/output aliasing, and invalid collision output;
- changed handle identity after a map-epoch transition.

### Stage B real-BSP parity gate

`generate_rewind_collision_bsp_fixture.py` creates a deterministic,
collision-only Quake II IBSP v38. No compiled BSP is checked in. The runner
generates it twice in memory, requires byte identity, pins its SHA-256, and
stages the result only at:

```text
.tmp/networking/rewind_collision_real_bsp/basew/maps/rewind_collision_parity.bsp
```

The existing WORR-owned `worr_crouch_ref.bsp` was considered first, but it has
only the world model and therefore cannot exercise translated or rotated inline
brushes. Optional stock maps in `.install/` are not a reliable clean-checkout
dependency. The generated fixture is consequently the smallest reproducible
asset that covers the required inline-model behavior without importing game
data.

Fixture identity:

```text
size       1228 bytes
SHA-256    e2bb2511bc17d637ee60ba08c9c51133bba97bb6c3ca7a9930b2d3f8d5c82ca6
IBSP       version 38
models     world plus two inline models
contents   CONTENTS_SOLID and CONTENTS_WATER
surfaces   fixture/solid and fixture/water
```

The native probe links the real production translation units for BSP loading,
collision tracing, server clipping, and the rewind provider:

```text
src/common/bsp.c
src/common/cmodel.c
src/server/world.c
src/server/rewind_collision.c
```

It replaces only unrelated process services—virtual filesystem reads, console
output, cvar storage, and zone allocation—with narrow standalone adapters. It
does not replace `BSP_Load`, `CM_LoadMap`, `CM_BoxTrace`,
`CM_TransformedBoxTrace`, `CM_ClipEntity`, `SV_HullForEntity`, or `SV_Clip`.
Thus the final geometric result is never supplied by a trace stub.

For each case the probe resolves the extension asset, builds one explicit
transform, calls `TraceTransformed`, calls production `SV_Clip` against the
same inline model at the same transform, and compares exact float bits and
collision metadata. The ten-case matrix covers:

| Case | Coverage |
|---|---|
| `translated-ray-hit` | Translated solid inline brush, point sweep, primary plane and solid surface |
| `rotated-ray-hit` | Translated/yaw-rotated brush and world-space plane rotation |
| `translated-box-hit` | Non-zero asymmetric box sweep |
| `rotated-box-hit` | Pitch/yaw/roll transform with a box sweep |
| `corner-two-plane-ray` | Primary and secondary planes/surfaces |
| `start-solid-exit` | Start-solid without all-solid |
| `all-solid-stationary-box` | Position test, all-solid, zero fraction, solid contents |
| `solid-mask-rejection` | Solid brush excluded by a water-only mask |
| `rotated-water-ray-hit` | Second inline asset, rotated water hit, water surface and contents |
| `water-mask-rejection` | Water brush excluded by a solid-only mask |

Successful parity compares `allsolid`, `startsolid`, fraction, end position,
both planes, both map-owned surface pointers, contents, and entity policy. The
extension must return `ent == NULL`; the reference must return the selected
clip edict. Surface names, surface IDs, and expected solid/water contents are
also asserted independently so two identically wrong empty traces cannot pass.

The probe marks a production-layout edict active and linked, gives its
`server_entity_t` area link and visibility fields non-default sentinels, and
snapshots both complete structures. Every extension trace, reference trace,
and rejection must leave both structures byte-identical. This detects edict
editing, link/unlink activity, link-count changes, transform writes, bounds
writes, and server-area-link changes.

Rejection coverage corrupts the asset hash, request epoch, current map epoch,
and authoritative collision checksum. Each must return `false`, preserve the
caller trace byte-for-byte, and leave the edict/link sentinels unchanged.

Meson/Clang validation against the generated Windows configuration passed:

```text
meson compile -C builddir-win rewind_collision_real_bsp_test
    14/14 compile/link steps passed

meson test -C builddir-win network-rewind-collision-real-bsp-parity --print-errorlogs
    1/1 passed

native probe
    engine collision checksum 0xee4dba20
    10/10 geometric parity cases passed
    4/4 identity rejection classes passed transactionally
```

Machine-readable evidence is written to
`.tmp/networking/rewind_collision_real_bsp/report.json`. Existing Stage A
scratch outputs remain under `.tmp/networking/rewind-collision-stage-a/`.

## Fixture and integration boundary

The generated BSP intentionally has collision lumps but no visibility or render
geometry. It is a deterministic engine-collision fixture, not a playable map.
This keeps the gate independent of proprietary Quake II data and avoids
checking generated binaries into source control. The loader emits its expected
"no Visibility" warning; that does not alter collision behavior.

The gate proves exact provider/`SV_Clip` parity for convex inline BSP brushes
under the current extended-collision mode. It does not claim coverage for a
campaign map's complex multi-brush mover, areaportal behavior, BSPX/extended
map formats, malformed BSP fuzzing, concurrent collision calls, or long-running
load/soak behavior. Those remain separate promotion evidence. The active edict
is a production-layout test sentinel rather than an edict spawned by sgame;
this test deliberately does not introduce mover capture or gameplay authority.

## Compatibility and deferred work

- No files under `q2proto/` changed.
- No snapshot, command, demo, or legacy wire layout changed.
- No sgame mover capture, lag-compensation cvar, current-world mover exclusion,
  or gameplay promotion was added.
- No roadmap state was changed by this bounded implementation.
- Meson registers the provider in both engine source sets and exposes focused
  provider, C/C++ layout, and real-BSP `SV_Clip` parity tests through the
  `networking` suite.

The next safe `FR-10-T10` slice is server-owned mover pose capture and audit-only
historical tracing using this extension. Authoritative hitscan promotion must
still wait for mover-history discontinuity, complex-map, load/soak, and latency
evidence; the deterministic real-BSP geometric parity and mutation guard are
now green.
