# RmlUi Round 78 Multiplayer Match Hub

Date: 2026-07-10

Tasks: `FR-09-T08`, `FR-09-T05`, `FR-03-T08`, `FR-09-T04`,
`FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Status: Accepted multiplayer match-hub slice. The parent RmlUi migration and
renderer-parity tasks remain active.

## Outcome

WORR deathmatch clients now enter a server-authoritative multiplayer match hub
instead of an intermediate welcome page or immediate automatic join. The same
hub returns from Escape during an active session and presents current server,
map, match, population, participation, voting, information, and administrative
choices in one branded overlay.

The implementation deliberately supports two presentation paths from one
sgame-published state contract:

- OpenGL opens the `dm_join` RmlUi route when the compiled runtime and active
  renderer bridge are available.
- Native Vulkan and RTX/vkpt never redirect to OpenGL. If their native RmlUi
  bridge is unavailable, the cgame JSON `dm_join` page renders the same match
  hub through the active native renderer.

This completes a substantial live multiplayer/session slice, but it does not
close `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, or the RmlUi cutover gates. Other
session controllers, complete renderer-native RmlUi parity, broader input and
layout automation, and legacy removal remain open.

## Runtime Architecture and Ownership

### Sgame remains authoritative

The sgame owns whether the match hub is open, whether it is the mandatory
first-connect choice, which participation actions are legal, and the current
match values shown by the UI. `OpenDmJoinMenu()` publishes a complete snapshot
through `ui_dm_*` cvars and then requests `pushmenu dm_join`.

`InitPlayerTeam()` publishes `ui_worr_match_hub=1` only for WORR deathmatch.
The client uses that server-authored capability together with active-session
and base-game checks; it does not infer deathmatch support from a possibly
stale local cvar. Coop, demos, other game directories, and legacy remote
servers retain the ordinary in-game menu path.

The ownership markers have separate purposes:

- `gclient_t::initialMenu.frozen` is the authoritative mandatory-choice state.
- `gclient_t::initialMenu.dmJoinActive` tells sgame that the live match hub is
  open and blocks relevant gameplay input.
- `ui_dm_menu_active` mirrors that ownership on the client so RmlUi and cgame
  JSON close/fallback paths can notify sgame correctly.
- `ui_worr_match_hub` advertises that the connected server supports the WORR
  match-hub command contract.
- `ui_rml_runtime_available` is a read-only client capability used before a
  cgame `pushmenu` is routed into RmlUi.

Both client ownership markers are cleared on disconnect/server change so a
session hub cannot leak into the next connection. An active match-owned RmlUi
route may be restored across a renderer/UI reinitialization while the network
session remains alive.

### Initial connect is a real choice

`match_auto_join` now defaults to `0`. A human who is not otherwise assigned
by tournament, force-join, owner, or bot policy begins in spectator freecam,
enters the frozen initial-menu state, and opens the match hub after the short
initial UI delay. The historical `dm_welcome` entry point now goes directly to
`dm_join`; there is no separate Continue screen.

While the initial hub is authoritative:

- movement, freecam rotation, attack, spectator-follow input, and other
  blocked actions remain frozen by the existing `IsBlockingUiMenuOpen()`
  boundary;
- Escape, Back, or the inventory command cannot silently dismiss the choice;
  sgame republishes and restores the hub if the client presentation vanished;
- a successful `team red`, `team blue`, `team auto`, or `team free` transition
  releases the freeze and closes the hub;
- `worr_dm_initial_spectate` handles the valid no-team-transition case where a
  newly connected spectator explicitly chooses Spectate. It initializes the
  spectator session, releases the freeze, and closes the hub without requiring
  a fake team change.

Server operators who intentionally want the historical immediate assignment
can explicitly set:

```cfg
set match_auto_join 1
```

This is the explicit `match_auto_join=1` compatibility override for operators
who want the historical immediate-assignment behavior.

That override is evaluated during initial team assignment. Existing
`match_force_join`, tournament assignment, bot assignment, and
`g_owner_auto_join` behavior remains separate.

### Escape asks the server for current state

During an active supported WORR multiplayer session, Escape now sends the
normal `inven` client command. Sgame handles that request, republishes a fresh
match snapshot, and opens the hub. This keeps team counts, join legality,
ready state, tournament state, voting availability, and intermission behavior
authoritative instead of reopening a stale client-only page.

After the initial choice, `worr_dm_join_close` is allowed to close the hub and
return to play. Closing either the root hub or a child route clears the client
marker and notifies sgame so `dmJoinActive` cannot remain latched after the
visible menu disappears. `inven` and the match-hub information/close commands
are permitted during intermission so the same server-owned path remains usable
there.

