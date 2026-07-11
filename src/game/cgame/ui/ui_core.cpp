#include "ui/ui_internal.h"
#include "client/client.h"
#include "ui_cgame_access.h"

#include <cstdarg>

namespace ui {

UiState uis;

static cvar_t *ui_debug;
static cvar_t *ui_open;
static cvar_t *ui_scale;

static void UI_GetVirtualScreen(int *width, int *height, float *pixel_scale)
{
    float scale_x = static_cast<float>(r_config.width) / VIRTUAL_SCREEN_WIDTH;
    float scale_y = static_cast<float>(r_config.height) / VIRTUAL_SCREEN_HEIGHT;
    float base_scale = max(scale_x, scale_y);
    int base_scale_int = static_cast<int>(base_scale);

    if (base_scale_int < 1)
        base_scale_int = 1;

    int virtual_width = r_config.width / base_scale_int;
    int virtual_height = r_config.height / base_scale_int;

    if (virtual_width < 1)
        virtual_width = 1;
    if (virtual_height < 1)
        virtual_height = 1;

    float scale = static_cast<float>(base_scale_int);

    if (width)
        *width = virtual_width;
    if (height)
        *height = virtual_height;
    if (pixel_scale)
        *pixel_scale = scale;
}

MenuSystem &GetMenuSystem()
{
    static MenuSystem system;
    return system;
}

static bool IsActiveMultiplayerMenu()
{
    return CgameIsActiveMultiplayerSession();
}

void UI_DrawString(int x, int y, int flags, color_t color, const char *string)
{
    if (!string || !*string)
        return;

    int draw_x = x;
    if ((flags & UI_CENTER) == UI_CENTER) {
        draw_x -= TextWidth(string) / 2;
    } else if (flags & UI_RIGHT) {
        draw_x -= TextWidth(string);
    }

    UI_FontDrawString(draw_x, y, flags & ~(UI_LEFT | UI_RIGHT),
                      MAX_STRING_CHARS, string, COLOR_SETA_U8(color, 255));
}

void UI_DrawChar(int x, int y, int flags, color_t color, int ch)
{
    if (ch >= 32 && ch < 127) {
        char text[2] = { static_cast<char>(ch), 0 };
        UI_FontDrawString(x, y, flags, 1, text, COLOR_SETA_U8(color, 255));
        return;
    }

    R_DrawChar(x, y, flags, ch, COLOR_SETA_U8(color, 255), UI_FontLegacyHandle());
}

void UI_StringDimensions(vrect_t *rc, int flags, const char *string)
{
    if (!rc)
        return;

    int height = 0;
    rc->width = UI_FontMeasureString(flags, MAX_STRING_CHARS, string ? string : "", &height);
    rc->height = height > 0 ? height : CONCHAR_HEIGHT;

    if ((flags & UI_CENTER) == UI_CENTER) {
        rc->x -= rc->width / 2;
    } else if (flags & UI_RIGHT) {
        rc->x -= rc->width;
    }
}

void UI_DrawRect8(const vrect_t *rc, int border, int c)
{
    if (!rc)
        return;

    R_DrawFill8(rc->x, rc->y, border, rc->height, c);
    R_DrawFill8(rc->x + rc->width - border, rc->y, border, rc->height, c);
    R_DrawFill8(rc->x + border, rc->y, rc->width - border * 2, border, c);
    R_DrawFill8(rc->x + border, rc->y + rc->height - border, rc->width - border * 2, border, c);
}

void *UI_FormatColumns(int extrasize, ...)
{
    va_list argptr;
    char *buffer;
    char *p;
    size_t total = 0;
    char *strings[MAX_COLUMNS];
    size_t lengths[MAX_COLUMNS];

    va_start(argptr, extrasize);
    int count = 0;
    for (; count < MAX_COLUMNS; count++) {
        char *s = va_arg(argptr, char *);
        if (!s)
            break;
        strings[count] = s;
        lengths[count] = strlen(s) + 1;
        total += lengths[count];
    }
    va_end(argptr);

    buffer = static_cast<char *>(UI_Malloc(extrasize + total + 1));
    p = buffer + extrasize;
    for (int i = 0; i < count; i++) {
        memcpy(p, strings[i], lengths[i]);
        p += lengths[i];
    }
    *p = 0;

    return buffer;
}

char *UI_GetColumn(char *s, int n)
{
    int i;
    for (i = 0; i < n && *s; i++) {
        s += strlen(s) + 1;
    }
    return s;
}

void UI_StartSound(Sound sound)
{
    switch (sound) {
    case Sound::In:
        S_StartLocalSound("misc/menu1.wav");
        break;
    case Sound::Move:
        S_StartLocalSound("misc/menu2.wav");
        break;
    case Sound::Out:
        S_StartLocalSound("misc/menu3.wav");
        break;
    case Sound::Beep:
        S_StartLocalSound("misc/talk1.wav");
        break;
    default:
        break;
    }
}

static void UI_Command_ForceMenuOff()
{
    // Server-driven closes (worr_forfeit_yes, welcome continue, ...) must
    // also dismiss an active RmlUi route, or the document keeps rendering
    // with no input focus. ui_rml_runtime_close is a no-op when inactive.
    cvar_t *ui_rml_enable = Cvar_Get("ui_rml_enable", "0", 0);
    if (ui_rml_enable && ui_rml_enable->integer) {
        Cbuf_InsertText(&cmd_buffer, "ui_rml_runtime_close\n");
    }

    GetMenuSystem().ForceOff();
}

static void UI_Command_NoOp()
{
}

static bool UI_IsRmlRouteName(const char *menu_name)
{
    static const char *const rml_routes[] = {
        "main",
        "game",
        "options",
        "video",
        "multimonitor",
        "performance",
        "accessibility",
        "sound",
        "railtrail",
        "effects",
        "crosshair",
        "screen",
        "language",
        "downloads",
        "download_status",
        "addressbook",
        "input",
        "keys",
        "legacykeys",
        "weapons",
        "quit_confirm",
        "gameflags",
        "startserver",
        "multiplayer",
        "singleplayer",
        "skill_select",
        "loadgame",
        "savegame",
        "servers",
        "demos",
        "players",
        "ui_list",
        "dm_welcome",
        "dm_join",
        "join",
        "dm_hostinfo",
        "dm_matchinfo",
        "callvote_main",
        "callvote_ruleset",
        "callvote_timelimit",
        "callvote_scorelimit",
        "callvote_unlagged",
        "callvote_random",
        "callvote_map_flags",
        "mymap_main",
        "mymap_flags",
        "forfeit_confirm",
        "leave_match_confirm",
        "admin_menu",
        "admin_commands",
        "tourney_info",
        "tourney_mapchoices",
        "tourney_veto",
        "tourney_replay_confirm",
        "vote_menu",
        "map_selector",
        "match_stats",
    };

    if (!menu_name || !menu_name[0])
        return false;

    for (const char *route : rml_routes) {
        if (!strcmp(menu_name, route))
            return true;
    }

    return false;
}

static bool UI_IsRmlPopupRouteName(const char *menu_name)
{
    return menu_name &&
           (!strcmp(menu_name, "quit_confirm") ||
            !strcmp(menu_name, "forfeit_confirm") ||
            !strcmp(menu_name, "leave_match_confirm") ||
            !strcmp(menu_name, "tourney_replay_confirm"));
}

static bool UI_Command_TryPushRmlRoute(const char *menu_name)
{
    cvar_t *ui_rml_enable = Cvar_Get("ui_rml_enable", "0", 0);
    cvar_t *ui_rml_runtime_available =
        Cvar_Get("ui_rml_runtime_available", "0", CVAR_ROM);

    if (!ui_rml_enable || !ui_rml_enable->integer ||
        !ui_rml_runtime_available || !ui_rml_runtime_available->integer ||
        !UI_IsRmlRouteName(menu_name)) {
        return false;
    }

    Cbuf_InsertText(&cmd_buffer,
                    va("%s %s\n",
                       UI_IsRmlPopupRouteName(menu_name)
                           ? "ui_rml_runtime_popup_fallback"
                           : "ui_rml_runtime_open_fallback",
                       menu_name));
    if (Cvar_VariableInteger("ui_rml_debug")) {
        Com_Printf("RmlUi pushmenu bridge routed '%s' through %s.\n",
                   menu_name,
                   UI_IsRmlPopupRouteName(menu_name)
                       ? "ui_rml_runtime_popup_fallback"
                       : "ui_rml_runtime_open_fallback");
    }
    return true;
}

static void UI_Command_PushMenu()
{
    if (Cmd_Argc() < 2) {
        PrintLocalized("$ui_cmd_menu_usage", Cmd_Argv(0));
        return;
    }

    const char *menu_name = Cmd_Argv(1);
    if (UI_Command_TryPushRmlRoute(menu_name))
        return;

    MenuPage *menu = GetMenuSystem().FindMenu(menu_name);
    if (menu) {
        const char *args = Cmd_RawArgsFrom(2);
        if (args && *args)
            menu->SetArgs(args);
        GetMenuSystem().Push(menu);
    } else {
        PrintLocalized("$ui_menu_not_found", menu_name);
    }
}

static void UI_Command_PopMenu()
{
    // With no legacy pages open, a popmenu issued from RmlUi content or
    // server stufftext pops the RmlUi route stack instead.
    if (!GetMenuSystem().HasOpenMenus()) {
        cvar_t *ui_rml_enable = Cvar_Get("ui_rml_enable", "0", 0);
        if (ui_rml_enable && ui_rml_enable->integer) {
            Cbuf_InsertText(&cmd_buffer, "ui_rml_runtime_back\n");
            return;
        }
    }

    GetMenuSystem().Pop();
}

static void UI_Menu_g(genctx_t *ctx)
{
    (void)ctx;
}

static void UI_PushMenu_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1)
        UI_Menu_g(ctx);
}

