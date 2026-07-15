# Multiplayer Session Menu

When you connect to a WORR multiplayer match, the session menu gives you the
important match information and asks how you want to enter.

The arena remains visible around the menu's partial-screen frame. WORR softly
defocuses the live view while keeping menu text and controls sharp, then eases
back to the clear game view when you close the menu. This visual transition
does not slow or alter the match.

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

Five tabs always appear in the same order: **Overview**, **Server Info**,
**Voting**, **MyMap**, and **Admin**. The current page has an orange underline.
Features the server does not provide remain visible but dimmed and cannot be
selected, so the tab positions never jump around.

Overview keeps occasional destinations such as Match Details, Stats, and
Tournament information in a small context-action row. Settings, Resume Match,
Leave Match, and Forfeit appear only when they apply. Hover a control with the
mouse or focus it with the keyboard/controller to see a short explanation in
the lower-left help area.

The upper-right status cluster presents the current match phase as a live
signal. The small clock and date below it use your computer's local time.

## Reviewing your match stats

If the server enables player statistics, open **Stats** from Match Tools. The
page shows your current kills, deaths, K/D ratio, damage dealt and received,
damage ratio, shots fired, hits, and total accuracy. Values refresh while the
page is open.

Ratios that cannot be calculated yet appear as **N/A**, such as K/D before
your first death or accuracy before your first shot. Use Back to return to the
session menu.

## Voting

Open **Voting** to call a new vote when the server allows it. If any player
starts a vote while the session hub is open, the live proposal replaces the
Overview block automatically. It shows who called it, the proposal, remaining
time, and your eligibility without moving the join/spectate controls. A short
get-ready countdown may appear before **Yes** and **No** become available.
After you answer, the hub records your choice and remains open.

Use **Call a Vote** to propose a new vote. The server decides which choices
are currently allowed, so the list can vary by gametype and server settings.
Time Limit, Score Limit, Unlagged, Ruleset, and Random Number open focused
choice pages.

For a map vote, **Map Flags** lets you add optional rule changes such as
enabling, disabling, or leaving an item at its default. Use Back after choosing
the flags, then select the map. **Clear Flags** on the map list restores the
default flag selection.

## Requesting a map with MyMap

If the server enables MyMap, open it from Match Tools to request a map for the
upcoming queue. **Flags** can attach optional item or damage-rule changes to
your request. Return with Back, choose **Select Map**, and select the map from
the list. **Clear Flags** removes your temporary flag choices.

The menu explains when MyMap is disabled, unavailable during a tournament, or
requires you to log in. In those states its unavailable actions are disabled.

## Voting for the next map

When the server enables intermission map selection, **Next Map Vote** appears
with up to three candidates. Choose one map before the countdown reaches zero.
The panel shows both whole seconds and a shrinking bar, so the deadline remains
clear even when map names vary in length.

After you choose, the map buttons are replaced by **Acknowledged** and the map
you selected. You cannot vote twice. You may use Back to dismiss the page; it
stays closed for that ballot while the server finishes collecting votes. A
strict majority can finish the vote early. Otherwise the server chooses the
leading candidate when time expires and loads the selected map.

## Tournament matches

Open **Tournament Info** for a quick explanation of the series format and veto
rules. You can run `tourney_status` in the console when you need the server's
current detailed status.

During a map veto, the menu shows whose turn it is. If you are the active
player or captain, choose **Pick** or **Ban**, then select a remaining map from
the list. Other players see a waiting message. **Ban locked** means the series
needs the remaining maps for its required picks, so another ban is no longer
valid. After the veto finishes, **Tournament Map Choices** shows the numbered
game order.

Tournament admins can open **Admin**, choose **Replay Game**, and select a
game. Replay is destructive: results from that game onward are discarded.
**No** is selected first; choose **Yes** only after checking the game number.

## Leaving or forfeiting

**Leave Match** always opens a confirmation before disconnecting. **No** is
selected first so an accidental confirm keeps you in the match. Choose **Yes**
only when you want to close the session menu and disconnect from the server.

When the current match and server allow it, **Forfeit** opens a separate
confirmation that explains the match will end and count as a loss. **Yes**
submits the server-managed forfeit action; **No** returns to the session menu
without changing the match.

## Adjusting the background focus

OpenGL, Vulkan, and RTX/vkpt all draw the current RmlUi session menu through
their native renderer paths. The exact focus character varies slightly by
renderer, but the arena stays behind the same frame and all controls behave the
same way.

To change the focus strength, set `cl_menu_bokeh_blur` to a value from `0` to
`1` in the console. The default is `0.85`; use `0` to keep the world sharp.

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
