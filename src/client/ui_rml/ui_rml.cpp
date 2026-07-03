/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "ui_rml.h"

#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/prompt.h"

static cvar_t *ui_rml_enable;
static cvar_t *ui_rml_debug;
static cvar_t *ui_rml_asset_root;
static bool ui_rml_initialized;
static bool ui_rml_commands_registered;

#if UI_RML_HAS_RUNTIME
static ui_rml_runtime_interface_t ui_rml_runtime;
static bool ui_rml_runtime_registered;
static bool ui_rml_runtime_started;
static bool ui_rml_runtime_failed;
static ui_rml_renderer_interface_t ui_rml_renderer;
static bool ui_rml_renderer_registered;
#endif

typedef struct {
    const char *id;
    const char *document;
} ui_rml_route_t;

static const ui_rml_route_t ui_rml_routes[] = {
    { "main", "shell/main.rml" },
    { "game", "shell/game.rml" },
    { "options", "shell/options.rml" },
    { "video", "settings/video.rml" },
    { "multimonitor", "settings/multimonitor.rml" },
    { "performance", "settings/performance.rml" },
    { "accessibility", "settings/accessibility.rml" },
    { "sound", "settings/sound.rml" },
    { "railtrail", "settings/railtrail.rml" },
    { "effects", "settings/effects.rml" },
    { "crosshair", "settings/crosshair.rml" },
    { "screen", "settings/screen.rml" },
    { "language", "settings/language.rml" },
    { "downloads", "shell/downloads.rml" },
    { "download_status", "shell/download_status.rml" },
    { "addressbook", "utility/addressbook.rml" },
    { "input", "settings/input.rml" },
    { "keys", "utility/keys.rml" },
    { "legacykeys", "utility/legacykeys.rml" },
    { "weapons", "utility/weapons.rml" },
    { "quit_confirm", "shell/quit_confirm.rml" },
    { "gameflags", "singleplayer/gameflags.rml" },
    { "startserver", "singleplayer/startserver.rml" },
    { "multiplayer", "multiplayer/multiplayer.rml" },
    { "singleplayer", "singleplayer/singleplayer.rml" },
    { "skill_select", "singleplayer/skill_select.rml" },
    { "loadgame", "singleplayer/loadgame.rml" },
    { "savegame", "singleplayer/savegame.rml" },
    { "servers", "utility/servers.rml" },
    { "demos", "utility/demos.rml" },
    { "players", "utility/players.rml" },
    { "ui_list", "utility/ui_list.rml" },
    { "dm_welcome", "session/dm_welcome.rml" },
    { "dm_join", "session/dm_join.rml" },
    { "join", "session/join.rml" },
    { "dm_hostinfo", "session/dm_hostinfo.rml" },
    { "dm_matchinfo", "session/dm_matchinfo.rml" },
    { "callvote_main", "session/callvote_main.rml" },
    { "callvote_ruleset", "session/callvote_ruleset.rml" },
    { "callvote_timelimit", "session/callvote_timelimit.rml" },
    { "callvote_scorelimit", "session/callvote_scorelimit.rml" },
    { "callvote_unlagged", "session/callvote_unlagged.rml" },
    { "callvote_random", "session/callvote_random.rml" },
    { "callvote_map_flags", "session/callvote_map_flags.rml" },
    { "mymap_main", "session/mymap_main.rml" },
    { "mymap_flags", "session/mymap_flags.rml" },
    { "forfeit_confirm", "session/forfeit_confirm.rml" },
    { "leave_match_confirm", "session/leave_match_confirm.rml" },
    { "admin_menu", "session/admin_menu.rml" },
    { "admin_commands", "session/admin_commands.rml" },
    { "tourney_info", "session/tourney_info.rml" },
    { "tourney_mapchoices", "session/tourney_mapchoices.rml" },
    { "tourney_veto", "session/tourney_veto.rml" },
    { "tourney_replay_confirm", "session/tourney_replay_confirm.rml" },
    { "vote_menu", "session/vote_menu.rml" },
    { "map_selector", "session/map_selector.rml" },
    { "match_stats", "session/match_stats.rml" },
    { "core.runtime_smoke", "core/runtime_smoke.rml" },
};