static const cmdreg_t c_ui[] = {
    { "forcemenuoff", UI_Command_ForceMenuOff },
    { "ui_nop", UI_Command_NoOp },
    { "pushmenu", UI_Command_PushMenu, UI_PushMenu_c },
    { "popmenu", UI_Command_PopMenu },
    { NULL, NULL }
};

static void ui_scale_changed(cvar_t *self)
{
    (void)self;
    GetMenuSystem().ModeChanged();
}

void MenuSystem::Resize()
{
    UI_Sys_UpdateRefConfig();

    int base_width = r_config.width;
    int base_height = r_config.height;
    UI_GetVirtualScreen(&base_width, &base_height, NULL);

    uis.scale = R_ClampScale(ui_scale);
    uis.width = Q_rint(base_width * uis.scale);
    uis.height = Q_rint(base_height * uis.scale);
    uis.canvas_width = r_config.width;
    uis.canvas_height = r_config.height;
}

void MenuSystem::Init()
{
    Cmd_Register(c_ui);

    ui_debug = Cvar_Get("ui_debug", "0", 0);
    ui_open = Cvar_Get("ui_open", "1", 0);
    ui_scale = Cvar_Get("ui_scale", "0", 0);

    ui_scale->changed = ui_scale_changed;

    UI_ResetBindIconCache();

    Resize();

    uis.fontHandle = UI_FontLegacyHandle();
    // Use the shipped Quake menu cursor as the native-UI pointer. The former
    // loose /gfx cursor paths are not part of the distributable asset tree and
    // resolve to the magenta missing-texture tile on Vulkan/RTX.
    uis.cursorHandle = R_RegisterPic("m_cursor0");
    R_GetPicSize(&uis.cursorWidth, &uis.cursorHeight, uis.cursorHandle);
    uis.cursorWidth = UI_CURSOR_SIZE;
    uis.cursorHeight = UI_CURSOR_SIZE;
    uis.cursorTextHandle = uis.cursorHandle;
    R_GetPicSize(&uis.cursorTextWidth, &uis.cursorTextHeight, uis.cursorTextHandle);
    uis.cursorTextWidth = UI_CURSOR_SIZE;
    uis.cursorTextHeight = UI_CURSOR_SIZE;

    for (int i = 0; i < NUM_CURSOR_FRAMES; i++) {
        uis.bitmapCursors[i] = R_RegisterPic(va("m_cursor%d", i));
    }

    uis.color.background = COLOR_RGBA(0, 0, 0, 255);
    uis.color.normal = COLOR_RGBA(62, 100, 59, 112);
    uis.color.active = COLOR_RGBA(91, 138, 74, 144);
    uis.color.selection = COLOR_RGBA(63, 106, 58, 176);
    uis.color.disabled = COLOR_RGBA(127, 127, 127, 255);
    Q_strlcpy(uis.weaponModel, "w_railgun.md2", sizeof(uis.weaponModel));

    UI_MapDB_Init();
    PlayerModel_Load();

    UI_LoadJsonMenus(UI_DEFAULT_FILE);
    UI_LoadJsonMenus(UI_MULTIPLAYER_FILE);
    if (!FindMenu("players"))
        RegisterMenu(CreatePlayerConfigPage());
    if (!FindMenu("dm_welcome"))
        RegisterMenu(CreateWelcomePage());

    uis.initialized = true;
}

