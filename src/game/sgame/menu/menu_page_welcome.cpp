/*Copyright (c) 2024 The DarkMatter Project
Licensed under the GNU General Public License 2.0.

menu_page_welcome.cpp (Menu Page - Welcome/Join) This file implements the main
menu that players see when they are spectators or have just joined the server.
It is the primary navigation hub for joining the game, spectating, or accessing
other informational menus. Key Responsibilities:
- Main Menu Hub: `OpenJoinMenu` is the function called to display the main menu.
- Dynamic Join Options: The `onUpdate` function (`AddJoinOptions`) dynamically
creates the "Join" options based on the current gametype (e.g., "Join Red",
"Join Blue" for TDM; "Join Match" or "Join Queue" for FFA/Duel). - Player
Counts: Displays the current number of players in the match or on each team.
- Navigation: Provides the entry points to all other major menus, such as "Host
Info", "Match Info", and "Call a Vote".*/

#include "../g_local.hpp"
#include "menu_ui_helpers.hpp"

extern bool SetTeam(gentity_t *ent, Team desired_team, bool inactive,
                    bool force, bool silent);
void GetFollowTarget(gentity_t *ent);
void FreeFollower(gentity_t *ent);
bool Vote_Menu_Active(gentity_t *ent);

extern void OpenHostInfoMenu(gentity_t *ent);
extern void OpenMatchInfoMenu(gentity_t *ent);
extern void OpenPlayerMatchStatsMenu(gentity_t *ent);
extern void OpenAdminSettingsMenu(gentity_t *ent);
extern void OpenVoteMenu(gentity_t *ent);
extern void OpenMyMapMenu(gentity_t *ent);
extern void OpenForfeitMenu(gentity_t *ent);
void OpenJoinMenu(gentity_t *ent);

