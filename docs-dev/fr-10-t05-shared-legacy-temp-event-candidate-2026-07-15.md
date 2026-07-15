# FR-10-T05 Shared Legacy Temporary-Event Candidate Constructor

Date: 2026-07-15

Project task: `FR-10-T05`

## Outcome

The temporary-entity conversion that formerly lived only in the cgame decode
shadow is now a common C constructor. It consumes the already-decoded
`q2proto_svc_temp_entity_t` shape, maps every supported legacy temporary-event
subtype into `worr_event_payload_legacy_temp_v1`, and returns the raw source
and subject entity indices required for later lineage binding.

`src/client/event_shadow.cpp` now uses this constructor before delivering an
action candidate into the existing cgame V2 range builder. That builder remains
the sole cgame owner of observed entity generations: it binds the returned
indices immediately before canonical record validation and publication. The
constructor therefore returns an intentionally unresolved action template,
never a guessed authoritative entity reference.

## Contract and safety boundary

- All output pointers are required and remain unchanged on failure.
- Unsupported subtypes, invalid Steam shapes, out-of-range entity references,
  and non-finite or otherwise invalid payload data fail closed.
- The constructor validates the completed ABI record against a temporary valid
  world source and then restores unresolved source/subject references. This
  checks the payload and record contract while preserving the existing cgame
  action-template invariant.
- Source-bearing temporary effects and Lightning's subject entity are returned
  as raw indices only. The helper does not infer generations, event IDs,
  transport identity, authority, or presentation order.
- `q2proto/` is consumed through its public decoded-structure header only; no
  `q2proto/` source or protocol behavior changed.

## Why this precedes server expansion

The current default-off native event producer is intentionally limited to
legacy entity events copied from the server's final per-peer snapshot emission.
Temporary effects, muzzle flashes, and sounds cannot safely be reconstructed
from packet bytes or a global pre-visibility stream. This shared constructor is
the reusable canonical mapping needed by a later per-client final-emission
capture seam; it does not add native server production, alter legacy packets,
or cut over effect/audio presentation.

## Verification

- Focused headless constructor plus cgame range tests: 2/2 pass.
- Production cgame, client engine, dedicated server, and sgame modules: pass.
- Full headless networking suite: 122/122 pass.
- Three consecutive headless suite repetitions: 366/366 pass.

Roadmap task completion is unchanged: `FR-10` remains 3 of 16 complete; this
is a documented in-progress `FR-10-T05` foundation rather than task closure.