void MenuSystem::ModeChanged()
{
    Resize();
}

void MenuSystem::Shutdown()
{
    if (!uis.initialized)
        return;

    ForceOff();

    ui_scale->changed = NULL;
    PlayerModel_Free();
    UI_MapDB_Shutdown();
    menus_.clear();
    stack_.clear();

    Cmd_Deregister(c_ui);

    memset(&uis, 0, sizeof(uis));
    Z_LeakTest(TAG_UI);
}

void MenuSystem::RegisterMenu(std::unique_ptr<MenuPage> menu)
{
    if (!menu)
        return;
    menus_[menu->Name()] = std::move(menu);
}

MenuPage *MenuSystem::FindMenu(const char *name) const
{
    auto it = menus_.find(name);
    if (it == menus_.end())
        return nullptr;
    return it->second.get();
}

void MenuSystem::Push(MenuPage *menu)
{
    if (!menu)
        return;

    auto existing = std::find(stack_.begin(), stack_.end(), menu);
    if (existing != stack_.end()) {
        while (stack_.back() != menu) {
            Pop();
        }
        return;
    }

    if (stack_.size() >= MAX_MENU_DEPTH) {
        Com_EPrintfLoc("$ui_pushmenu_max_depth");
        return;
    }

    stack_.push_back(menu);
    menu->OnOpen();

    Key_SetDest(static_cast<keydest_t>((Key_GetDest() & ~KEY_CONSOLE) | KEY_MENU));
    Con_Close(true);

    if (!active_) {
        float pixel_scale = 1.0f;
        UI_GetVirtualScreen(NULL, NULL, &pixel_scale);
        int x = Q_rint((uis.width / 2.0f) * pixel_scale);
        int y = Q_rint((uis.height / 2.0f) * pixel_scale);
        IN_WarpMouse(x, y);
        uis.mouseCoords[0] = uis.width / 2;
        uis.mouseCoords[1] = uis.height / 2;
        uis.entersound = true;
    }

    active_ = menu;
}