namespace {

static std::string MatchStateLabel() {
  if (level.intermission.time)
    return "Intermission";
  if (level.timeoutActive > 0_ms)
    return "Timeout";

  switch (level.matchState) {
  case MatchState::None:
  case MatchState::Initial_Delay:
    return "Starting";
  case MatchState::Warmup_Default:
    switch (level.warmupState) {
    case WarmupState::Too_Few_Players:
      return "Waiting for players";
    case WarmupState::Teams_Imbalanced:
      return "Teams imbalanced";
    case WarmupState::Not_Ready:
      return "Waiting for ready players";
    case WarmupState::Default:
    default:
      return "Warmup";
    }
  case MatchState::Warmup_ReadyUp:
    return "Warmup - ready up";
  case MatchState::Countdown:
    return "Match starting";
  case MatchState::In_Progress:
    return "Match in progress";
  case MatchState::Ended:
    return "Match complete";
  default:
    return "Match status unavailable";
  }
}

static std::string CurrentPlayerStatus(gclient_t *cl,
                                       bool initialMenu) {
  if (!cl)
    return {};

  if (!ClientIsPlaying(cl)) {
    if (cl->sess.matchQueued)
      return "Queued for the next duel slot";
    return initialMenu ? "Choose how you want to enter the match"
                       : "Currently spectating";
  }

  if (Teams())
    return fmt::format("Playing on the {} team", Teams_TeamName(cl->sess.team));
  return "Playing in the match";
}

static std::string ScoreLimitLabel() {
  const int limit = GT_ScoreLimit();
  if (limit <= 0)
    return "No score limit";
  return fmt::format("{} limit: {}", GT_ScoreLimitString(), limit);
}

static std::string TimeLimitLabel() {
  if (!timeLimit || timeLimit->value <= 0.f)
    return "No time limit";
  return fmt::format("Time limit: {}",
                     TimeString(static_cast<int>(timeLimit->value * 60000.f),
                                false, false));
}

static std::string PlainUiText(std::string_view text) {
  char buffer[MAX_STRING_CHARS] = {};
  G_StripColorEscapes(text, buffer, sizeof(buffer));
  return buffer;
}

static std::string DmJoinTitle() {
  if (hostname && hostname->string && hostname->string[0]) {
    std::string title = PlainUiText(hostname->string);
    if (!title.empty())
      return title;
  }
  return "WORR Match";
}

static std::string DmJoinSubtitle() {
  const char *gametype = level.gametype_name.data();
  const char *mapTitle = level.longName.data();
  const char *mapName = level.mapName.data();
  const char *displayMap = (mapTitle && *mapTitle) ? mapTitle : mapName;

  if (gametype && *gametype && displayMap && *displayMap)
    return fmt::format("{} | {}", gametype, displayMap);
  if (gametype && *gametype)
    return gametype;
  return displayMap ? displayMap : "";
}

static void OpenJoinMenuInternal(gentity_t *ent, const std::string &title,
                                 const std::string &subtitle,
                                 const char *menuName) {
  if (!ent || !ent->client)
    return;

  gclient_t *cl = ent->client;
  if (menuName && *menuName)
    cl->ui.commandQueue.clear();

  int maxPlayers = maxplayers->integer;
  if (maxPlayers < 1)
    maxPlayers = 1;

  uint8_t redCount = 0, blueCount = 0, freeCount = 0, queueCount = 0;
  uint8_t spectatorCount = 0;
  const bool duelQueueAllowed =
      Game::Has(GameFlags::OneVOne) && g_allow_duel_queue &&
      g_allow_duel_queue->integer && !Tournament_IsActive();

  for (auto ec : active_clients()) {
    if (duelQueueAllowed && ec->client->sess.team == Team::Spectator &&
        ec->client->sess.matchQueued) {
      queueCount++;
    } else {
      switch (ec->client->sess.team) {
      case Team::Free:
        freeCount++;
        break;
      case Team::Red:
        redCount++;
        break;
      case Team::Blue:
        blueCount++;
        break;
      case Team::None:
      case Team::Total:
        break;
      case Team::Spectator:
        spectatorCount++;
        break;
      }
    }
  }

  const bool teamplay = Teams();
  std::string joinRed =
      fmt::format("Join Red ({}/{})", redCount, maxPlayers / 2);
  std::string joinBlue =
      fmt::format("Join Blue ({}/{})", blueCount, maxPlayers / 2);
  const std::string joinAuto = "Auto Assign";

  std::string joinFree;
  if (duelQueueAllowed && level.pop.num_playing_clients == 2) {
    joinFree = fmt::format("Join Queue ({}/{})", queueCount, maxPlayers - 2);
  } else {
    const int targetMax = Game::Has(GameFlags::OneVOne) ? 2 : maxPlayers;
    joinFree = fmt::format("Join Match ({}/{})", freeCount, targetMax);
  }

  const bool isPlaying = ClientIsPlaying(cl);
  const bool isTournament = Tournament_IsActive();
  const bool isIntermission = level.intermission.time != 0_ms;
  const bool initialMenu = cl->initialMenu.frozen;
  const bool matchLocked = match_lock && match_lock->integer &&
                           level.matchState >= MatchState::Countdown;
  const bool publicSlotsFull =
      !isPlaying && level.pop.num_playing_human_clients >= maxPlayers;
  const bool canJoin = !isIntermission &&
                       (isPlaying || duelQueueAllowed ||
                        (!matchLocked && !publicSlotsFull));
  const bool showJoin =
      !isIntermission &&
      (initialMenu || !isPlaying || teamplay) && !isTournament;
  const bool showSpectate =
      (initialMenu || (isPlaying && !isIntermission)) && !isTournament;
  const bool showResume = !initialMenu;
  const bool showLeave = true;

  std::string joinNotice;
  if (isIntermission) {
    joinNotice = initialMenu
        ? "The match has ended. Spectate or leave while the next map loads."
        : "The match has ended. Review the results or leave while the next map loads.";
  } else if (isTournament) {
    joinNotice = "Tournament controls determine participation.";
  } else if (!canJoin && matchLocked) {
    joinNotice = "The match is locked. Spectate until the next warmup.";
  } else if (!canJoin && publicSlotsFull) {
    joinNotice = "The player limit is full. Spectate until a slot opens.";
  } else if (initialMenu && teamplay) {
    joinNotice = "Choose a side, use Auto Assign, or spectate.";
  } else if (initialMenu) {
    joinNotice = "Join the match or begin as a spectator.";
  }

  const bool showTourneyInfo = isTournament && !isIntermission;
  const bool showTourneyMaps =
      isTournament && !isIntermission && Tournament_VetoComplete() &&
      !game.tournament.mapOrder.empty();
  const bool showCallvote =
      !isIntermission && !isTournament && g_allowVoting->integer &&
      (isPlaying || (!isPlaying && g_allowSpecVote->integer));
  const bool showMymap =
      !isIntermission && !isTournament &&
      g_maps_mymap && g_maps_mymap->integer &&
      (!g_allowMymap || g_allowMymap->integer);
  const bool showForfeit =
      !isTournament && g_allowVoting->integer && isPlaying &&
      (level.matchState == MatchState::In_Progress ||
       level.matchState == MatchState::Countdown);
  const bool showMatchStats = g_matchstats->integer != 0;
  const bool showAdmin = cl->sess.admin && !isIntermission;
  const bool showReady = isPlaying &&
                         level.matchState == MatchState::Warmup_ReadyUp;
  const std::string readyLabel =
      cl->pers.readyStatus ? "Mark Not Ready" : "Ready Up";
  const char *readyCommand = cl->pers.readyStatus ? "notready" : "ready";

  std::string spectatorLabel = fmt::format(
      "{} spectator{}", spectatorCount, spectatorCount == 1 ? "" : "s");
  if (queueCount) {
    spectatorLabel += fmt::format(" | {} queued", queueCount);
  }

  const char *mapTitle = level.longName.data();
  const char *mapName = level.mapName.data();
  const char *displayMap = (mapTitle && *mapTitle) ? mapTitle : mapName;

  MenuUi::UiCommandBuilder cmd(ent);
  cmd.AppendCvar("ui_dm_title", title);
  cmd.AppendCvar("ui_dm_subtitle", subtitle);
  cmd.AppendCvar("ui_dm_context",
                 initialMenu ? "WELCOME TO THE MATCH" : "MATCH MENU");
  cmd.AppendCvar("ui_dm_initial", initialMenu ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_resume", showResume ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_leave", showLeave ? "1" : "0");
  cmd.AppendCvar("ui_dm_menu_active", "1");
  cmd.AppendCvar("ui_dm_motd", game.motd);
  cmd.AppendCvar("ui_dm_gametype", level.gametype_name.data());
  cmd.AppendCvar("ui_dm_map", displayMap ? displayMap : "");
  cmd.AppendCvar("ui_dm_mapname", mapName ? mapName : "");
  cmd.AppendCvar("ui_dm_match_state", MatchStateLabel());
  cmd.AppendCvar("ui_dm_player_count",
                 fmt::format("{} / {} playing",
                             level.pop.num_playing_clients, maxPlayers));
  cmd.AppendCvar("ui_dm_spectator_count", spectatorLabel);
  cmd.AppendCvar("ui_dm_ruleset", rs_long_name[(int)game.ruleset]);
  cmd.AppendCvar("ui_dm_score_limit", ScoreLimitLabel());
  cmd.AppendCvar("ui_dm_time_limit", TimeLimitLabel());
  cmd.AppendCvar("ui_dm_current_status",
                 CurrentPlayerStatus(cl, initialMenu));
  cmd.AppendCvar("ui_dm_join_notice", joinNotice);
  cmd.AppendCvar("ui_dm_can_join", canJoin ? "1" : "0");
  cmd.AppendCvar("ui_dm_teamplay", teamplay ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_join", showJoin ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_spectate", showSpectate ? "1" : "0");
  cmd.AppendCvar("ui_dm_spectate_command",
                 initialMenu ? "worr_dm_initial_spectate"
                             : "team spectator");
  cmd.AppendCvar("ui_dm_join_red", joinRed);
  cmd.AppendCvar("ui_dm_join_blue", joinBlue);
  cmd.AppendCvar("ui_dm_join_auto", joinAuto);
  cmd.AppendCvar("ui_dm_join_free", joinFree);
  cmd.AppendCvar("ui_dm_show_ready", showReady ? "1" : "0");
  cmd.AppendCvar("ui_dm_ready_label", readyLabel);
  cmd.AppendCvar("ui_dm_ready_command", readyCommand);
  cmd.AppendCvar("ui_dm_show_tourney_info", showTourneyInfo ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_tourney_maps", showTourneyMaps ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_callvote", showCallvote ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_mymap", showMymap ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_forfeit", showForfeit ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_matchstats", showMatchStats ? "1" : "0");
  cmd.AppendCvar("ui_dm_show_admin", showAdmin ? "1" : "0");
  if (menuName && *menuName)
    cmd.AppendCommand(std::string("pushmenu ") + menuName);
  cmd.Flush();

  if (menuName && *menuName) {
    cl->initialMenu.dmJoinActive = true;
    cl->initialMenu.dmWelcomeActive = false;
  }
}

} // namespace

static void ReleaseWelcomeFreeze(gentity_t *ent) {
  if (!ent || !ent->client)
    return;

  if (!ent->client->initialMenu.frozen)
    return;

  ent->client->initialMenu.frozen = false;
  ent->client->initialMenu.shown = true;
  ent->client->initialMenu.delay = 0_sec;
  ent->client->initialMenu.hostSetupDone = true;
  ent->client->initialMenu.dmWelcomeActive = false;
  ent->client->initialMenu.dmJoinActive = false;
}

[[maybe_unused]] static void TryJoinTeam(gentity_t *ent, Team team) {
  if (SetTeam(ent, team, false, false, false))
    ReleaseWelcomeFreeze(ent);
}

[[maybe_unused]] static void SelectSpectate(gentity_t *ent) {
  if (!ent || !ent->client)
    return;

  const bool wasFrozen = ent->client->initialMenu.frozen;
  const bool wasSpectator = !ClientIsPlaying(ent->client);
  const bool changed = SetTeam(ent, Team::Spectator, false, false, false);

  if (changed || wasSpectator) {
    ReleaseWelcomeFreeze(ent);
    if (!changed)
      CloseActiveMenu(ent);
    else if (!wasFrozen)
      OpenJoinMenu(ent);
  }
}

void OpenDmWelcomeMenu(gentity_t *ent) {
  // Keep the historical entry point for callers and older integrations, but
  // first-connect onboarding now lands directly in the authoritative match
  // hub. The initial freeze is released only by a successful team/spectator
  // choice, not by an intermediate Continue page.
  OpenDmJoinMenu(ent);
}

void OpenDmJoinMenu(gentity_t *ent) {
  if (!ent || !ent->client)
    return;

  OpenJoinMenuInternal(ent, DmJoinTitle(), DmJoinSubtitle(), "dm_join");
}

void RefreshDmJoinMenu(gentity_t *ent) {
  if (!ent || !ent->client || !ent->client->initialMenu.dmJoinActive)
    return;

  if (!ent->client->ui.commandQueue.empty())
    return;

  OpenJoinMenuInternal(ent, DmJoinTitle(), DmJoinSubtitle(), nullptr);
}

void CloseDmJoinMenu(gentity_t *ent) {
  if (!ent || !ent->client)
    return;

  ent->client->initialMenu.dmJoinActive = false;
  ent->client->initialMenu.nextUpdate = 0_ms;
  ent->client->ui.commandQueue.clear();
  MenuUi::SendUiCommand(ent, "set ui_dm_menu_active 0\n");
}

void ForceCloseDmJoinMenu(gentity_t *ent) {
  CloseDmJoinMenu(ent);
  MenuUi::SendUiCommand(ent, "forcemenuoff\n");
}

void OpenDmHostInfoMenu(gentity_t *ent) {
  if (!ent || !ent->client)
    return;

  std::string hostName;
  if (g_entities[1].client) {
    char value[MAX_INFO_VALUE] = {0};
    gi.Info_ValueForKey(g_entities[1].client->pers.userInfo, "name", value,
                        sizeof(value));
    if (value[0])
      hostName = value;
  }

  const std::string serverName =
      (hostname && hostname->string) ? PlainUiText(hostname->string) : "";

  MenuUi::UiCommandBuilder cmd(ent);
  cmd.AppendCvar("ui_hostinfo_server", serverName);
  cmd.AppendCvar("ui_hostinfo_host", hostName);
  cmd.AppendCvar("ui_hostinfo_motd", game.motd);
  cmd.AppendCommand("pushmenu dm_hostinfo");
  cmd.Flush();
}

void OpenDmMatchInfoMenu(gentity_t *ent) {
  if (!ent || !ent->client)
    return;

  std::string author1;
  std::string author2;
  if (level.author[0])
    author1 = fmt::format("author: {}", level.author);
  if (level.author2[0])
    author2 = fmt::format("      {}", level.author2);

  std::string scorelimit;
  if (GT_ScoreLimit())
    scorelimit = fmt::format("{} limit: {}", GT_ScoreLimitString(),
                             GT_ScoreLimit());

  std::string timelimit;
  if (timeLimit->value > 0) {
    timelimit = fmt::format(
        "time limit: {}",
        TimeString(timeLimit->value * 60000, false, false));
  }

  MenuUi::UiCommandBuilder cmd(ent);
  cmd.AppendCvar("ui_matchinfo_gametype", level.gametype_name.data());
  cmd.AppendCvar("ui_matchinfo_map", fmt::format("map: {}", level.longName.data()));
  cmd.AppendCvar("ui_matchinfo_mapname", fmt::format("mapname: {}", level.mapName.data()));
  cmd.AppendCvar("ui_matchinfo_author1", author1);
  cmd.AppendCvar("ui_matchinfo_author2", author2);
  cmd.AppendCvar("ui_matchinfo_ruleset",
                 fmt::format("ruleset: {}", rs_long_name[(int)game.ruleset]));
  cmd.AppendCvar("ui_matchinfo_scorelimit", scorelimit);
  cmd.AppendCvar("ui_matchinfo_timelimit", timelimit);
  cmd.AppendCommand("pushmenu dm_matchinfo");
  cmd.Flush();
}

void OpenJoinMenu(gentity_t *ent) {
  if (!ent || !ent->client)
    return;

  if (deathmatch->integer) {
    OpenDmJoinMenu(ent);
    return;
  }

  if (Vote_Menu_Active(ent)) {
    OpenVoteMenu(ent);
    return;
  }

  const std::string title = fmt::format("{} v{}", worr::version::kGameTitle,
                                        worr::version::kGameVersion);
  OpenJoinMenuInternal(ent, title, "", "join");
}

/*
===============
OpenPlayerWelcomeMenu

Welcome menu for non-hosts. Shows welcome, hostname, MOTD, and Continue button.
===============
*/
void OpenPlayerWelcomeMenu(gentity_t *ent) {
  OpenDmWelcomeMenu(ent);
}