static int UI_Rml_DefaultLoadFile(const char *path, void **data)
{
    return FS_LoadFileEx(path, data, 0, TAG_FILESYSTEM);
}

static void UI_Rml_DefaultFreeFile(void *data)
{
    FS_FreeFile(data);
}

// Narrow engine-system boundary for future runtime and test hooks.
static const ui_rml_file_interface_t ui_rml_file_interface = {
    UI_Rml_DefaultLoadFile,
    UI_Rml_DefaultFreeFile
};

static const ui_rml_route_t *UI_Rml_FindRoute(const char *route_id)
{
    if (!route_id || !route_id[0]) {
        return NULL;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        if (!strcmp(route_id, ui_rml_routes[i].id)) {
            return &ui_rml_routes[i];
        }
    }

    return NULL;
}

static const char *UI_Rml_AssetRoot(void)
{
    if (ui_rml_asset_root && ui_rml_asset_root->string[0]) {
        return ui_rml_asset_root->string;
    }

    return "ui/rml";
}

static bool UI_Rml_BuildDocumentPath(const ui_rml_route_t *route,
                                     char *path, size_t size)
{
    const char *root = UI_Rml_AssetRoot();
    size_t root_len = strlen(root);
    size_t len;

    if (!root_len) {
        len = Q_strlcpy(path, route->document, size);
    } else if (root[root_len - 1] == '/' || root[root_len - 1] == '\\') {
        len = Q_concat(path, size, root, route->document);
    } else {
        len = Q_concat(path, size, root, "/", route->document);
    }

    return len < size;
}

static bool UI_Rml_RuntimeInterfaceAvailable(void)
{
#if UI_RML_HAS_RUNTIME
    return ui_rml_runtime_registered &&
           ui_rml_runtime.OpenRoute != NULL &&
           !ui_rml_runtime_failed;
#else
    return false;
#endif
}

static bool UI_Rml_RuntimeCanOpenRoutes(void)
{
#if UI_RML_HAS_RUNTIME
    if (!UI_Rml_RuntimeInterfaceAvailable()) {
        return false;
    }

    if (!UI_Rml_RendererIsAvailable()) {
        return false;
    }

    if (ui_rml_runtime.CanOpenRoutes) {
        return ui_rml_runtime.CanOpenRoutes();
    }

    return true;
#else
    return false;
#endif
}

static void UI_Rml_StopRuntime(void);

static bool UI_Rml_StartRuntime(void)
{
#if UI_RML_HAS_RUNTIME
    if (!UI_Rml_RuntimeInterfaceAvailable()) {
        return false;
    }

    if (ui_rml_runtime_started) {
        return true;
    }

    if (ui_rml_runtime.Init && !ui_rml_runtime.Init()) {
        ui_rml_runtime_failed = true;
        return false;
    }

    ui_rml_runtime_started = true;
    return true;
#else
    return false;
#endif
}

static void UI_Rml_StopRuntime(void)
{
#if UI_RML_HAS_RUNTIME
    if (ui_rml_runtime_started && ui_rml_runtime.Shutdown) {
        ui_rml_runtime.Shutdown();
    }

    ui_rml_runtime_started = false;
#endif
}

