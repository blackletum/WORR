# FR-10-T10 live mover-pose history

Date: 2026-07-15

Project task: `FR-10-T10`

Status: implemented bounded live-capture and immutable-scene increment.
The subsequent sealed historical-brush dispatch increment is documented in
`docs-dev/fr-10-t10-sealed-historical-brush-dispatch-2026-07-15.md`.

## Outcome

The server game now records map-owned moving BSP poses alongside the existing
player-pose history. A player pose records a generation-checked ground-mover
reference plus its mover-relative origin and angles when the current ground
entity is an eligible mover. Canonical rewind scenes then include each eligible
mover's immutable brush-model pose before they are sealed.

This supplies the provenance required for a deliberate historical brush query.
The subsequent dispatch keeps the world BSP authoritative, excludes only
matching live movers from the current-entity baseline, and traces their sealed
immutable BSP poses without changing an edict.

## Capture contract

`LagCompensation_RecordMovers()` runs in the normal deathmatch frame path after
`ClientEndServerFrames()`. This gives player and mover records the same final
`level.time` capture boundary, after pusher movement, player end-frame stance,
bounds, linkage, and ground contact are finalized.

An eligible entity is deliberately narrow:

- in use, linked, non-client, and `SOLID_BSP`;
- a `MoveType::Push` or `MoveType::Stop` entity;
- an inline game model rather than the world or player model.

Each eligible mover must resolve through the engine's existing immutable inline
BSP provider at the current map epoch. The captured pose uses the resulting
map-local asset ID, local bounds, transform, collision flags, and stable
entity-generation reference. Failure to resolve the current map or one mover
asset rejects that mover pose; it never invents an asset, maps it to another
epoch, or writes a live edict during rewind.

The storage is fixed and allocation-free: 64 mover tracks × 64 poses × the
160-byte common pose ABI, or 640 KiB of pose storage. If all tracks are busy,
the capture increments a distinct exhaustion counter and does not reuse an
active entity's history. Stale entity references are the only reusable tracks.

## Scene and map safety

When the engine collision provider first supplies a map epoch, sgame adopts it
through the same map-epoch boundary used by canonical command context. Existing
player histories are relabeled only during the controlled initial adoption;
unannounced transitions reset histories. Mover tracks follow the same rule.

The scene roster is ordered by entity number and now contains eligible players
and eligible captured movers. A live mover without a matching mover history
fails scene construction closed, causing the existing uncompensated query
fallback rather than producing a player pose that refers to an absent mover.
Brush poses require only `LINKED` to enter the scene; player targets still
require both `LINKED` and `DAMAGEABLE`. The common rewind scene validator then
checks every `HAS_MOVER` relationship before the scene is sealed.

The engine-facing collision header is not directly importable from modern
sgame, because it imports the legacy C game ABI. The local
`rewind_collision_import.hpp` bridge mirrors the fixed provider and trace
request layout with compile-time size and offset checks. Its transformed-trace
callback is used only by the later sealed-scene dispatch; this capture increment
continues to establish the exact identity/map/asset provenance it requires.

## Validation

The focused source contract covers the bounded mover eligibility and asset
resolution, player mover-relative provenance, map-epoch adoption, history
append, sealed-scene inclusion, final frame placement, and the later
historical-brush replacement ordering. It complements the existing executable
rewind core test, which verifies brush-model candidates and player-to-mover
scene validation at the common ABI level.

Focused Windows x86-64 validation passed:

```text
ninja -C builddir-win sgame_x86_64.dll

meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-rewind-core \
  network-rewind-acceptance-probe \
  network-lag-compensation-mover-capture-contract

meson test -C builddir-win --suite networking --print-errorlogs
```

The focused mover gates passed 3/3, their three-repeat run passed 9/9, and the
full networking suite passed 129/129. The refreshed `.install` tree also passed
the staged dedicated-server command-gap gate for both 161- and 401-command
recovery cases, with one bounded fast-forward, exact cursors, and zero core or
policy rejections per case.

No interactive client is launched. The module build is compilation evidence;
the tests are standalone networking binaries or a Python source-wiring
contract; the staged gate invokes only `worr_ded_x86_64.exe`.

## Remaining promotion gates

`FR-10-T10` remains open. This increment does not yet:

- compose player-relative poses through mover transforms for a collision query;
- cover rotating/complex BSP or BSPX assets beyond the provider's existing
  fixture, engine trace/damage scenarios, sustained load, or release platforms;
- define weapon-by-weapon moving-platform fairness policy; or
- promote the current default-off historical path.

The next technical promotion must add direct moving-mover runtime evidence and
fairness policy to the sealed dispatch. It must retain live-world immutability,
use only sealed scene poses, and demonstrate player-on-mover/blocker behavior
before any broader gameplay promotion.

## Roadmap completion

- Overall strategic roadmap: **68/180 complete (37.8%)**; **112 open**.
- `FR-10`: **3/16 complete (18.75%)**; **13 open**.
- This is a subtask increment; no parent checkbox is closed.
