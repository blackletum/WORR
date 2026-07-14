# Competitive Server Tools

This guide is for operators running WORR duel, pickup, tournament, or practice
servers with voting, MyMap, map selection, bots, and match logs enabled.

## Recommended Baseline

Use this as a starting point in `basew/server.cfg`, then adjust limits for your
community:

```text
set deathmatch 1
set maxclients 12

set warmup_enabled 1
set warmup_do_ready_up 1
set match_start_no_humans 1

set g_allow_voting 1
set g_allow_vote_mid_game 0
set g_allow_spec_vote 0
set g_vote_limit 3

set g_maps_selector 1
set g_maps_mymap 1
set g_allow_mymap 1
set g_maps_mymap_queue_limit 8

set g_allow_duel_queue 1

set g_statex_enabled 1
set g_statex_humans_present 1
set g_statex_export_html 1
```

For bot practice, choose a small autofill target:

```text
set bot_profile vanguard
set bot_min_players 4
```

See [Bots](bots.md), [Bot Profiles](bot-profiles.md), and
[Bot Map Readiness](bot-map-readiness.md) for bot setup and AAS map checks.

## Warmup And Bot Practice

`warmup_enabled 1` keeps a competitive server in warmup until players are
ready. `warmup_do_ready_up 1` asks players to ready up instead of starting
immediately. `match_start_no_humans 1` lets a bot-filled practice server start
when there are no human players, which is useful for local validation and
always-on warmup servers.

Bots can fill practice slots through `bot_min_players`, but they are treated
as bots by the competitive systems. They do not become voting humans, tournament
captains, or match-log human presence.

## Voting

Player votes are controlled by:

| Cvar | Default | Purpose |
|---|---:|---|
| `g_allow_voting` | `1` | Enables or disables player vote calls. |
| `g_allow_vote_mid_game` | `0` | Allows vote calls after countdown has started. |
| `g_allow_spec_vote` | `0` | Allows spectators to call and receive vote prompts. |
| `g_vote_limit` | `3` | Limits how many votes one client may call per map. |
| `g_vote_flags` | all standard votes | Bitmask of enabled vote types. |

Players can call votes with `callvote` or the short alias `cv`. Running
`callvote` with no arguments prints the enabled vote types for that server.
Common vote commands include:

```text
callvote map <mapname> [flags]
callvote nextmap
callvote restart
callvote gametype <gametype>
callvote ruleset <q1|q2|q3a>
callvote timelimit <minutes>
callvote scorelimit <score>
callvote shuffle
callvote balance
callvote unlagged <0|1>
callvote cointoss
callvote random <max>
callvote arena <number>
```

Players vote with `vote yes` or `vote no`. The menu-backed UI commands such as
`worr_callvote_menu`, `worr_callvote_map`, and `worr_vote_yes` are available to
the in-game menus and should normally be left to the client UI.

Bots cannot call votes or cast yes/no votes. This keeps bot-filled warmup and
practice servers from accidentally passing map, ruleset, or admin-affecting
votes.

## MyMap, Nextmap, And Map Selection

MyMap lets authenticated players queue a preferred map for the next transition:

```text
mymap <mapname> [+flag] [-flag] ...
```

The relevant controls are:

| Cvar | Default | Purpose |
|---|---:|---|
| `g_maps_selector` | `1` | Enables the intermission map selector. |
| `g_maps_mymap` | `1` | Enables the MyMap queue system. |
| `g_allow_mymap` | `1` | Operator allow switch for MyMap requests. |
| `g_maps_mymap_queue_limit` | `8` | Caps queued MyMap requests. |
| `g_maps_pool_file` | `mapdb.json` | Map pool metadata file. |
| `g_maps_cycle_file` | `mapcycle.txt` | Map cycle file. |

`callvote nextmap` consumes a queued MyMap entry first. If the queue is empty,
the server selects the next map from the configured cycle.

When `g_maps_selector` is enabled, eligible players receive a three-candidate
**Next Map Vote** during intermission. The menu shows the remaining seconds and
a shrinking countdown bar. Casting a ballot replaces the candidates with an
acknowledgement; using Back dismisses the current ballot without making it
reappear. A strict majority finalizes early, otherwise the leading candidate
is selected when the short vote window expires. Bots cannot cast selector
ballots.

