# Multiplayer Session Menu

When you connect to a WORR multiplayer match, the session menu gives you the
important match information and asks how you want to enter.

## Joining a match

On most WORR deathmatch servers, the menu opens automatically after the map is
ready. Your view stays locked until you make a choice.

- In free-for-all modes, choose **Join Match** or **Spectate**.
- In team modes, choose **Join Red**, **Join Blue**, **Auto Assign**, or
  **Spectate**. Auto Assign lets the server pick the balanced team.
- In Duel, **Join Queue** may appear when both active places are occupied.
- If the match is locked, full, or in intermission, the menu explains why
  joining is unavailable.

Choosing Spectate is a real choice: it unlocks the view and lets you remain an
observer without joining a team.

## Opening it again

Press **Escape** during an active WORR multiplayer match to reopen the session
menu. The server refreshes the information before showing it, so team counts,
ready state, join availability, and match status are current.

After your initial choice, select **Resume Match** or press Escape/Back to
return to play. **Leave Match** opens a confirmation before disconnecting.

The inventory/menu binding can also open the same server-managed hub.

## What the menu shows

The Overview page includes:

- server name and message;
- gametype, map title, and map name;
- warmup, countdown, live-match, timeout, or intermission status;
- player, spectator, and Duel queue counts;
- ruleset, score limit, and time limit;
- your current team, playing, queued, or spectator status.

The tabs and Match Tools area provide the options that the current server and
match allow, such as Vote, Server, Match Details, Stats, Settings, MyMap,
Tournament information, Forfeit, Ready Up, and Admin.

## Why it may look different between renderers

OpenGL can display the current RmlUi match hub. Native Vulkan and RTX/vkpt use
the matching built-in menu while their native RmlUi renderer bridges are still
being completed. This is expected: the commands and live server information
are the same, and Vulkan/RTX are not redirected through OpenGL.

## Server option: immediate auto-join

The default is to let players review the match and choose how to enter. Server
operators who prefer the older immediate-assignment behavior can put this in
the server configuration:

```cfg
set match_auto_join 1
```

This enables the `match_auto_join=1` compatibility behavior.

With the default `match_auto_join 0`, human players use the initial session
menu. Tournament assignments, forced joins, bots, and local-host auto-join
rules may still place a player automatically when their separate server policy
requires it.

## If the menu does not appear

- Confirm you are in an active WORR deathmatch session, not a demo or coop
  game.
- Try the inventory/menu binding once; this asks the server to reopen the hub.
- A legacy or non-WORR server may use its own normal in-game menu instead.
- If you are already playing, Escape opens the in-session version rather than
  the mandatory first-connect choice.
