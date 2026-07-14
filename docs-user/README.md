# WORR User Docs

Welcome. This section is for players and server admins who just want to get WORR running and play.

If you want deep engine internals, design notes, or migration writeups, see `docs-dev/`.

## Start Here

- [Getting Started](getting-started.md): install basics, launch flow, and first-run checks.
- [In-Game Console](console.md): completion, smooth scrolling, mouse controls,
  selection, appearance, and chat behavior.
- [Menu Navigation and Accessibility](menu-navigation-and-accessibility.md):
  keyboard/gamepad controls, visual intent, large text, high contrast, reduced
  motion, and language behavior.
- [Server Quickstart](server-quickstart.md): host a dedicated server with sane defaults.
- [Progressive Networking Controls](progressive-networking-controls.md): safe
  defaults, compatibility fallbacks, lag-compensation evaluation, snapshot
  timeline audits, adaptive input evaluation, and developer-only network fault
  testing.
- [Bots](bots.md): enable bots, add/remove them, choose practical cvars, and
  understand map/AAS limitations.
- [Bot Cvars](bot-cvars.md): supported public bot cvars, defaults, and common
  setup snippets.
- [Bot Map Readiness](bot-map-readiness.md): check whether a map has the AAS
  data bots need for routing.
- [Bot Profiles](bot-profiles.md): enable bots, find installed botfiles,
  customize profiles, and reload changes.
- [Bot Multiplayer Playtest](bot-playtest.md): generate repeatable FFA, Duel,
  TDM, and CTF bot playtest configs and checklists.
- [Multiplayer Session Menu](multiplayer-session-menu.md): join or spectate,
  review the live match, and reopen the server-managed menu during play.

## Full Reference Manuals

- `client.asciidoc`: full client command/cvar reference.
- `server.asciidoc`: full dedicated server command/cvar reference.
