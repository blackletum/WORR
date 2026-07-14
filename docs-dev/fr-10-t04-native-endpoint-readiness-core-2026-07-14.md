# FR-10-T04 Native Endpoint Readiness Core

Date: 2026-07-14

Task: `FR-10-T04`

Status: Readiness state machine implemented, tested, and now owned by the
default-off production adapters linked below. Native capability remains
excluded from public offer/support masks.

## Problem

The existing capability confirmation is server selection, not proof that the
client received the confirmation and installed its native receive resources.
In particular, `worr_capability_confirm_sent` means that the server queued the
three confirmation settings. It cannot safely authorize server-native
transmission. A native packet could otherwise arrive before the client has an
RX adapter, and a client-native packet could reorder ahead of its readiness
message at the server.

`FR-10-T04` therefore needs a separate, explicit endpoint barrier. Capability
selection answers *which features were negotiated*; readiness answers *which
direction may use those features now*.

## State and proof record

`inc/common/net/native_readiness.h` defines a pointer-free V1 core with distinct
server and client roles. Its exact transition is:

```text
server: WAIT_CLIENT_READY --CLIENT_READY--> SERVER_ACTIVE
client: WAIT_CHALLENGE --CHALLENGE--> WAIT_SERVER_ACTIVE
client: WAIT_SERVER_ACTIVE --SERVER_ACTIVE--> CLIENT_ACTIVE
```

The 32-byte proof record binds:

- record kind (`CHALLENGE`, `CLIENT_READY`, or `SERVER_ACTIVE`);
- nonzero transport epoch;
- the exact negotiated capability mask, including the native bit;
- a caller-supplied nonzero 64-bit readiness nonce;
- a canonical little-endian CRC32 and zero reserved field.

The checksum detects corruption and is not authentication. The nonce is a wire
barrier identity and is deliberately separate from the process-local,
nonserialized `connection_owner_id`. A future live server must supply a fresh
nonce from a process-salted, monotonic source.

## Direction gates

The core exposes separate receive and transmit predicates:

- client RX becomes available after it accepts the exact challenge and before
  it emits `CLIENT_READY`;
- server RX and TX become available only after exact `CLIENT_READY`;
- client TX becomes available only after exact `SERVER_ACTIVE`.

Both predicates take the current monotonic tick and are mutating admission
gates, not passive phase queries. They apply the same sticky clock-regression
and deadline checks as the explicit deadline API immediately before evaluating
the phase. Live RX/TX adapters therefore cannot remain armed past a deadline
merely because their owner forgot to run a separate timeout poll.

The final echo is essential. Without it, a client WTC1 packet could overtake the
reliable readiness tuple and cause the server to reject or stale-drop the proof
that was supposed to arm the connection.

An armed TX hook must still return `BYPASS` and create no retained token while
`CanTransmitNative` is false. A receive hook may pass no-carrier legacy bytes,
but a matching native candidate observed before the direction's receive gate
must fail closed.

## Failure, duplicate, and lifecycle rules

Exact duplicate proof records are idempotent. Duplicate challenge and
client-ready observations reproduce the exact response record so a lost reply
can be retransmitted without reopening the transition. A duplicate active echo
is accepted without another transition.

The following peer/runtime failures are sticky until reset or a new connection:

- invalid/corrupt proof record;
- wrong role, order, kind, epoch, capability mask, or nonce;
- monotonic-clock regression;
- readiness deadline expiry.

Waiting phases use caller-supplied nonzero timeouts and fail at
`now_tick >= deadline_tick`. Epoch advancement is in-incarnation only: the epoch
and generation must increase, the server nonce must increase, and the client
retains the previous nonce as a floor. Nonce, epoch, deadline, and generation
exhaustion fail without wrap; `UINT32_MAX` is an explicit terminal epoch state.
A fresh connection uses a newly initialized object, a new process-local owner,
and a globally non-reused transport epoch. That last invariant is supplied by
the lifecycle owner because this pointer-free core cannot distinguish a new
object from a replayed connection incarnation on its own.

All externally mutable records are fixed-width and pointer-free. Init, advance,
and observe APIs reject overlapping state/input/output regions. Invalid
arguments and preflight failures leave state and outputs unchanged; a valid but
hostile peer record deliberately commits the sticky failed phase while leaving
response output untouched. Counters saturate at `UINT64_MAX`.

## Validation

The focused tests cover:

- record CRC and every bound-field mutation;
- the complete role-specific gate transition;
- exact duplicate response replay before and after activation;
- corrupt, wrong-kind, wrong-role, wrong-epoch/mask/nonce, and stale-nonce
  failures;
- sticky failure and output transactionality;
- deadline boundary, clock regression, and deadline overflow;
- exact-deadline and clock-regression failure through the live RX/TX gates;
- server/client epoch advancement, nonce/generation exhaustion, and rollback
  rejection;
- reconnect replay rejection under the globally unique epoch contract and
  explicit terminal epoch exhaustion;
- overlapping arguments, output preservation, counter saturation, deep state
  validation, reset, and C++ fixed-record layout.

Configured Windows Clang tests pass. Strict C/C++, ASan/UBSan, Clang static
analysis, repeated execution, and i686 syntax/layout checks also pass.

## Remaining integration at core validation

This core intentionally does not choose a cvar, advertise a capability,
allocate native sessions, install hooks, or serialize settings. The next
readiness slice must encode the three records into adjacent packet-bounded
legacy settings, then connect lifecycle ownership to client/server netchan
setup and teardown. Only after both local adapter setup and the peer proof are
complete may the live masks include `WORR_NET_CAP_NATIVE_ENVELOPE_V1`.

Once native was confirmed for an epoch, readiness failure must disconnect or
reconnect with a fresh offer; it must never silently reinterpret the current
stream as legacy.

## Production integration update (2026-07-14)

This state machine is now owned by the default-off production adapters and
binds current/retired native transport banks after the exact private proof.
The reliable-queue commitment points are irreversible for hook ownership. A
failed same-epoch ACTIVE transition stays in DRAIN and cannot later resurrect
DATA; only an accepted fresh map challenge after explicit quiesce can restore
arming. Native capability remains unadvertised and legacy authority unchanged.
See `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`.