## Match, Team, and Intermission Behavior

The snapshot derives display text and action availability from live sgame
state:

- match state covers startup, warmup reasons, ready-up, countdown, in-progress,
  timeout, completion, and intermission;
- FFA exposes Join Match, while a full Duel exposes Join Queue when the duel
  queue is enabled;
- team modes expose Red, Blue, and Auto Assign with current team counts;
- Spectate remains an explicit first-connect and in-session action;
- ready-up exposes a live Ready Up/Mark Not Ready command while applicable;
- match lock and full public slots disable joining and explain why;
- tournament participation suppresses ordinary join controls and exposes
  tournament information/map choices when appropriate;
- intermission suppresses join, vote, MyMap, tournament, and admin actions that
  are not valid after the match, while keeping results/status, Spectate or
  Resume where applicable, and Leave Match available.

The hub also conditionally exposes voting, MyMap, forfeit, match stats, and
admin tools from the same policy checks already used by their server command
handlers.

## Cvar and Command Data Bridge

The live bridge is intentionally narrow: sgame publishes display-ready values
and boolean/action cvars, while the documents issue registered server or UI
commands.

| Group | Published contract |
|---|---|
| Identity | `ui_dm_context`, `ui_dm_title`, `ui_dm_subtitle`, `ui_dm_motd` |
| Map and rules | `ui_dm_gametype`, `ui_dm_map`, `ui_dm_mapname`, `ui_dm_ruleset`, `ui_dm_score_limit`, `ui_dm_time_limit` |
| Live status | `ui_dm_match_state`, `ui_dm_player_count`, `ui_dm_spectator_count`, `ui_dm_current_status`, `ui_dm_join_notice` |
| Participation | `ui_dm_can_join`, `ui_dm_teamplay`, `ui_dm_show_join`, `ui_dm_show_spectate`, team/join labels, and the spectator command |
| Conditional tools | `ui_dm_show_ready`, tournament, vote, MyMap, forfeit, match-stats, admin, resume, and leave flags/commands |
| Ownership/capability | `ui_dm_initial`, `ui_dm_menu_active`, `ui_worr_match_hub`, `ui_rml_runtime_available` |

Both `dm_join.rml` and `join.rml` consume this contract through cvar text,
visibility, enablement, label, and command bindings. The JSON fallback consumes
the same names through `labelCvar`, `showIf`, `enableIf`, `defaultIf`, and
`commandCvar`, so the renderer choice does not change match policy.

## Paced UI Command Queue

The expanded snapshot no longer assumes that every `set ui_dm_*` plus
`pushmenu` command can fit safely in one stuffed-command message.

`MenuUi::UiCommandBuilder` now:

- escapes all UI text before command insertion;
- chunks snapshots below half of the shared 1 KiB command buffer, leaving room
  for ordinary server traffic;
- queues at most 16 chunks per client and clears a stale snapshot if that
  bound is exceeded;
- flushes one queued chunk per server frame;
- waits for the queue to drain before the one-second live match-hub refresh
  publishes another snapshot.

This prevents long hostnames, MOTDs, match metadata, and the larger set of
conditional values from overflowing or interleaving with later snapshots.
Queues are cleared on client setup, disconnect, team/menu close, and fresh
root opens.

## Presentation and Renderer Fallback

### RmlUi presentation

The RmlUi page is a translucent full match overlay with:

- a first-party `worr-mark.svg` emblem and WORR Multiplayer wordmark;
- server name, gametype/map subtitle, and a prominent match-state chip;
- top Overview, Vote, Server, Match Details, Stats, and Settings tabs;
- a map card, tabulated live match statistics, current player status, and MOTD;
- a distinct participation column with red, blue, auto, free, spectator, and
  ready treatments;
- conditional match-tool actions and a clear Resume/Leave footer;
- separate first-connect instruction and in-session Escape presentation;
- responsive narrow/short viewport rules and explicit high-visibility states.

The overlay remains transparent so the live game view is visible behind it.
The RmlUi render path also resets the renderer clip rectangle after document
rendering so a final scroll-region scissor cannot leak into the gameplay frame.

### JSON/native-renderer fallback

The cgame JSON hub mirrors the hierarchy, dynamic values, conditional actions,
team coloring, first-connect instruction, and Resume/Leave behavior. Action
widgets gained per-item size/color support to make the native fallback more
readable. Its pointer is a crisp code-native yellow cursor drawn with
`R_DrawFill32`, so the fallback does not depend on loose or packaged cursor
textures.

