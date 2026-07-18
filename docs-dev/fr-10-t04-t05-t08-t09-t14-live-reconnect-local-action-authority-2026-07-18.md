# Live reconnect local-action authority acceptance

Date: 2026-07-18

Project tasks: `FR-10-T04`, `FR-10-T05`, `FR-10-T08`, `FR-10-T09`,
`FR-10-T14`

Status: default-off local-action shadow authority now has repeated live,
cross-process reconnect evidence. No weapon prediction or presenter authority
is promoted.

## Outcome

The `blaster-local-action-lease` runtime mode now proves the complete private
local-action receipt path across an in-session client reconnect. Each repeat
launches a dedicated server and two network-only headless clients, admits both
players, disconnects and reconnects the shooter, rebases command identity into
epoch 3, then sends an ordinary client-side `+attack`. Production
`ClientThink`, post-command weapon lease, Blaster callback, projectile,
current-world collision, damage, native event delivery, cgame correlation,
semantic ACK, and server release paths remain authoritative.

The acceptance fixes and proves four lifecycle boundaries:

1. Cgame shutdown resets mutable local-interaction state but retains the
   immutable engine import tables needed by the reloaded cgame. The reconnect
   therefore keeps the V2 exact-command range available without retaining an
   old prediction ledger.
2. `WORR_CGAME_COMMAND_RECORD_IMPORT_V2` exposes exact finalized canonical
   records by command ID. `Worr_CommandRecordInputHashV1` hashes the semantic
   input identity while deliberately excluding the render watermark, so the
   independently produced client record and authoritative receipt compare on
   gameplay input rather than presentation timing.
3. The sgame observation layer can select only the newest exact joined,
   attack-bearing shadow inside the probe's already admitted command interval.
   It never extends the command lease or changes projectile authority. Once
   found, the fixture retains immutable value copies of the scoped, leased,
   joined, and shadow evidence. This prevents a slow RCON diagnostic from
   missing the row after the bounded observation ring rotates.
4. The generated collision fixture now contains four separated deathmatch
   spawns. The gate uses the existing cheat-gated `dev_ready` developer fixture
   command before reconnect to suppress the unrelated two-player intermission
   transition. The bypass resets on map load and does not construct commands,
   choose rewind time, call a weapon, apply damage, or affect receipt authority.

The receipt-boundary parity logger is registered as an optional event-runtime
observer. Event admission therefore does not acquire a hard link dependency on
the prediction/UI translation unit; production prediction registers the real
reporter, while focused event-runtime binaries can link and exercise the same
authority path with a counting observer.

`cl_headless 1` suppresses client presentation initialization while retaining
the real client network, command, cgame, and native-carrier paths. Input and
audio remain disabled and no window, renderer, input device, or mouse capture
is initialized by this automated gate.

## Three-repeat evidence

Machine-readable report:
`.tmp/networking/local-action-authority-reconnect-acceptance-final.json`

Schema: `worr.networking.canonical-weapon-damage-runtime.v32`

| Run | Exact command | Shadow hash | Receipts/matches | Conflict/mismatch/resync | Damage |
|---:|---:|---:|---:|---:|---:|
| 1 | `3:132` | `6655587289248463807` | `17/17` | `0/0/0` | 15 |
| 2 | `3:135` | `3639309320349105130` | `14/14` | `0/0/0` | 15 |
| 3 | `3:133` | `1091851830527945720` | `14/14` | `0/0/0` | 15 |

Every repeat also proves:

- exactly three server admissions, two shooter `serverdata` handshakes, and
  the reconnect-specific `dev_ready` minimum-player bypass;
- two playing clients plus retained pre-attack history before input;
- canonical scope, attack receipt, the normal Blaster callback, current-world
  projectile forward, unchanged collision authority, and exact 15 damage;
- local-action catalog ID `1`, shadow flags `127`, V2 blocker mask `4367`, and
  a nonzero exact record hash;
- catalog and lease readiness, exact scoped and leased records, exact timer
  continuity, a joined record, at least one bounded expiry, and zero lease
  rejection; and
- no unmatched or outstanding receipt, authority mismatch, authority conflict,
  or shared-runtime resync.

The repeat gate compares stable proof semantics across all runs. Command
sequence, observation counters, and record hash legitimately vary with process
scheduling and are not treated as deterministic wall-clock values.

## Focused verification

```powershell
python -m unittest `
  tools.networking.test_lag_compensation_canonical_rail_contract `
  tools.networking.test_run_canonical_rail_damage_runtime_gate
ninja -C builddir-win sgame_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install `
  --base-game basew --platform-id windows-x86_64
python tools/networking/run_canonical_rail_damage_runtime_gate.py `
  --client-exe .install/worr_x86_64.exe `
  --dedicated-exe .install/worr_ded_x86_64.exe `
  --working-dir .install `
  --output .tmp/networking/local-action-authority-reconnect-acceptance-final.json `
  --weapon blaster-local-action-lease --repeat 3 --timeout 45
```

The Python source/runtime contracts pass `91/91`, sgame compiles and links,
the refreshed Windows x86-64 stage validates, and the three live repeats pass.

Final integration verification adds:

- the complete 265-target production build passes after the optional parity-
  observer seam removes an otherwise hidden focused-test link dependency;
- the full networking invocation passes 156/157 rows, including both
  100,000-snapshot corpus repetitions in 638.10 seconds; its only failure is
  the real-BSP runner's stale expected hash for the intentional four-spawn
  fixture update;
- after updating that versioned fixture sentinel, the failed real-BSP parity
  row passes 1/1 with the new hash;
- package plus release-headless contracts pass 18/18; and
- the final Windows x86-64 stage validates 16 root runtime files, one
  dependency, 525 packaged assets, 31 botfile payloads, 215 RmlUi assets, and
  one q2aas reference map.

## Authority and remaining boundary

This milestone closes the previously open live in-session reconnect rebase and
cross-process local-action receipt-parity gaps. It does not close a parent
FR-10 task. All 22 ordinary weapons remain shadow-only because every exact V2
blocker mask is nonzero. Predictable weapon transactions, audiovisual
suppression and de-duplication, sustained correction budgets, full adapter
load, malformed-input scale, demos/spectators, soak, and cross-platform release
acceptance remain open. `q2proto/` is unchanged.