MyMap requires a connected client identity. Bots are not useful MyMap voters on
public servers and are kept from inflating human map choice during the validated
bot scenarios.

## Duel Queue

`g_allow_duel_queue 1` enables surplus-player queue handling for Duel-style
servers. Extra clients can wait as spectators while the active duel slots stay
bounded. Bots respect the active/spectator split, so bot-filled practice servers
should not crowd out the intended active duel participants.

## Tournament Flow

Tournament mode uses the match setup and tournament cvars:

```text
set match_setup_type tournament
set match_setup_bestof bo3
set g_tourney_cfg tourney.json
```

Tournament configs live under the game directory and are loaded through
`g_tourney_cfg` when tournament match setup starts. The server applies the
configured gametype, best-of length, team names, participants, and map pool.

Players can inspect and act on veto state through:

```text
tourney_status
tourney_pick <mapname>
tourney_ban <mapname>
```

The menu-backed commands `worr_tourney_info`, `worr_tourney_maps`,
`worr_tourney_veto`, `worr_tourney_pick`, and `worr_tourney_ban` are used by
the client UI. The Veto menu opens the shared remaining-map picker for Pick or
Ban and disables Ban when another ban would leave too few required picks.

Admin replay controls are available through **Admin > Replay Game** and
through:

```text
replay <game#> confirm
```

Replay is destructive. Replaying game N discards recorded results from game N
onward, rebuilds the earlier series score, and loads game N's map. The menu
warns about this result truncation and selects **No** first.

Tournament veto choices are limited to the active side. Bots are rejected from
the tournament veto identity path even if a smoke test assigns one a matching
participant identity.

## Admin Commands

Competitive operators usually need these registered admin commands:

```text
start_match
end_match
reset_match
map_restart
next_map
set_map <mapname>
set_team <client> <team>
lock_team <red|blue>
unlock_team <red|blue>
shuffle
balance
force_vote
replay <game#> confirm
```

Admin commands require admin privileges. Bot clients are blocked from crossing
the admin boundary, including the validated `lock_team` audit path.

In a live match, open **Admin** from the session menu and choose **Admin
Commands** for the complete in-game reference. The reference shows all
registered admin commands and their usage, but commands with arguments are
still entered in the console. **Replay Game** appears in the Admin menu only
while the server reports an active tournament.

## In-Game Player Stats

Set `g_matchstats 1` to make **Stats** available in the multiplayer session
menu. Each player sees their own live Combat, Damage, and Accuracy summary.
The page refreshes once per second while open and reports unavailable K/D,
damage, or accuracy ratios as `N/A` until the required counters exist.

This in-game page is separate from the completed-match files described below:
it is a current per-player view, while match logging writes durable server
artifacts for later review.

## Match Logs

Match logging writes under the active game directory's `matches/` folder, such
as `basew/matches/` in a staged or release server install.

| Cvar | Default | Purpose |
|---|---:|---|
| `g_statex_enabled` | `1` | Enables JSON match-stat export. |
| `g_statex_humans_present` | `1` | Exports only when at least one human is playing. |
| `g_statex_export_html` | `1` | Writes the companion HTML export when enabled. |

For bot-only practice logs, set `g_statex_humans_present 0`. For public servers,
the default `1` avoids generating empty or bot-only match history when no humans
are playing.

Current JSON artifacts identify their format with top-level schema fields. Match
stats use `schemaName: worr.match_stats` and tournament series logs use
`schemaName: worr.tournament_series`; both currently use `schemaVersion: 1`.
Successful exports also update `basew/matches/catalog.json`. The catalog uses
`schemaName: worr.match_catalog`, lists relative `jsonPath` entries for match
and tournament artifacts, and keeps latest-artifact IDs for downstream tools.

## Quick Checks

- Run `callvote` with no arguments to confirm the enabled vote list.
- Run `mappool` and `mapcycle` to confirm map pool and cycle visibility.
- Run `tourney_status` before a series starts to confirm veto readiness.
- Check `basew/matches/` after a completed human match if logs are missing.
- Keep `g_allow_vote_mid_game 0` on public competitive servers unless your
  community explicitly wants mid-match rule changes.