`pushmenu` checks `ui_rml_runtime_available` before entering the RmlUi bridge.
Bridged route and popup commands also have explicit cgame fallback variants.
If RmlUi cannot start, update, resize, or render the requested route, the
client closes the failed runtime route and queues the equivalent cgame menu.

Native Vulkan and RTX/vkpt keep their RmlUi interfaces inactive until those
renderers implement their own bridges. Their expected availability is
`renderer_unavailable`, which selects the JSON hub drawn by that renderer.
There is no OpenGL redirection.

## Implementation Areas

The slice spans these ownership boundaries:

- sgame state and commands:
  `src/game/sgame/menu/menu_page_welcome.cpp`,
  `src/game/sgame/menu/menu_ui_helpers.*`,
  `src/game/sgame/commands/command_client.cpp`,
  `src/game/sgame/player/p_client.cpp`,
  `src/game/sgame/player/p_view.cpp`,
  `src/game/sgame/client/client_session_service_impl.cpp`, and
  `src/game/sgame/g_local.hpp`;
- client routing and RmlUi fallback:
  `src/client/keys.cpp`, `src/client/main.cpp`, `src/client/ui_bridge.cpp`,
  `src/client/ui_rml/ui_rml.cpp`, and
  `src/client/ui_rml/ui_rml_runtime.cpp`;
- cgame JSON fallback:
  `src/game/cgame/ui/ui_core.cpp`, `ui_json.cpp`, `ui_widgets.cpp`,
  `ui_internal.h`, and `worr-multiplayer.json`;
- RmlUi assets:
  `assets/ui/rml/session/dm_join.rml`,
  `assets/ui/rml/session/join.rml`,
  `assets/ui/rml/common/theme/session.rcss`, and
  `assets/ui/rml/common/icons/brand/worr-mark.svg`.

## Validation and Evidence

### Build and static smoke

- `meson compile -C builddir-win` succeeded.
- `python -m pytest tools/ui_smoke -q` passed: `225 passed`.

### Staging

The distributable payload was refreshed with the canonical command:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

The refresh validated `275` packaged assets and `181` RmlUi asset paths in the
current `.install` payload.

### Live OpenGL state and transition smoke

`.install/basew/logs/match_hub_live_initial_v3.log` records:

- the OpenGL RmlUi 6.2 runtime and renderer bridge initialized;
- the WORR brand SVG generated through the OpenGL RmlUi texture path;
- runtime status `active=yes`, route `dm_join`, availability `ready`.

`.install/basew/logs/match_hub_transition_smoke.log` validates the live state
sequence:

1. initial hub `active=yes`;
2. Join Match closes it to `active=no`;
3. inventory/Escape server reopen returns `active=yes` on `dm_join`;
4. Resume Match returns to `active=no`.

The same log records one open and one close request for each completed phase
and live rendered-frame counts before each close.

### Visual inspection

Injected RmlUi snapshots under `.tmp/rmlui/match-hub/` were inspected for the
two authored states:

- `match_hub_initial.png`: branded initial-choice layout, match overview,
  team choices, Spectate, and the locked-view instruction;
- `match_hub_escape_final.png`: in-session tabs, team/ready state, match tools,
  and Resume/Leave actions.

The directory also contains the live OpenGL and native Vulkan capture rounds
used while stabilizing the flow.

### Native Vulkan fallback

`.install/basew/logs/match_hub_live_vulkan_v4.log` and
`.install/basew/logs/match_hub_live_vulkan_final.log` record:

- renderer `vulkan` on the native Vulkan device;
- RmlUi availability `renderer_unavailable` with family `vulkan`;
- fallback to the legacy/cgame presentation instead of an OpenGL path;
- `ui_dm_menu_active=1`, proving the server still owns the live hub.

`.tmp/rmlui/match-hub/match_hub_live_vulkan_repeat_a.png` is the final visually
inspected native fallback capture. It shows the matching JSON multiplayer match
hub and the code-native yellow cursor rendered over the native Vulkan game
view; the repeated capture is stable.

## Remaining Work

- Implement and validate native RmlUi render interfaces for Vulkan and
  RTX/vkpt before claiming renderer-wide RmlUi parity.
- Extend automated navigation/input and layout coverage beyond this focused
  initial/join/reopen/resume sequence.
- Complete live controller parity for the remaining browser, list, tournament,
  vote, MyMap, keybind, save/load, and player-preview surfaces.
- Complete localization/text-shaping stress and the full selected renderer,
  viewport, high-visibility, mouse, and keyboard matrix.
- Keep the JSON fallback until Gate G3/G4 parity evidence permits deliberate
  legacy removal.