static bool UI_Rml_RuntimeProbeRoute(const char *route_id)
{
#if UI_RML_HAS_RUNTIME
    const ui_rml_route_t *route = UI_Rml_FindRoute(route_id);
    char path[MAX_QPATH];
    bool was_started = ui_rml_runtime_started;
    bool ok;

    if (!route) {
        Com_Printf("RmlUi route '%s' is not registered.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!ui_rml_runtime_registered || !ui_rml_runtime.ProbeRoute) {
        Com_Printf("RmlUi runtime probe is unavailable; runtime='%s', availability='%s'.\n",
                   UI_Rml_RuntimeName(),
                   UI_Rml_AvailabilityString(UI_Rml_Availability()));
        return false;
    }

    if (!UI_Rml_BuildDocumentPath(route, path, sizeof(path))) {
        Com_Printf("RmlUi route '%s' document path is too long: root='%s', document='%s'.\n",
                   route->id, UI_Rml_AssetRoot(), route->document);
        return false;
    }

    if (!UI_Rml_StartRuntime()) {
        Com_Printf("RmlUi runtime '%s' failed to start for route probe.\n",
                   UI_Rml_RuntimeName());
        return false;
    }

    ok = ui_rml_runtime.ProbeRoute(route->id, path);

    if (!was_started) {
        UI_Rml_StopRuntime();
    }

    return ok;
#else
    Com_Printf("RmlUi runtime probe is unavailable; runtime support is not compiled.\n");
    return false;
#endif
}

static void UI_Rml_ProbeRoute_c(genctx_t *ctx, int argnum)
{
    if (argnum != 1) {
        return;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        Prompt_AddMatch(ctx, ui_rml_routes[i].id);
    }
}

static void UI_Rml_ProbeRoute_f(void)
{
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [route_id]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() == 2) {
        UI_Rml_ProbeRoute(Cmd_Argv(1));
        return;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        UI_Rml_ProbeRoute(ui_rml_routes[i].id);
    }
}

static void UI_Rml_RuntimeProbeRoute_c(genctx_t *ctx, int argnum)
{
    UI_Rml_ProbeRoute_c(ctx, argnum);
}

static void UI_Rml_RuntimeProbeRoute_f(void)
{
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [route_id]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() == 2) {
        UI_Rml_RuntimeProbeRoute(Cmd_Argv(1));
        return;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        UI_Rml_RuntimeProbeRoute(ui_rml_routes[i].id);
    }
}

static const cmdreg_t ui_rml_commands[] = {
    { "ui_rml_probe", UI_Rml_ProbeRoute_f, UI_Rml_ProbeRoute_c },
    { "ui_rml_runtime_probe", UI_Rml_RuntimeProbeRoute_f, UI_Rml_RuntimeProbeRoute_c },
    { NULL }
};

void UI_Rml_Init(void)
{
    if (ui_rml_initialized) {
        return;
    }

    ui_rml_enable = Cvar_Get("ui_rml_enable", "0", 0);
    ui_rml_debug = Cvar_Get("ui_rml_debug", "0", 0);
    ui_rml_asset_root = Cvar_Get("ui_rml_asset_root", "ui/rml", 0);

#if UI_RML_HAS_RUNTIME
    UI_Rml_RegisterCompiledRuntime();
#endif

    if (!Cmd_Exists("ui_rml_probe")) {
        Cmd_Register(ui_rml_commands);
        ui_rml_commands_registered = true;
    }

    ui_rml_initialized = true;

    if (ui_rml_debug->integer) {
        Com_Printf("RmlUi menu scaffold initialized; availability='%s', runtime='%s', renderer='%s', renderer_family='%s', asset_root='%s'.\n",
                   UI_Rml_AvailabilityString(UI_Rml_Availability()),
                   UI_Rml_RuntimeName(),
                   UI_Rml_RendererName(),
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()),
                   UI_Rml_AssetRoot());
    }
}

void UI_Rml_Shutdown(void)
{
    if (!ui_rml_initialized) {
        return;
    }

    if (ui_rml_debug && ui_rml_debug->integer) {
        Com_Printf("RmlUi menu scaffold shut down.\n");
    }

    if (ui_rml_commands_registered) {
        Cmd_Deregister(ui_rml_commands);
        ui_rml_commands_registered = false;
    }

    UI_Rml_StopRuntime();

    ui_rml_initialized = false;
    ui_rml_enable = NULL;
    ui_rml_debug = NULL;
    ui_rml_asset_root = NULL;
}

bool UI_Rml_IsEnabled(void)
{
    return ui_rml_initialized && ui_rml_enable && ui_rml_enable->integer != 0;
}

bool UI_Rml_RuntimeIsAvailable(void)
{
    return UI_Rml_Availability() == UI_RML_AVAILABILITY_READY;
}