void MenuSystem::Pop()
{
    if (stack_.empty())
        return;

    MenuPage *menu = stack_.back();
    stack_.pop_back();
    menu->OnClose();

    if (stack_.empty()) {
        ForceOff();
        return;
    }

    active_ = stack_.back();
    uis.keywait = false;
}

void MenuSystem::ForceOff()
{
    const bool closesMatchHub =
        !stack_.empty() && Cvar_VariableInteger("ui_dm_menu_active") != 0;

    for (MenuPage *menu : stack_) {
        menu->OnClose();
    }
    stack_.clear();
    active_ = nullptr;
    uis.keywait = false;
    uis.transparent = false;
    UI_Sys_SetMenuBlurRect(nullptr);
    Key_SetDest(static_cast<keydest_t>(Key_GetDest() & ~KEY_MENU));

    if (closesMatchHub) {
        Cvar_SetInteger(Cvar_Get("ui_dm_menu_active", "0", 0),
                        0, FROM_CODE);
        Cbuf_AddText(&cmd_buffer, "worr_dm_join_close\n");
    }
}

void MenuSystem::OpenMenu(uiMenu_t menu)
{
    if (!uis.initialized)
        return;

    const bool is_multiplayer_main = IsActiveMultiplayerMenu();

    ForceOff();

    MenuPage *target = nullptr;
    switch (menu) {
    case UIMENU_DEFAULT:
        if (ui_open->integer) {
            target = FindMenu(is_multiplayer_main ? "dm_join" : "main");
        }
        break;
    case UIMENU_MAIN:
        target = FindMenu(is_multiplayer_main ? "dm_join" : "main");
        break;
    case UIMENU_GAME:
        target = FindMenu("game");
        if (!target)
            target = FindMenu("main");
        break;
    case UIMENU_DOWNLOAD:
        target = FindMenu("download_status");
        break;
    case UIMENU_NONE:
        break;
    default:
        break;
    }

    Push(target);
}

