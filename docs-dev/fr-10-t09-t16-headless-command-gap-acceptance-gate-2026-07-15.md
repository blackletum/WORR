# Headless command-gap acceptance gate

Date: 2026-07-15

Project tasks: `FR-10-T09`, `FR-10-T16`

Status: implemented and passed on the staged Windows x86-64 dedicated server.
This is a production-policy acceptance slice, not completion of either parent
task.

## Purpose

The bounded command-gap repair changed a real server failure mode: a credible
packet-history gap larger than the 128-slot command-retention ring used to
disconnect a client even though its canonical identity range was valid. The
shared command-stream tests already cover the mathematical and transactional
core, and previous long-running runs observed live recovery, but neither gave
CI a small, machine-readable proof that the production server function takes
the large-loss branch and publishes clean recovery counters.

This gate fills that evidence gap without launching a client window. It uses
the dedicated-server binary only, verifies the exact production
`SV_WorrFillCommandGap` function, and emits an atomic report under
`.tmp/networking/`.

## Production boundary exercised

`src/server/user.c` now exposes the operator-only
`sv_worr_command_gap_selftest` command. It creates a zero-initialized,
stack-owned `client_t`, initializes the normal 128-slot canonical command
stream behind cursor `{ epoch=1, sequence=1000 }`, and passes a valid one-item
legacy sideband range whose first identity follows either a 161-command or a
401-command gap.

Both cases select `simulation_budget=0`. That is the exact large-loss policy
used by `SV_OldClientExecuteMove` and `SV_EnhancedClientExecuteMove` when
`net_drop >= 20`: the server must not replay an unbounded prefix into gameplay;
it fast-forwards the already-lost identities in bounded common-core work.
No game callback is invoked by the test. `sv_client` and `sv_player` are saved
and restored before the result is printed, so it cannot borrow or mutate a
live client context.

Each status line proves all of the following:

- the range is accepted by the real server policy with the 4,096-command cap;
- the exact gap is skipped with zero synthesized commands;
- received and consumed cursors end at the expected identity;
- one fast-forward attempt and one fast-forward succeed;
- skipped-command accounting matches the requested gap;
- fast-forward rejection and gap-policy rejection remain zero; and
- the final command stream validates.

## Machine-readable runner

`tools/networking/run_command_gap_acceptance_gate.py` launches only
`worr_ded_x86_64.exe` with the self-test command and `+quit`. It rejects stale
success/failure pointers before launch, captures stdout/stderr, parses the two
strict status lines, validates every counter and cursor field, hashes the
dedicated executable and retained logs, then atomically writes:

```text
.tmp/networking/command-gap-acceptance.json
```

Failures write a sibling `.failure.json`; a pass pointer is absent unless the
entire acceptance transaction has completed. The runner is available as the
Windows Meson target `networking-command-gap-acceptance`, and its parser and
negative contracts run in the ordinary headless networking suite.

## Staged result

The staged dedicated binary passed both cases:

| Gap | Final cursor | Attempts / forwards / skipped | Rejections |
|---:|---|---|---|
| 161 | `{1,1161}` | `1 / 1 / 161` | `0 / 0` |
| 401 | `{1,1401}` | `1 / 1 / 401` | `0 / 0` |

The generated evidence was
`.tmp/networking/command-gap-acceptance.json` with schema
`worr.networking.command-gap-acceptance.v1`. Its captured dedicated binary
SHA-256 was `caa652d8670e359372f6f48c94aeab8b8e21ac5af8fefdb857b075ff6f32113c`;
stderr was empty.

## Validation

```text
meson setup builddir-win --reconfigure
ninja -C builddir-win worr_ded_engine_x86_64.dll
meson test -C builddir-win --no-rebuild --print-errorlogs \
  network-command-gap-acceptance-gate-parser

python tools/refresh_install.py --build-dir builddir-win --install-dir .install \
  --base-game basew --platform-id windows-x86_64

python tools/networking/run_command_gap_acceptance_gate.py \
  --dedicated-exe .install/worr_ded_x86_64.exe \
  --working-dir .install \
  --output .tmp/networking/command-gap-acceptance.json --timeout 30
```

The focused parser test passed `1/1`; the dedicated acceptance command passed
both required gaps with no graphical client launched.

## Scope remaining

This is intentionally narrower than a full client/server packet-loss proof. It
does not replace the legacy sideband parser/packet admission path, a real
client's input-age and retry cadence, the native adapter, adaptive batching and
redundancy, cgame correction/audit pairing, multi-client load/soak, or
cross-platform acceptance. Those remain required for `FR-10-T09`, `FR-10-T16`,
and the downstream release tasks.