ui_rml_availability_t UI_Rml_Availability(void)
{
    if (!ui_rml_initialized) {
        return UI_RML_AVAILABILITY_UNINITIALIZED;
    }

    if (!ui_rml_enable || ui_rml_enable->integer == 0) {
        return UI_RML_AVAILABILITY_DISABLED;
    }

#if UI_RML_HAS_RUNTIME
    if (!UI_Rml_RuntimeInterfaceAvailable()) {
        return UI_RML_AVAILABILITY_RUNTIME_UNAVAILABLE;
    }

    if (!UI_Rml_RuntimeCanOpenRoutes()) {
        return UI_RML_AVAILABILITY_RENDERER_UNAVAILABLE;
    }

    return UI_RML_AVAILABILITY_READY;
#else
    return UI_RML_AVAILABILITY_RUNTIME_NOT_COMPILED;
#endif
}

const char *UI_Rml_AvailabilityString(ui_rml_availability_t availability)
{
    switch (availability) {
    case UI_RML_AVAILABILITY_UNINITIALIZED:
        return "uninitialized";
    case UI_RML_AVAILABILITY_DISABLED:
        return "disabled";
    case UI_RML_AVAILABILITY_RUNTIME_NOT_COMPILED:
        return "runtime_not_compiled";
    case UI_RML_AVAILABILITY_RUNTIME_UNAVAILABLE:
        return "runtime_unavailable";
    case UI_RML_AVAILABILITY_RENDERER_UNAVAILABLE:
        return "renderer_unavailable";
    case UI_RML_AVAILABILITY_READY:
        return "ready";
    default:
        return "unknown";
    }
}

const char *UI_Rml_RuntimeName(void)
{
#if UI_RML_HAS_RUNTIME
    if (ui_rml_runtime_registered && ui_rml_runtime.RuntimeName) {
        const char *name = ui_rml_runtime.RuntimeName();

        if (name && name[0]) {
            return name;
        }
    }

    return ui_rml_runtime_registered ? "registered" : "unregistered";
#else
    return "stub";
#endif
}

const ui_rml_renderer_interface_t *UI_Rml_RendererInterface(void)
{
#if UI_RML_HAS_RUNTIME
    return ui_rml_renderer_registered ? &ui_rml_renderer : NULL;
#else
    return NULL;
#endif
}

ui_rml_renderer_family_t UI_Rml_RendererFamily(void)
{
#if UI_RML_HAS_RUNTIME
    if (ui_rml_renderer_registered) {
        return ui_rml_renderer.family;
    }
#endif

    return UI_RML_RENDERER_FAMILY_NONE;
}

const char *UI_Rml_RendererFamilyString(ui_rml_renderer_family_t family)
{
    switch (family) {
    case UI_RML_RENDERER_FAMILY_OPENGL:
        return "opengl";
    case UI_RML_RENDERER_FAMILY_VULKAN:
        return "vulkan";
    case UI_RML_RENDERER_FAMILY_RTX_VKPT:
        return "rtx_vkpt";
    case UI_RML_RENDERER_FAMILY_NONE:
    default:
        return "none";
    }
}

const char *UI_Rml_RendererName(void)
{
#if UI_RML_HAS_RUNTIME
    if (ui_rml_renderer_registered && ui_rml_renderer.RendererName) {
        const char *name = ui_rml_renderer.RendererName();

        if (name && name[0]) {
            return name;
        }
    }

    if (ui_rml_renderer_registered) {
        return UI_Rml_RendererFamilyString(ui_rml_renderer.family);
    }
#endif

    return "unregistered";
}

bool UI_Rml_RendererIsAvailable(void)
{
#if UI_RML_HAS_RUNTIME
    if (!ui_rml_renderer_registered ||
        ui_rml_renderer.family == UI_RML_RENDERER_FAMILY_NONE ||
        !ui_rml_renderer.CanRender ||
        !ui_rml_renderer.NativeRenderInterface) {
        return false;
    }

    if (!ui_rml_renderer.CanRender()) {
        return false;
    }

    return ui_rml_renderer.NativeRenderInterface() != NULL;
#else
    return false;
#endif
}

const ui_rml_file_interface_t *UI_Rml_FileInterface(void)
{
    return &ui_rml_file_interface;
}

const char *UI_Rml_RouteForMenu(uiMenu_t menu)
{
    switch (menu) {
    case UIMENU_DEFAULT:
    case UIMENU_MAIN:
        return "main";
    case UIMENU_GAME:
        return "game";
    case UIMENU_DOWNLOAD:
        return "download_status";
    case UIMENU_NONE:
    default:
        return NULL;
    }
}