void MenuSystem::Draw(unsigned realtime)
{
    UI_Sys_UpdateRefConfig();
    uis.realtime = realtime;

    if (!(Key_GetDest() & KEY_MENU) || !active_) {
        UI_Sys_SetMenuBlurRect(nullptr);
        return;
    }

    R_SetScale(uis.scale);
    active_->Draw();

    bool use_text_cursor = active_ && active_->WantsTextCursor(uis.mouseCoords[0], uis.mouseCoords[1]);

    // Draw a small code-native pointer so every renderer has a crisp cursor
    // without depending on loose legacy PCX assets in the distributable.
    const int cursor_x = uis.mouseCoords[0];
    const int cursor_y = uis.mouseCoords[1];
    const color_t cursor_shadow = COLOR_RGBA(0, 0, 0, 210);
    const color_t cursor_color = COLOR_RGBA(255, 217, 103, 255);
    if (use_text_cursor) {
        R_DrawFill32(cursor_x + 1, cursor_y - 7, 2, 18, cursor_shadow);
        R_DrawFill32(cursor_x, cursor_y - 8, 2, 18, cursor_color);
    } else {
        R_DrawFill32(cursor_x + 1, cursor_y + 1, 3, 17, cursor_shadow);
        R_DrawFill32(cursor_x, cursor_y, 3, 17, cursor_color);
        for (int i = 0; i < 9; ++i) {
            R_DrawFill32(cursor_x + i + 1, cursor_y + i + 1,
                         3, 3, cursor_shadow);
            R_DrawFill32(cursor_x + i, cursor_y + i,
                         3, 3, cursor_color);
        }
    }

    if (ui_debug->integer) {
        UI_DrawString(uis.width - 4, 4, UI_RIGHT, COLOR_WHITE,
                      va("%3i %3i", uis.mouseCoords[0], uis.mouseCoords[1]));
    }

    if (uis.entersound) {
        uis.entersound = false;
        S_StartLocalSound("misc/menu1.wav");
    }

    R_SetScale(1.0f);
}

void MenuSystem::KeyEvent(int key, bool down)
{
    if (!active_)
        return;

    if (!down) {
        return;
    }

    Sound sound = active_->KeyEvent(key);
    UI_StartSound(sound);
}

void MenuSystem::CharEvent(int ch)
{
    if (!active_)
        return;

    Sound sound = active_->CharEvent(ch);
    UI_StartSound(sound);
}

void MenuSystem::MouseEvent(int x, int y)
{
    UI_Sys_UpdateRefConfig();

    x = Q_clip(x, 0, r_config.width - 1);
    y = Q_clip(y, 0, r_config.height - 1);

    float pixel_scale = 1.0f;
    UI_GetVirtualScreen(NULL, NULL, &pixel_scale);
    float inv_scale = (pixel_scale > 0.0f) ? (1.0f / pixel_scale) : 1.0f;
    uis.mouseCoords[0] = Q_rint((x * inv_scale) * uis.scale);
    uis.mouseCoords[1] = Q_rint((y * inv_scale) * uis.scale);

    if (active_)
        active_->MouseEvent(uis.mouseCoords[0], uis.mouseCoords[1], false);
}

void MenuSystem::Frame(int msec)
{
    if (active_)
        active_->Frame(msec);
}

void MenuSystem::StatusEvent(const serverStatus_t *status)
{
    if (MenuPage *servers = FindMenu("servers"))
        servers->StatusEvent(status);
}

void MenuSystem::ErrorEvent(const netadr_t *from)
{
    if (MenuPage *servers = FindMenu("servers"))
        servers->ErrorEvent(from);
}

bool MenuSystem::IsTransparent() const
{
    if (!(Key_GetDest() & KEY_MENU))
        return true;
    if (!active_)
        return true;
    return active_->IsTransparent();
}

} // namespace ui

extern "C" {

void UI_Init(void)
{
    ui::GetMenuSystem().Init();
}

void UI_Shutdown(void)
{
    ui::GetMenuSystem().Shutdown();
}

void UI_ModeChanged(void)
{
    ui::GetMenuSystem().ModeChanged();
}

void UI_KeyEvent(int key, bool down)
{
    ui::GetMenuSystem().KeyEvent(key, down);
}

void UI_CharEvent(int key)
{
    ui::GetMenuSystem().CharEvent(key);
}

void UI_Draw(unsigned realtime)
{
    ui::GetMenuSystem().Draw(realtime);
}

void UI_OpenMenu(uiMenu_t menu)
{
    ui::GetMenuSystem().OpenMenu(menu);
}

void UI_Frame(int msec)
{
    ui::GetMenuSystem().Frame(msec);
}

void UI_StatusEvent(const serverStatus_t *status)
{
    ui::GetMenuSystem().StatusEvent(status);
}

void UI_ErrorEvent(const netadr_t *from)
{
    ui::GetMenuSystem().ErrorEvent(from);
}

void UI_MouseEvent(int x, int y)
{
    ui::GetMenuSystem().MouseEvent(x, y);
}

bool UI_IsTransparent(void)
{
    return ui::GetMenuSystem().IsTransparent();
}

} // extern "C"