const char *UI_Rml_DocumentForRoute(const char *route_id)
{
    static char path[MAX_QPATH];
    const ui_rml_route_t *route = UI_Rml_FindRoute(route_id);

    if (!route) {
        return NULL;
    }

    if (!UI_Rml_BuildDocumentPath(route, path, sizeof(path))) {
        return NULL;
    }

    return path;
}

bool UI_Rml_ProbeRoute(const char *route_id)
{
    const ui_rml_route_t *route = UI_Rml_FindRoute(route_id);
    const ui_rml_file_interface_t *files = UI_Rml_FileInterface();
    char path[MAX_QPATH];
    void *data = NULL;
    int len;

    if (!route) {
        Com_Printf("RmlUi route '%s' is not registered.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!UI_Rml_BuildDocumentPath(route, path, sizeof(path))) {
        Com_Printf("RmlUi route '%s' document path is too long: root='%s', document='%s'.\n",
                   route->id, UI_Rml_AssetRoot(), route->document);
        return false;
    }

    len = files->LoadFile(path, &data);
    if (!data) {
        Com_Printf("RmlUi route '%s' document probe failed: %s (%s).\n",
                   route->id, path, Q_ErrorString(len));
        return false;
    }

    Com_Printf("RmlUi route '%s' document probe OK: %s (%d bytes).\n",
               route->id, path, len);
    files->FreeFile(data);
    return true;
}

bool UI_Rml_OpenMenu(uiMenu_t menu)
{
    const char *route = UI_Rml_RouteForMenu(menu);
    const char *document;
    bool document_found;

    if (!route || !UI_Rml_IsEnabled()) {
        return false;
    }

    Com_Printf("RmlUi route '%s' requested; probing document.\n", route);
    document_found = UI_Rml_ProbeRoute(route);

    if (!UI_Rml_RuntimeIsAvailable()) {
        Com_Printf("RmlUi availability is '%s' with renderer='%s' family='%s'; falling back to legacy UI.\n",
                   UI_Rml_AvailabilityString(UI_Rml_Availability()),
                   UI_Rml_RendererName(),
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
        return false;
    }

    if (!document_found) {
        return false;
    }

    document = UI_Rml_DocumentForRoute(route);
    if (!document) {
        Com_Printf("RmlUi route '%s' document path is unavailable; falling back to legacy UI.\n",
                   route);
        return false;
    }

    if (!UI_Rml_StartRuntime()) {
        Com_Printf("RmlUi runtime '%s' failed to start; falling back to legacy UI.\n",
                   UI_Rml_RuntimeName());
        return false;
    }

#if UI_RML_HAS_RUNTIME
    if (ui_rml_runtime.OpenRoute(route, document)) {
        return true;
    }

    Com_Printf("RmlUi runtime '%s' declined route '%s'; falling back to legacy UI.\n",
               UI_Rml_RuntimeName(), route);
#endif
    return false;
}

#if UI_RML_HAS_RUNTIME
void UI_Rml_SetRuntimeInterface(const ui_rml_runtime_interface_t *runtime)
{
    UI_Rml_StopRuntime();
    ui_rml_runtime_failed = false;

    if (runtime) {
        ui_rml_runtime = *runtime;
        ui_rml_runtime_registered = runtime->OpenRoute != NULL;
    } else {
        ui_rml_runtime = {};
        ui_rml_runtime_registered = false;
    }
}

void UI_Rml_SetRendererInterface(const ui_rml_renderer_interface_t *renderer)
{
    if (ui_rml_runtime_started) {
        UI_Rml_StopRuntime();
    }

    if (renderer && renderer->family != UI_RML_RENDERER_FAMILY_NONE) {
        ui_rml_renderer = *renderer;
        ui_rml_renderer_registered = true;
    } else {
        UI_Rml_ClearRendererInterface();
    }
}

void UI_Rml_ClearRendererInterface(void)
{
    if (ui_rml_runtime_started) {
        UI_Rml_StopRuntime();
    }

    ui_rml_renderer = {};
    ui_rml_renderer_registered = false;
}
#endif
