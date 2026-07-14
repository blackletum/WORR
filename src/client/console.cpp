/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// console.c

#include "client.h"
#include "client/font.h"

#define CON_TIMES 16
#define CON_TIMES_MASK (CON_TIMES - 1)

#define CON_TOTALLINES 1024 // total lines in console scrollback
#define CON_TOTALLINES_MASK (CON_TOTALLINES - 1)

#define CON_LINEWIDTH 126 // fixed width, do not need more
#define CON_COMPLETION_MAX_MATCHES 256
#define CON_COMPLETION_MAX_VISIBLE 8

typedef enum { CHAT_NONE, CHAT_DEFAULT, CHAT_TEAM } chatMode_t;

typedef enum { CON_POPUP, CON_DEFAULT, CON_REMOTE } consoleMode_t;

typedef struct {
  byte color;
  byte ts_len;
  char text[CON_LINEWIDTH];
} consoleLine_t;

typedef struct {
  consoleLine_t text[CON_TOTALLINES];

  int current; // line where next message will be printed
  int x;       // offset in current line for next print
  int display; // bottom of console displays this line
  float displayLine; // animated visual counterpart to display
  color_index_t color;
  int newline;

  int linewidth; // characters across screen
  int vidWidth, vidHeight;
  int displayX, displayWidth;
  float scale;
  color_t ts_color;
  color_t backgroundColor;
  color_t lineColor;
  color_t versionColor;

  unsigned times[CON_TIMES]; // cls.realtime time the line was generated
                             // for transparent notify lines
  bool skipNotify;
  bool initialized;

  qhandle_t backImage;
  qhandle_t charsetImage;
  qhandle_t cursorImage;
  font_t *font;
  float font_pixel_scale;

  float currentHeight; // approaches scr_conlines at scr_conspeed
  float destHeight;    // 0.0 to 1.0 lines of console to display

  commandPrompt_t chatPrompt;
  commandPrompt_t prompt;

  chatMode_t chat;
  consoleMode_t mode;
  netadr_t remoteAddress;
  char *remotePassword;

  load_state_t loadstate;

  char completionMatches[CON_COMPLETION_MAX_MATCHES][MAX_FIELD_TEXT];
  char completionSnapshot[MAX_FIELD_TEXT];
  size_t completionSnapshotCursor;
  size_t completionReplaceOffset;
  size_t completionReplaceLength;
  int completionCount;
  int completionSelection;
  int completionScroll;
  bool completionAppendSpace;
  bool completionPrependSlash;
  bool completionPopupVisible;
  bool completionSnapshotValid;

  float mouseX, mouseY;
  float scrollbarHover;
  float scrollbarDragOffset;
  bool mouseInitialized;
  bool scrollbarDragging;
  bool inputSelecting;
  bool logSelecting;
  bool logFocus;
  bool scrollbarValid;
  int scrollbarX, scrollbarY, scrollbarW, scrollbarH;
  int scrollbarThumbY, scrollbarThumbH;
  bool inputRectValid;
  int inputX, inputY, inputW, inputH;
  bool completionRectValid;
  int completionX, completionY, completionW, completionH;
  int completionRowY, completionVisible;
  bool completionScrollbarDragging;
  float completionScrollbarDragOffset;
  bool completionScrollbarValid;
  int completionScrollbarX, completionScrollbarY;
  int completionScrollbarW, completionScrollbarH;
  int completionScrollbarThumbY, completionScrollbarThumbH;
  bool logRectValid;
  int logTopY, logBottomY;
  float logDisplayLine;
  int logAnchorLine, logAnchorColumn;
  int logLine, logColumn;
  bool textDragPending, textDragging, textDragFromInput;
  float textDragStartX, textDragStartY;
  size_t textDragInputStart, textDragInputEnd;
} console_t;

static console_t con;

static cvar_t *con_notifytime;
static cvar_t *con_notifylines;
static cvar_t *con_clock;
static cvar_t *con_height;
static cvar_t *con_speed;
static cvar_t *con_speed_legacy;
static cvar_t *con_alpha;
static cvar_t *con_scale;
static cvar_t *con_font;
static cvar_t *con_background;
static cvar_t *con_scroll;
static cvar_t *con_scroll_lines;
static cvar_t *con_scroll_smooth;
static cvar_t *con_scroll_smooth_speed;
static cvar_t *con_completion_popup;
static cvar_t *con_screen_extents;
static cvar_t *con_background_style;
static cvar_t *con_background_color;
static cvar_t *con_background_opacity;
static cvar_t *con_line_color;
static cvar_t *con_version_color;
static cvar_t *con_show_version;
static cvar_t *con_fade;
static cvar_t *con_say_raw;
static cvar_t *con_history;
static cvar_t *con_timestamps;
static cvar_t *con_timestampsformat;
static cvar_t *con_timestampscolor;
static cvar_t *con_auto_chat;
static cvar_t *ui_download_active;
static cvar_t *con_fontscale;
static cvar_t *con_fontsize;
static cvar_t *cl_font_skip_virtual_scale;
static int con_font_settings_generation;
static const char k_console_kfont_fallback[] = "fonts/qconfont.kfont";

static bool con_speed_alias_syncing;

static void con_speed_alias_changed(cvar_t *self)
{
  if (con_speed_alias_syncing)
    return;

  con_speed_alias_syncing = true;

  if (self == con_speed && con_speed_legacy)
    Cvar_SetByVar(con_speed_legacy, con_speed->string, FROM_CODE);
  else if (self == con_speed_legacy && con_speed)
    Cvar_SetByVar(con_speed, con_speed_legacy->string, FROM_CODE);

  con_speed_alias_syncing = false;
}

static void con_speed_alias_sync_defaults(void)
{
  if (con_speed_alias_syncing)
    return;

  con_speed_alias_syncing = true;

  if (con_speed && con_speed_legacy) {
    if (!(con_speed->flags & CVAR_MODIFIED) && (con_speed_legacy->flags & CVAR_MODIFIED))
      Cvar_SetByVar(con_speed, con_speed_legacy->string, FROM_CODE);
    else
      Cvar_SetByVar(con_speed_legacy, con_speed->string, FROM_CODE);
  }

  con_speed_alias_syncing = false;
}

static void con_speed_alias_register(void)
{
  if (con_speed)
    con_speed->changed = con_speed_alias_changed;
  if (con_speed_legacy)
    con_speed_legacy->changed = con_speed_alias_changed;

  con_speed_alias_sync_defaults();
}

static float Con_GetFontPixelScale(void);
static int Con_FontCharWidth(void);
static int Con_FontCharHeight(void);
static void con_color_changed(cvar_t *self);
static void Con_RefreshCompletionState(void);
static void Con_DismissCompletionPopup(void);
static void Con_ApplySelectedCompletion(int direction);
static void Con_Test_f(void);
static void Con_TestVisual_f(void);
static bool con_test_visual_request;

// ============================================================================

/*
================
Con_SkipNotify
================
*/
void Con_SkipNotify(bool skip) { con.skipNotify = skip; }

/*
================
Con_ClearTyping
================
*/
void Con_ClearTyping(void) {
  // clear any typing
  IF_Clear(&con.prompt.inputLine);
  Prompt_ClearState(&con.prompt);
}

/*
================
Con_Close

Instantly removes the console. Unless `force' is true, does not remove the
console if user has typed something into it since the last call to Con_Popup.
================
*/
void Con_Close(bool force) {
  if (con.mode > CON_POPUP && !force) {
    return;
  }

  // if not connected, console or menu should be up
  if (cls.state < ca_active && !(cls.key_dest & KEY_MENU)) {
    return;
  }

  Con_ClearTyping();
  Con_ClearNotify_f();

  Key_SetDest(static_cast<keydest_t>(cls.key_dest & ~KEY_CONSOLE));

  con.destHeight = con.currentHeight = 0;
  con.mode = CON_POPUP;
  con.chat = CHAT_NONE;
}

/*
================
Con_Popup

Drop to connection screen. Unless `force' is true, does not change console mode
to popup.
================
*/
void Con_Popup(bool force) {
  if (force) {
    con.mode = CON_POPUP;
  }

  Key_SetDest(static_cast<keydest_t>(cls.key_dest | KEY_CONSOLE));
  Con_RunConsole();
}

/*
================
Con_ToggleConsole_f

Toggles console up/down animation.
================
*/
static void toggle_console(consoleMode_t mode, chatMode_t chat) {
  SCR_EndLoadingPlaque(); // get rid of loading plaque

  Con_ClearTyping();
  Con_ClearNotify_f();

  if (cls.key_dest & KEY_CONSOLE) {
    Key_SetDest(static_cast<keydest_t>(cls.key_dest & ~KEY_CONSOLE));
    con.mode = CON_POPUP;
    con.chat = CHAT_NONE;
    return;
  }

  // toggling console discards chat message
  if (cls.key_dest & KEY_MENU)
    UI_CloseMenu();
  Key_SetDest(
      static_cast<keydest_t>((cls.key_dest | KEY_CONSOLE) &
                             ~(KEY_MESSAGE | KEY_MENU)));
  con.mode = mode;
  con.chat = chat;
}

void Con_ToggleConsole_f(void) { toggle_console(CON_DEFAULT, CHAT_NONE); }

/*
================
Con_Clear_f
================
*/
static void Con_Clear_f(void) {
  memset(con.text, 0, sizeof(con.text));
  con.display = con.current = 0;
  con.displayLine = 0.0f;
  con.newline = '\r';
}

static void Con_Dump_c(genctx_t *ctx, int argnum) {
  if (argnum == 1) {
    FS_File_g("condumps", ".txt", FS_SEARCH_STRIPEXT, ctx);
  }
}

/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
static void Con_Dump_f(void) {
  int l;
  qhandle_t f;
  char name[MAX_OSPATH];

  if (Cmd_Argc() != 2) {
    Com_PrintfLoc("$cl_filename_usage", Cmd_Argv(0));
    return;
  }

  f = FS_EasyOpenFile(name, sizeof(name), FS_MODE_WRITE | FS_FLAG_TEXT,
                      "condumps/", Cmd_Argv(1), ".txt");
  if (!f) {
    return;
  }

  // skip empty lines
  for (l = con.current - CON_TOTALLINES + 1; l <= con.current; l++) {
    if (con.text[l & CON_TOTALLINES_MASK].text[0]) {
      break;
    }
  }

  // write the remaining lines
  for (; l <= con.current; l++) {
    char buffer[CON_LINEWIDTH + 1];
    const char *p = con.text[l & CON_TOTALLINES_MASK].text;
    int i;

    for (i = 0; i < CON_LINEWIDTH && p[i]; i++)
      buffer[i] = Q_charascii(p[i]);
    buffer[i] = '\n';

    FS_Write(buffer, i + 1, f);
  }

  if (FS_CloseFile(f))
    Com_EPrintfLoc("$cl_error_writing_file", name);
  else
    Com_PrintfLoc("$con_dumped_to_file", name);
}

/*
================
Con_ClearNotify_f
================
*/
void Con_ClearNotify_f(void) {
  int i;

  for (i = 0; i < CON_TIMES; i++)
    con.times[i] = 0;
}

/*
================
Con_MessageMode_f
================
*/
static void start_message_mode(chatMode_t mode) {
  if (cls.state != ca_active || cls.demo.playback) {
    Com_PrintfLoc("$cl_chat_requires_level");
    return;
  }

  // starting messagemode closes console
  if (cls.key_dest & KEY_CONSOLE) {
    Con_Close(true);
  }

  con.chat = mode;
  IF_Replace(&con.chatPrompt.inputLine, COM_StripQuotes(Cmd_RawArgs()));
  Key_SetDest(static_cast<keydest_t>(cls.key_dest | KEY_MESSAGE));
}

static void Con_MessageMode_f(void) { start_message_mode(CHAT_DEFAULT); }

static void Con_MessageMode2_f(void) { start_message_mode(CHAT_TEAM); }

/*
================
Con_RemoteMode_f
================
*/
static void Con_RemoteMode_f(void) {
  netadr_t adr;
  char *s;

  if (Cmd_Argc() != 3) {
    Com_PrintfLoc("$cl_rcon_usage", Cmd_Argv(0));
    return;
  }

  s = Cmd_Argv(1);
  if (!NET_StringToAdr(s, &adr, PORT_SERVER)) {
    Com_PrintfLoc("$cl_bad_address", s);
    return;
  }

  s = Cmd_Argv(2);

  if (!(cls.key_dest & KEY_CONSOLE)) {
    toggle_console(CON_REMOTE, CHAT_NONE);
  } else {
    con.mode = CON_REMOTE;
    con.chat = CHAT_NONE;
  }

  Z_Free(con.remotePassword);

  con.remoteAddress = adr;
  con.remotePassword = Z_CopyString(s);
}

static void CL_RemoteMode_c(genctx_t *ctx, int argnum) {
  if (argnum == 1) {
    Com_Address_g(ctx);
  }
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize(void) {
  con.scale = R_ClampScale(con_scale);

  int base_width = scr.virtual_width ? scr.virtual_width : r_config.width;
  int base_height = scr.virtual_height ? scr.virtual_height : r_config.height;

  con.vidWidth = Q_rint(base_width * con.scale);
  con.vidHeight = Q_rint(base_height * con.scale);
  con.displayWidth = con.vidWidth;
  if (Cvar_ClampInteger(con_screen_extents, 0, 1))
    con.displayWidth = min(con.vidWidth,
                           Q_rint(base_height * (4.0f / 3.0f) * con.scale));
  con.displayX = max(0, (con.vidWidth - con.displayWidth) / 2);

  int char_width = Con_FontCharWidth();
  con.linewidth = Q_clip(con.displayWidth / char_width - 2, 0, CON_LINEWIDTH);
  con.prompt.inputLine.visibleChars = con.linewidth;
  con.prompt.widthInChars = con.linewidth;
  con.chatPrompt.inputLine.visibleChars = con.linewidth;

  if (con_timestamps->integer) {
    char temp[CON_LINEWIDTH];
    con.prompt.widthInChars -=
        Com_FormatLocalTime(temp, con.linewidth, con_timestampsformat->string);
  }

  if (con.font && cls.ref_initialized) {
    float pixel_scale = Con_GetFontPixelScale();
    if (con.font_pixel_scale != pixel_scale ||
        con_font_settings_generation != Font_SettingsGeneration())
      Con_RegisterMedia();
  }
}

static float Con_GetFontPixelScale(void) {
  if (!cl_font_skip_virtual_scale)
    cl_font_skip_virtual_scale =
        Cvar_Get("cl_font_skip_virtual_scale", "0", CVAR_ARCHIVE);

  float scale = con.scale > 0.0f ? con.scale : 1.0f;
  bool skip_virtual_scale =
      cl_font_skip_virtual_scale && cl_font_skip_virtual_scale->integer;
  return CL_CalcFontPixelScale(r_config.width, r_config.height, scale,
                               skip_virtual_scale);
}

static int Con_FontCharWidth(void) {
  if (con.font) {
    int width = Font_MeasureString(con.font, 1, 0, 1, "M", nullptr);
    if (width > 0)
      return width;
  }
  return CONCHAR_WIDTH;
}

static int Con_FontCharHeight(void) {
  if (con.font)
    return Font_LineHeight(con.font, 1);
  return CONCHAR_HEIGHT;
}

static int Con_MeasureString(const char *text, size_t max_chars) {
  if (!text || !*text)
    return 0;

  if (con.font)
    return Font_MeasureString(con.font, 1, 0, max_chars, text, nullptr);

  size_t len = strlen(text);
  if (len > max_chars)
    len = max_chars;
  return (int)Com_StrlenNoColor(text, len) * CONCHAR_WIDTH;
}

static int Con_DrawString(int x, int y, int flags, size_t max_chars,
                          const char *text, color_t color) {
  if (con.font)
    return Font_DrawString(con.font, x, y, 1, flags, max_chars, text, color);
  return R_DrawString(x, y, flags, max_chars, text, color, con.charsetImage);
}

/*
================
Con_CheckTop

Make sure at least one line is visible if console is backscrolled.
================
*/
static void Con_CheckTop(void) {
  int top = con.current - CON_TOTALLINES + 1;

  if (top < 0) {
    top = 0;
  }
  if (con.display < top) {
    con.display = top;
  }
}

static void con_media_changed(cvar_t *self) {
  if (con.initialized && cls.ref_initialized) {
    Con_RegisterMedia();
    Con_CheckResize();
  }
}

static void con_width_changed(cvar_t *self) {
  if (con.initialized && cls.ref_initialized) {
    Con_CheckResize();
  }
}

static void con_timestampscolor_changed(cvar_t *self) {
  if (!SCR_ParseColor(self->string, &con.ts_color)) {
    Com_WPrintfLoc("$con_invalid_value", self->string, self->name);
    Cvar_Reset(self);
    con.ts_color = COLOR_RGB(170, 170, 170);
  }
}

static const cmdreg_t c_console[] = {
    {"toggleconsole", Con_ToggleConsole_f},
    {"messagemode", Con_MessageMode_f},
    {"messagemode2", Con_MessageMode2_f},
    {"remotemode", Con_RemoteMode_f, CL_RemoteMode_c},
    {"clear", Con_Clear_f},
    {"clearnotify", Con_ClearNotify_f},
    {"condump", Con_Dump_f, Con_Dump_c},
    {"con_test", Con_Test_f},
    {"con_test_visual", Con_TestVisual_f},

    {NULL}};

/*
================
Con_Init
================
*/
void Con_Init(void) {
  memset(&con, 0, sizeof(con));

  //
  // register our commands
  //
  Cmd_Register(c_console);

  con_notifytime = Cvar_Get("con_notifytime", "0", 0);
  con_notifytime->changed = cl_timeout_changed;
  con_notifytime->changed(con_notifytime);
  con_notifylines = Cvar_Get("con_notifylines", "4", 0);
  con_clock = Cvar_Get("con_clock", "0", 0);
  con_height = Cvar_Get("con_height", "0.66", 0);
  con_speed = Cvar_Get("con_speed", "3", 0);
  con_speed_legacy = Cvar_Get("scr_conspeed", con_speed->string, CVAR_NOARCHIVE);
  con_speed_alias_register();
  con_alpha = Cvar_Get("con_alpha", "1", 0);
  con_scale = Cvar_Get("con_scale", "0", 0);
  con_scale->changed = con_width_changed;
  con_font = Cvar_Get("con_font", "fonts/RobotoMono-Regular.ttf", 0);
  con_font->changed = con_media_changed;
  con_fontscale = Cvar_Get("con_fontscale", "5", 0);
  con_fontscale->changed = con_media_changed;
  con_fontsize = Cvar_Get("con_fontsize", "6", 0);
  con_fontsize->changed = con_media_changed;
  con_background = Cvar_Get("con_background", "conback", 0);
  con_background->changed = con_media_changed;
  con_scroll = Cvar_Get("con_scroll", "0", 0);
  con_scroll_lines = Cvar_Get("con_scroll_lines", "8", CVAR_ARCHIVE);
  con_scroll_smooth = Cvar_Get("con_scroll_smooth", "1", CVAR_ARCHIVE);
  con_scroll_smooth_speed =
      Cvar_Get("con_scroll_smooth_speed", "72", CVAR_ARCHIVE);
  con_completion_popup = Cvar_Get("con_completion_popup", "1", CVAR_ARCHIVE);
  con_screen_extents = Cvar_Get("con_screen_extents", "0", CVAR_ARCHIVE);
  con_background_style = Cvar_Get("con_background_style", "0", CVAR_ARCHIVE);
  con_background_color =
      Cvar_Get("con_background_color", "#180000", CVAR_ARCHIVE);
  con_background_opacity =
      Cvar_Get("con_background_opacity", "0.8", CVAR_ARCHIVE);
  con_line_color = Cvar_Get("con_line_color", "#566c42", CVAR_ARCHIVE);
  con_version_color =
      Cvar_Get("con_version_color", "#ffc840", CVAR_ARCHIVE);
  con_show_version = Cvar_Get("con_show_version", "1", CVAR_ARCHIVE);
  con_fade = Cvar_Get("con_fade", "0", CVAR_ARCHIVE);
  con_say_raw = Cvar_Get("con_say_raw", "0", CVAR_ARCHIVE);
  con_background_color->changed = con_color_changed;
  con_line_color->changed = con_color_changed;
  con_version_color->changed = con_color_changed;
  con_color_changed(con_background_color);
  con_color_changed(con_line_color);
  con_color_changed(con_version_color);
  con_screen_extents->changed = con_width_changed;
  con_history = Cvar_Get("con_history", STRINGIFY(HISTORY_SIZE), 0);
  con_timestamps = Cvar_Get("con_timestamps", "0", 0);
  con_timestamps->changed = con_width_changed;
  con_timestampsformat = Cvar_Get("con_timestampsformat", "%H:%M:%S ", 0);
  con_timestampsformat->changed = con_width_changed;
  con_timestampscolor = Cvar_Get("con_timestampscolor", "#aaa", 0);
  con_timestampscolor->changed = con_timestampscolor_changed;
  con_timestampscolor_changed(con_timestampscolor);
  con_auto_chat = Cvar_Get("con_auto_chat", "0", 0);
  ui_download_active = Cvar_Get("ui_download_active", "0", 0);

  IF_Init(&con.prompt.inputLine, 0, MAX_FIELD_TEXT - 1);
  IF_Init(&con.chatPrompt.inputLine, 0, MAX_FIELD_TEXT - 1);

  con.prompt.printf = Con_Printf;

  // use default width since no video is initialized yet
  r_config.width = 640;
  r_config.height = 480;
  con.linewidth = -1;
  con.scale = 1;
  con.color = COLOR_INDEX_NONE;
  con.newline = '\r';
  con.displayLine = (float)con.display;
  con.completionSnapshotCursor = SIZE_MAX;

  Con_CheckResize();

  con.initialized = true;
}

void Con_PostInit(void) {
  if (con_history->integer > 0) {
    Prompt_LoadHistory(&con.prompt, COM_HISTORYFILE_NAME);
  }
}

/*
================
Con_Shutdown
================
*/
void Con_Shutdown(void) {
  if (con_history->integer > 0) {
    Prompt_SaveHistory(&con.prompt, COM_HISTORYFILE_NAME, con_history->integer);
  }
  Prompt_Clear(&con.prompt);
}

static void Con_CarriageRet(void) {
  consoleLine_t *line = &con.text[con.current & CON_TOTALLINES_MASK];

  // add color from last line
  line->color = con.color;

  // add timestamp
  con.x = 0;
  if (con_timestamps->integer)
    con.x = Com_FormatLocalTime(line->text, con.linewidth,
                                con_timestampsformat->string);
  line->ts_len = con.x;

  // init text (must be after timestamp format which may overflow)
  memset(line->text + con.x, 0, CON_LINEWIDTH - con.x);

  // update time for transparent overlay
  if (!con.skipNotify)
    con.times[con.current & CON_TIMES_MASK] = cls.realtime;
}

static void Con_Linefeed(void) {
  if (con.display == con.current)
    con.display++;
  con.current++;

  Con_CarriageRet();

  if (con_scroll->integer & 2) {
    con.display = con.current;
  } else {
    Con_CheckTop();
  }

  // wrap to avoid integer overflow
  if (con.current >= CON_TOTALLINES * 2) {
    con.current -= CON_TOTALLINES;
    con.display -= CON_TOTALLINES;
    con.displayLine -= CON_TOTALLINES;
  }
}

static void con_color_changed(cvar_t *self) {
  color_t *target = NULL;
  color_t fallback = COLOR_WHITE;
  if (self == con_background_color) {
    target = &con.backgroundColor;
    fallback = COLOR_RGB(24, 0, 0);
  } else if (self == con_line_color) {
    target = &con.lineColor;
    fallback = COLOR_RGB(86, 108, 66);
  } else if (self == con_version_color) {
    target = &con.versionColor;
    fallback = COLOR_RGB(255, 200, 64);
  }
  if (!target)
    return;
  if (!SCR_ParseColor(self->string, target)) {
    Com_WPrintfLoc("$con_invalid_value", self->string, self->name);
    Cvar_Reset(self);
    *target = fallback;
  }
}

static int Con_VisibleTextRows(void) {
  int char_height = max(1, Con_FontCharHeight());
  int vislines = Q_clip(Q_rint(con.vidHeight * con.currentHeight), 0,
                        con.vidHeight);
  int separator_y = max(0, vislines - char_height - 2);
  int output_bottom_y = max(0, separator_y + 1 - 2 * char_height);
  return max(1, output_bottom_y / char_height + 1);
}

static int Con_GetScrollStep(bool page) {
  int page_lines = max(1, Con_VisibleTextRows() - 1);
  if (page)
    return page_lines;
  return min(page_lines, Cvar_ClampInteger(con_scroll_lines, 1, 256));
}

static void Con_UpdateDisplayLine(void) {
  float target = (float)con.display;
  if (!con_scroll_smooth->integer) {
    con.displayLine = target;
    return;
  }

  float oldest = (float)max(0, con.current - CON_TOTALLINES + 1);
  if (!isfinite(con.displayLine) || con.displayLine < oldest - 1.0f ||
      con.displayLine > (float)con.current + 1.0f) {
    con.displayLine = target;
    return;
  }

  CL_AdvanceValue(&con.displayLine, target,
                  Cvar_ClampValue(con_scroll_smooth_speed, 1.0f, 240.0f));
  if (fabsf(con.displayLine - target) < 0.01f)
    con.displayLine = target;
}

typedef struct {
  char text[MAX_FIELD_TEXT];
  int score;
} completionCandidate_t;

static int Con_CompareCompletionText(const void *a, const void *b) {
  const char *lhs = (const char *)a;
  const char *rhs = (const char *)b;
  return Q_strcasecmp(lhs, rhs);
}

static int Con_CompareCompletionCandidate(const void *a, const void *b) {
  const completionCandidate_t *lhs = (const completionCandidate_t *)a;
  const completionCandidate_t *rhs = (const completionCandidate_t *)b;
  if (lhs->score != rhs->score)
    return lhs->score - rhs->score;
  return Q_strcasecmp(lhs->text, rhs->text);
}

static int Con_CompletionEditDistance(const char *needle, const char *match) {
  int previous[MAX_FIELD_TEXT];
  int current[MAX_FIELD_TEXT];
  int needle_len = (int)min(strlen(needle), (size_t)MAX_FIELD_TEXT - 1);
  int match_len = (int)min(strlen(match), (size_t)MAX_FIELD_TEXT - 1);

  for (int j = 0; j <= match_len; ++j)
    previous[j] = j;
  for (int i = 1; i <= needle_len; ++i) {
    current[0] = i;
    for (int j = 1; j <= match_len; ++j) {
      int cost = Q_tolower((unsigned char)needle[i - 1]) ==
                         Q_tolower((unsigned char)match[j - 1])
                     ? 0
                     : 1;
      current[j] = min(min(previous[j] + 1, current[j - 1] + 1),
                       previous[j - 1] + cost);
    }
    memcpy(previous, current, (match_len + 1) * sizeof(previous[0]));
  }
  return previous[match_len];
}

static int Con_FuzzyCompletionScore(const char *needle, const char *match) {
  const char *substring = Q_stristr(match, needle);
  int length_penalty = abs((int)strlen(match) - (int)strlen(needle));
  if (substring)
    return 100 + (int)(substring - match) * 8 + length_penalty;

  int gaps = 0;
  int last = -1;
  int matched = 0;
  for (int i = 0; match[i] && needle[matched]; ++i) {
    if (Q_tolower((unsigned char)match[i]) ==
        Q_tolower((unsigned char)needle[matched])) {
      if (last >= 0)
        gaps += i - last - 1;
      last = i;
      matched++;
    }
  }
  if (!needle[matched])
    return 300 + gaps * 8 + length_penalty;

  int distance = Con_CompletionEditDistance(needle, match);
  int max_distance = strlen(needle) < 5 ? 1 : 2;
  if (distance <= max_distance)
    return 600 + distance * 32 + length_penalty;
  return INT_MAX;
}

static void Con_FreeCompletionMatches(genctx_t *ctx) {
  for (int i = 0; i < ctx->count; ++i)
    Z_Free(ctx->matches[i]);
  Z_Free(ctx->matches);
  ctx->matches = NULL;
  ctx->count = 0;
}

static void Con_GenerateCompletionMatches(genctx_t *ctx, int argnum) {
  if (argnum > 0) {
    Com_Generic_c(ctx, argnum);
  } else {
    Cmd_Command_g(ctx);
    Cvar_Variable_g(ctx);
    Cmd_Alias_g(ctx);
  }
}

static void Con_FindCompletionSegment(const char *text, size_t cursor,
                                      size_t *segment_start,
                                      size_t *segment_end) {
  size_t len = strlen(text);
  bool quoted = false;
  size_t start = 0;
  size_t end = len;

  cursor = min(cursor, len);
  for (size_t i = 0; i < cursor; ++i) {
    if (text[i] == '"')
      quoted = !quoted;
    else if (!quoted && text[i] == ';')
      start = i + 1;
  }
  quoted = false;
  for (size_t i = start; i < len; ++i) {
    if (text[i] == '"')
      quoted = !quoted;
    else if (!quoted && text[i] == ';' && i >= cursor) {
      end = i;
      break;
    }
  }
  while (start < end && (unsigned char)text[start] <= ' ')
    start++;
  *segment_start = start;
  *segment_end = end;
}

static void Con_DismissCompletionPopup(void) {
  con.completionPopupVisible = false;
  con.completionCount = 0;
  con.completionScroll = 0;
  con.completionSnapshotCursor = con.prompt.inputLine.cursorPos;
  Q_strlcpy(con.completionSnapshot, con.prompt.inputLine.text,
            sizeof(con.completionSnapshot));
  con.completionSnapshotValid = true;
}

static void Con_ClampCompletionScroll(void) {
  if (con.completionCount <= 0) {
    con.completionSelection = con.completionScroll = 0;
    return;
  }
  con.completionSelection =
      Q_clip(con.completionSelection, 0, con.completionCount - 1);
  int visible = min(CON_COMPLETION_MAX_VISIBLE, con.completionCount);
  int max_scroll = max(0, con.completionCount - visible);
  if (con.completionSelection < con.completionScroll)
    con.completionScroll = con.completionSelection;
  else if (con.completionSelection >= con.completionScroll + visible)
    con.completionScroll = con.completionSelection - visible + 1;
  con.completionScroll = Q_clip(con.completionScroll, 0, max_scroll);
}

static void Con_MoveCompletionSelection(int delta) {
  if (con.completionCount < 1 || !delta)
    return;
  con.completionSelection =
      (con.completionSelection + con.completionCount +
       (delta < 0 ? -1 : 1)) %
      con.completionCount;
  Con_ClampCompletionScroll();
}

static void Con_RefreshCompletionState(void) {
  inputField_t *field = &con.prompt.inputLine;
  size_t cursor = min(field->cursorPos, strlen(field->text));
  if (!con_completion_popup->integer || !field->text[0]) {
    Con_DismissCompletionPopup();
    return;
  }
  if (con.completionSnapshotValid &&
      con.completionSnapshotCursor == cursor &&
      !strcmp(con.completionSnapshot, field->text))
    return;

  char previous[MAX_FIELD_TEXT] = {0};
  if (con.completionSelection >= 0 &&
      con.completionSelection < con.completionCount)
    Q_strlcpy(previous, con.completionMatches[con.completionSelection],
              sizeof(previous));

  con.completionCount = 0;
  con.completionSelection = 0;
  con.completionScroll = 0;
  con.completionAppendSpace = false;
  con.completionPrependSlash = false;
  con.completionPopupVisible = false;

  size_t segment_start, segment_end;
  Con_FindCompletionSegment(field->text, cursor, &segment_start, &segment_end);
  size_t segment_len = min(segment_end - segment_start,
                           (size_t)MAX_FIELD_TEXT - 1);
  char segment[MAX_FIELD_TEXT];
  memcpy(segment, field->text + segment_start, segment_len);
  segment[segment_len] = 0;
  size_t relative_cursor = min(cursor - segment_start, segment_len);
  bool trailing_space = relative_cursor > 0 &&
                        (unsigned char)segment[relative_cursor - 1] <= ' ';

  Cmd_TokenizeString(segment, false);
  int full_argc = Cmd_Argc();
  int replace_arg = trailing_space || !full_argc
                        ? full_argc
                        : Cmd_FindArgForOffset((int)relative_cursor);
  if (replace_arg < 0 || replace_arg >= full_argc || trailing_space) {
    con.completionReplaceOffset = cursor;
    con.completionReplaceLength = 0;
  } else {
    size_t token_offset = (size_t)max(0, Cmd_ArgOffset(replace_arg));
    size_t token_length = strlen(Cmd_Argv(replace_arg));
    if (token_offset < segment_len && segment[token_offset] == '"')
      token_offset++;
    if (replace_arg == 0 && token_offset < segment_len &&
        (segment[token_offset] == '/' || segment[token_offset] == '\\')) {
      token_offset++;
      if (token_length)
        token_length--;
    }
    con.completionReplaceOffset = segment_start + token_offset;
    con.completionReplaceLength = min(token_length,
        strlen(field->text) - min(strlen(field->text),
                                  segment_start + token_offset));
  }

  size_t query_start = segment_start;
  while (query_start < cursor && (unsigned char)field->text[query_start] <= ' ')
    query_start++;
  if (query_start < cursor &&
      (field->text[query_start] == '/' || field->text[query_start] == '\\'))
    query_start++;
  size_t query_len = min(cursor - query_start, (size_t)MAX_FIELD_TEXT - 1);
  char query[MAX_FIELD_TEXT];
  memcpy(query, field->text + query_start, query_len);
  query[query_len] = 0;

  Cmd_TokenizeString(query, false);
  int argnum = Cmd_Argc() ? Cmd_Argc() - 1 : 0;
  if (query_len && (unsigned char)query[query_len - 1] <= ' ')
    argnum = Cmd_Argc();
  const char *partial = argnum < Cmd_Argc() ? Cmd_Argv(argnum) : "";
  if (argnum == 0 && !partial[0]) {
    con.completionSnapshotValid = true;
    con.completionSnapshotCursor = cursor;
    Q_strlcpy(con.completionSnapshot, field->text,
              sizeof(con.completionSnapshot));
    return;
  }

  genctx_t ctx = {};
  ctx.partial = partial;
  ctx.length = strlen(partial);
  ctx.argnum = argnum;
  ctx.size = CON_COMPLETION_MAX_MATCHES;
  ctx.ignorecase = true;
  Con_GenerateCompletionMatches(&ctx, argnum);
  for (int i = 0; i < ctx.count &&
                  con.completionCount < CON_COMPLETION_MAX_MATCHES; ++i) {
    Q_strlcpy(con.completionMatches[con.completionCount++], ctx.matches[i],
              MAX_FIELD_TEXT);
  }
  Con_FreeCompletionMatches(&ctx);

  if (!con.completionCount && argnum == 0 && strlen(partial) >= 2) {
    Cmd_TokenizeString(query, false);
    genctx_t all = {};
    all.partial = "";
    all.length = 0;
    all.argnum = 0;
    all.size = 4096;
    all.ignorecase = true;
    Con_GenerateCompletionMatches(&all, 0);
    completionCandidate_t candidates[CON_COMPLETION_MAX_MATCHES];
    int candidate_count = 0;
    for (int i = 0; i < all.count &&
                    candidate_count < CON_COMPLETION_MAX_MATCHES; ++i) {
      int score = Con_FuzzyCompletionScore(partial, all.matches[i]);
      if (score == INT_MAX)
        continue;
      Q_strlcpy(candidates[candidate_count].text, all.matches[i],
                sizeof(candidates[candidate_count].text));
      candidates[candidate_count++].score = score;
    }
    Con_FreeCompletionMatches(&all);
    qsort(candidates, candidate_count, sizeof(candidates[0]),
          Con_CompareCompletionCandidate);
    for (int i = 0; i < candidate_count; ++i)
      Q_strlcpy(con.completionMatches[con.completionCount++],
                candidates[i].text, MAX_FIELD_TEXT);
  } else if (con.completionCount > 1) {
    qsort(con.completionMatches, con.completionCount,
          sizeof(con.completionMatches[0]), Con_CompareCompletionText);
  }

  for (int i = 0; previous[0] && i < con.completionCount; ++i) {
    if (!Q_strcasecmp(previous, con.completionMatches[i])) {
      con.completionSelection = i;
      break;
    }
  }
  con.completionAppendSpace = con.completionCount == 1;
  con.completionPrependSlash = replace_arg == 0 &&
      con.completionReplaceOffset == segment_start && segment_start == 0 &&
      field->text[0] != '/' && field->text[0] != '\\';
  bool exact = con.completionCount == 1 &&
      con.completionReplaceLength == strlen(con.completionMatches[0]) &&
      !Q_strncasecmp(field->text + con.completionReplaceOffset,
                     con.completionMatches[0], con.completionReplaceLength);
  con.completionPopupVisible = con.completionCount > 0 && !exact;
  Con_ClampCompletionScroll();
  con.completionSnapshotValid = true;
  con.completionSnapshotCursor = cursor;
  Q_strlcpy(con.completionSnapshot, field->text,
            sizeof(con.completionSnapshot));
}

static void Con_ApplySelectedCompletion(int direction) {
  Con_RefreshCompletionState();
  if (con.completionCount < 1)
    return;

  inputField_t *field = &con.prompt.inputLine;
  if (direction && con.completionSelection < con.completionCount) {
    const char *current = field->text + con.completionReplaceOffset;
    const char *selected = con.completionMatches[con.completionSelection];
    if (con.completionReplaceLength == strlen(selected) &&
        !Q_strncasecmp(current, selected, con.completionReplaceLength))
      Con_MoveCompletionSelection(direction);
  }

  const char *match = con.completionMatches[con.completionSelection];
  bool quote = strchr(match, ' ') || strchr(match, ';');
  char replacement[MAX_FIELD_TEXT];
  Q_snprintf(replacement, sizeof(replacement), "%s%s%s%s",
             con.completionPrependSlash ? "/" : "", quote ? "\"" : "",
             match, quote ? "\"" : "");
  if (con.completionAppendSpace)
    Q_strlcat(replacement, " ", sizeof(replacement));

  char completed[MAX_FIELD_TEXT];
  size_t prefix = min(con.completionReplaceOffset, strlen(field->text));
  if (con.completionPrependSlash && prefix == 0) {
    completed[0] = 0;
  } else {
    memcpy(completed, field->text, prefix);
    completed[prefix] = 0;
  }
  Q_strlcat(completed, replacement, sizeof(completed));
  size_t cursor = strlen(completed);
  size_t suffix = min(strlen(field->text),
                      con.completionReplaceOffset +
                          con.completionReplaceLength);
  Q_strlcat(completed, field->text + suffix, sizeof(completed));
  IF_Replace(field, completed);
  IF_SetCursor(field, min(cursor, field->maxChars - 1), false);
  con.completionSnapshotValid = false;
  Con_RefreshCompletionState();
}

static void Con_Test_f(void) {
  int failures = 0;
  size_t start, end;
  const char *compound = "echo \"a;b\"; togglcon";
  Con_FindCompletionSegment(compound, strlen(compound), &start, &end);
  if (strcmp(compound + start, "togglcon") || end != strlen(compound)) {
    Com_WPrintf("con_test: quoted compound segment detection failed\n");
    failures++;
  }

  if (Con_CompletionEditDistance("togglconsole", "toggleconsole") != 1 ||
      Con_FuzzyCompletionScore("togglcon", "toggleconsole") == INT_MAX) {
    Com_WPrintf("con_test: fuzzy completion ranking failed\n");
    failures++;
  }

  IF_Replace(&con.prompt.inputLine, "/togglcon");
  con.completionSnapshotValid = false;
  Con_RefreshCompletionState();
  bool found_toggle = false;
  for (int i = 0; i < con.completionCount; ++i) {
    if (!Q_strcasecmp(con.completionMatches[i], "toggleconsole")) {
      found_toggle = true;
      break;
    }
  }
  if (!found_toggle) {
    Com_WPrintf("con_test: registered-command fuzzy completion failed\n");
    failures++;
  }

  int page = Con_GetScrollStep(true);
  int step = Con_GetScrollStep(false);
  if (page < 1 || step < 1 || step > page) {
    Com_WPrintf("con_test: scroll-step bounds failed\n");
    failures++;
  }

  bool visual = con_test_visual_request ||
      (Cmd_Argc() > 1 && !Q_strcasecmp(Cmd_Argv(1), "visual"));
  con_test_visual_request = false;
  if (visual) {
    IF_Replace(&con.prompt.inputLine, "/togglcon");
    con.completionSnapshotValid = false;
    Con_RefreshCompletionState();
    UI_CloseMenu();
    Key_SetDest(KEY_CONSOLE);
    con.mode = CON_DEFAULT;
    con.chat = CHAT_NONE;
    con.destHeight = con.currentHeight =
        Cvar_ClampValue(con_height, 0.1f, 1.0f);
  } else {
    IF_Clear(&con.prompt.inputLine);
    Con_DismissCompletionPopup();
  }
  if (failures)
    Com_WPrintf("con_test: %d failure%s\n", failures,
                failures == 1 ? "" : "s");
  else
    Com_Printf("con_test: all console integration checks passed\n");
}

static void Con_TestVisual_f(void) {
  con_test_visual_request = true;
  Con_Test_f();
}

void Con_SetColor(color_index_t color) { con.color = color; }

/*
=================
CL_LoadState
=================
*/
void CL_LoadState(load_state_t state) {
  con.loadstate = state;
  SCR_UpdateScreen();
  if (vid)
    vid->pump_events();
  S_Update();
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be displayed on screen
If no console is visible, the text will appear at the top of the game window
================
*/
void Con_Print(const char *txt) {
  char *p;
  int l;

  if (!con.initialized)
    return;

  while (*txt) {
    if (con.newline) {
      if (con.newline == '\n') {
        Con_Linefeed();
      } else {
        Con_CarriageRet();
      }
      con.newline = 0;
    }

    // count word length
    for (p = (char *)txt; *p > 32; p++)
      ;
    l = p - txt;

    // word wrap
    if (l < con.linewidth && con.x + l > con.linewidth) {
      Con_Linefeed();
    }

    switch (*txt) {
    case '\r':
    case '\n':
      con.newline = *txt;
      break;
    default: // display character and advance
      if (con.x == con.linewidth) {
        Con_Linefeed();
      }
      p = con.text[con.current & CON_TOTALLINES_MASK].text;
      p[con.x++] = *txt;
      break;
    }

    txt++;
  }

  // update time for transparent overlay
  if (!con.skipNotify)
    con.times[con.current & CON_TIMES_MASK] = cls.realtime;
}

/*
================
Con_Printf

Print text to graphical console only,
bypassing system console and logfiles
================
*/
void Con_Printf(const char *fmt, ...) {
  va_list argptr;
  char msg[MAXPRINTMSG];

  va_start(argptr, fmt);
  Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
  va_end(argptr);

  Con_Print(msg);
}

/*
================
Con_RegisterMedia
================
*/
void Con_RegisterMedia(void) {
  if (con.font) {
    Font_Free(con.font);
    con.font = nullptr;
  }
  con.font_pixel_scale = 0.0f;

  float pixel_scale = Con_GetFontPixelScale();
  int con_size = con_fontscale ? Cvar_ClampInteger(con_fontscale, 1, 64)
                               : CONCHAR_HEIGHT;
  if (con_fontscale && con_fontsize &&
      con_fontscale->default_string &&
      !Q_strcasecmp(con_fontscale->string, con_fontscale->default_string)) {
    int legacy_size = Cvar_ClampInteger(con_fontsize, 1, 64);
    if (legacy_size > 0)
      con_size = legacy_size;
  }
  int con_line_height = max(1, con_size);
  int con_fixed_advance = max(1, con_size);
  con.font =
      Font_Load(con_font->string, con_line_height, pixel_scale,
                con_fixed_advance, k_console_kfont_fallback, "conchars.png");
  if (!con.font && strcmp(con_font->string, con_font->default_string)) {
    Cvar_Reset(con_font);
    con.font =
        Font_Load(con_font->default_string, con_line_height, pixel_scale,
                  con_fixed_advance, k_console_kfont_fallback, "conchars.png");
  }
  con.font_pixel_scale = pixel_scale;
  con_font_settings_generation = Font_SettingsGeneration();
  con.charsetImage = Font_LegacyHandle(con.font);
  if (!con.font) {
    Com_Error(ERR_FATAL, "%s", Com_GetLastError());
  }

  if (con.font) {
    int char_width = Con_FontCharWidth();
    con.linewidth = Q_clip(con.displayWidth / char_width - 2, 0, CON_LINEWIDTH);
    con.prompt.inputLine.visibleChars = con.linewidth;
    con.prompt.widthInChars = con.linewidth;
    con.chatPrompt.inputLine.visibleChars = con.linewidth;

    if (con_timestamps->integer) {
      char temp[CON_LINEWIDTH];
      con.prompt.widthInChars -= Com_FormatLocalTime(
          temp, con.linewidth, con_timestampsformat->string);
    }
  }

  con.backImage = R_RegisterPic(con_background->string);
  if (!con.backImage) {
    if (strcmp(con_background->string, con_background->default_string)) {
      Cvar_Reset(con_background);
      con.backImage = R_RegisterPic(con_background->default_string);
    }
  }
  con.cursorImage = R_RegisterPic("/gfx/cursor.png");
}

void Con_RendererShutdown(void) {
  con.font = nullptr;
  con.font_pixel_scale = 0.0f;
  con_font_settings_generation = 0;
  con.charsetImage = 0;
  con.backImage = 0;
  con.cursorImage = 0;
}

/*
==============================================================================

DRAWING

==============================================================================
*/

static int Con_DrawLine(int v, int row, float alpha, bool notify) {
  const consoleLine_t *line = &con.text[row & CON_TOTALLINES_MASK];
  const char *s = line->text;
  int flags = 0;
  int x = con.displayX + Con_FontCharWidth();
  int w = con.linewidth;
  color_t color;

  if (notify) {
    s += line->ts_len;
  } else if (line->ts_len) {
    color = COLOR_SETA_U8(con.ts_color, static_cast<uint8_t>(alpha * 255.0f));
    x = Con_DrawString(x, v, 0, line->ts_len, s, color);
    s += line->ts_len;
    w -= line->ts_len;
  }
  if (w < 1)
    return x;

  switch (line->color) {
  case COLOR_INDEX_ALT:
    flags = UI_ALTCOLOR;
    // fall through
  case COLOR_INDEX_NONE:
    color = COLOR_WHITE;
    break;
  default:
    color = colorTable[line->color & 7];
    break;
  }
  color.a *= alpha;

  return Con_DrawString(x, v, flags, w, s, color);
}

const char *Con_GetChatPromptText(int *skip_chars) {
  const char *prompt = con.chat == CHAT_TEAM ? "say_team: " : "say: ";

  if (skip_chars)
    *skip_chars = (int)strlen(prompt);
  return prompt;
}

inputField_t *Con_GetChatInputField(void) { return &con.chatPrompt.inputLine; }

static size_t Con_InputClampChars(const char *text, size_t max_chars) {
  if (!text)
    return 0;
  size_t available = UTF8_CountChars(text, strlen(text));
  if (max_chars)
    available = min(available, max_chars);
  return available;
}

static size_t Con_InputCharsForWidth(const char *text, size_t max_chars,
                                     int pixel_width) {
  if (!text || pixel_width <= 0 || max_chars == 0)
    return 0;

  size_t available = Con_InputClampChars(text, max_chars);
  size_t chars = 0;
  while (chars < available) {
    size_t next_bytes = UTF8_OffsetForChars(text, chars + 1);
    int width = Con_MeasureString(text, next_bytes);
    if (width > pixel_width)
      break;
    chars++;
  }
  return chars;
}

static size_t Con_InputOffsetForWidth(const char *text, size_t cursor_chars,
                                      int pixel_width) {
  if (!text || pixel_width <= 0)
    return 0;

  size_t cursor_bytes = UTF8_OffsetForChars(text, cursor_chars);
  for (size_t start = 0; start < cursor_chars; ++start) {
    size_t start_bytes = UTF8_OffsetForChars(text, start);
    size_t len_bytes = cursor_bytes - start_bytes;
    int width = Con_MeasureString(text + start_bytes, len_bytes);
    if (width <= pixel_width)
      return start;
  }
  return cursor_chars;
}

static int Con_DrawInputField(const inputField_t *field, int x, int y,
                              int flags, size_t max_chars, color_t color) {
  if (!field)
    return 0;
  if (!field->maxChars || !field->visibleChars)
    return 0;
  if (!con.font)
    return IF_Draw(field, x, y, flags, con.charsetImage);

  int pixel_width = max(0, con.displayX + con.displayWidth - x);
  size_t total_chars = Con_InputClampChars(field->text, max_chars);
  size_t cursor_chars = UTF8_CountChars(field->text, field->cursorPos);
  if (cursor_chars > total_chars)
    cursor_chars = total_chars;
  size_t offset_chars = Con_InputOffsetForWidth(field->text, cursor_chars,
                                                pixel_width);
  if (offset_chars > total_chars)
    offset_chars = total_chars;

  size_t remaining_chars = (offset_chars < total_chars)
                               ? (total_chars - offset_chars)
                               : 0;
  size_t offset = UTF8_OffsetForChars(field->text, offset_chars);
  const char *text = field->text + offset;
  size_t draw_chars = Con_InputCharsForWidth(text, remaining_chars,
                                             pixel_width);
  size_t draw_len = UTF8_OffsetForChars(text, draw_chars);
  size_t cursor_chars_visible =
      (cursor_chars > offset_chars) ? (cursor_chars - offset_chars) : 0;
  if (cursor_chars_visible > draw_chars)
    cursor_chars_visible = draw_chars;
  size_t cursor_bytes = UTF8_OffsetForChars(text, cursor_chars_visible);

  if (field->selecting && field->selectionAnchor != field->cursorPos) {
    size_t sel_start = min(field->selectionAnchor, field->cursorPos);
    size_t sel_end = max(field->selectionAnchor, field->cursorPos);
    size_t sel_start_chars = UTF8_CountChars(field->text, sel_start);
    size_t sel_end_chars = UTF8_CountChars(field->text, sel_end);
    size_t sel_start_visible =
        (sel_start_chars > offset_chars) ? (sel_start_chars - offset_chars) : 0;
    size_t sel_end_visible =
        (sel_end_chars > offset_chars) ? (sel_end_chars - offset_chars) : 0;
    if (sel_start_visible > draw_chars)
      sel_start_visible = draw_chars;
    if (sel_end_visible > draw_chars)
      sel_end_visible = draw_chars;

    if (sel_end_visible > sel_start_visible) {
      size_t sel_start_bytes = UTF8_OffsetForChars(text, sel_start_visible);
      size_t sel_end_bytes = UTF8_OffsetForChars(text, sel_end_visible);
      int sel_start_x = x + Con_MeasureString(text, sel_start_bytes);
      int sel_end_x = x + Con_MeasureString(text, sel_end_bytes);
      int sel_w = max(0, sel_end_x - sel_start_x);
      color_t highlight = COLOR_RGBA(80, 120, 200, 120);
      int char_height = Con_FontCharHeight();
      R_DrawFill32(sel_start_x, y, sel_w, char_height, highlight);
    }
  }

  int end_x = Font_DrawString(con.font, x, y, 1, flags, draw_len, text, color);

  if ((flags & UI_DRAWCURSOR) && (com_localTime & BIT(8))) {
    int cursor_x = x + Con_MeasureString(text, cursor_bytes);
    int char_height = Con_FontCharHeight();
    size_t next_chars = min(draw_chars, cursor_chars_visible + 1);
    size_t next_bytes = UTF8_OffsetForChars(text, next_chars);
    int next_x = x + Con_MeasureString(text, next_bytes);
    int cursor_w = max(0, next_x - cursor_x);
    if (Key_GetOverstrikeMode()) {
      int width = max(2, cursor_w);
      if (width < 2)
        width = max(2, char_height / 2);
      color_t fill = COLOR_SETA_U8(color, 160);
      R_DrawFill32(cursor_x, y, width, char_height, fill);
    } else {
      color_t fill = COLOR_SETA_U8(color, 220);
      R_DrawFill32(cursor_x, y, 1, char_height, fill);
    }
  }

  return end_x;
}

static void Con_DrawCompletionPopup(int input_y) {
  Con_RefreshCompletionState();
  con.completionRectValid = false;
  con.completionScrollbarValid = false;
  if (!con.completionPopupVisible || con.completionCount < 1)
    return;

  int char_width = Con_FontCharWidth();
  int char_height = Con_FontCharHeight();
  int visible = min(CON_COMPLETION_MAX_VISIBLE,
                    con.completionCount - con.completionScroll);
  int width = 0;
  char row_text[MAX_FIELD_TEXT * 2];
  for (int i = 0; i < visible; ++i) {
    const char *match = con.completionMatches[con.completionScroll + i];
    cvar_t *var = Cvar_FindVar(match);
    if (var)
      Q_snprintf(row_text, sizeof(row_text), "%s = %s", match, var->string);
    else
      Q_strlcpy(row_text, match, sizeof(row_text));
    width = max(width, Con_MeasureString(row_text, MAX_STRING_CHARS));
  }

  int x = con.displayX + 2 * char_width;
  int padding = max(3, char_width / 3);
  int scrollbar = con.completionCount > visible ? max(3, char_width / 4) : 0;
  width = min(width + 2 * padding + scrollbar,
              max(char_width * 8,
                  con.displayX + con.displayWidth - x - char_width));
  int height = visible * char_height + 2 * padding;
  int y = max(0, input_y - height - 2);
  con.completionRectValid = true;
  con.completionX = x;
  con.completionY = y;
  con.completionW = width;
  con.completionH = height;
  con.completionRowY = y + padding;
  con.completionVisible = visible;
  R_DrawFill32(x, y, width, height, COLOR_RGBA(8, 10, 12, 238));
  R_DrawFill32(x, y, 1, height, COLOR_RGBA(110, 145, 88, 230));

  for (int i = 0; i < visible; ++i) {
    int index = con.completionScroll + i;
    int row_y = y + padding + i * char_height;
    const char *match = con.completionMatches[index];
    cvar_t *var = Cvar_FindVar(match);
    if (var)
      Q_snprintf(row_text, sizeof(row_text), "%s = %s", match, var->string);
    else
      Q_strlcpy(row_text, match, sizeof(row_text));
    if (index == con.completionSelection)
      R_DrawFill32(x + 1, row_y, width - 2, char_height,
                   COLOR_RGBA(70, 100, 58, 210));
    Con_DrawString(x + padding, row_y, 0, MAX_STRING_CHARS, row_text,
                   index == con.completionSelection
                       ? COLOR_WHITE
                       : COLOR_RGBA(205, 210, 205, 255));
  }

  if (scrollbar) {
    int track_x = x + width - scrollbar;
    int track_y = y + padding;
    int track_h = visible * char_height;
    int thumb_h = max(char_height / 2,
                      Q_rint((float)track_h * visible / con.completionCount));
    int travel = max(0, track_h - thumb_h);
    int max_scroll = max(1, con.completionCount - visible);
    int thumb_y = track_y +
                  Q_rint((float)travel * con.completionScroll / max_scroll);
    con.completionScrollbarValid = true;
    con.completionScrollbarX = track_x;
    con.completionScrollbarY = track_y;
    con.completionScrollbarW = scrollbar;
    con.completionScrollbarH = track_h;
    con.completionScrollbarThumbY = thumb_y;
    con.completionScrollbarThumbH = thumb_h;
    R_DrawFill32(track_x, track_y, scrollbar, track_h,
                 COLOR_RGBA(255, 255, 255, 45));
    R_DrawFill32(track_x, thumb_y, scrollbar, thumb_h,
                 COLOR_RGBA(255, 255, 255, 165));
  }
}

static void Con_ClampMouse(void) {
  con.mouseX = Q_clipf(con.mouseX, 0.0f, (float)max(0, con.vidWidth - 1));
  con.mouseY = Q_clipf(con.mouseY, 0.0f, (float)max(0, con.vidHeight - 1));
}

static size_t Con_InputCursorFromMouse(void) {
  inputField_t *field = &con.prompt.inputLine;
  int pixel_width = max(1, con.inputW);
  size_t cursor_chars = UTF8_CountChars(field->text, field->cursorPos);
  size_t offset_chars =
      Con_InputOffsetForWidth(field->text, cursor_chars, pixel_width);
  size_t total_chars = UTF8_CountChars(field->text, strlen(field->text));
  int relative_x = Q_rint(con.mouseX) - con.inputX;
  if (relative_x <= 0)
    return UTF8_OffsetForChars(field->text, offset_chars);

  for (size_t chars = offset_chars; chars < total_chars; ++chars) {
    size_t start = UTF8_OffsetForChars(field->text, offset_chars);
    size_t next = UTF8_OffsetForChars(field->text, chars + 1);
    int edge = Con_MeasureString(field->text + start, next - start);
    size_t current = UTF8_OffsetForChars(field->text, chars);
    int prior = Con_MeasureString(field->text + start, current - start);
    if (relative_x < prior + (edge - prior) / 2)
      return current;
  }
  return strlen(field->text);
}

static int Con_LogLineLength(int line) {
  if (line < 0 || line > con.current)
    return 0;
  return (int)strnlen(con.text[line & CON_TOTALLINES_MASK].text,
                      con.linewidth);
}

static void Con_ClampLogPosition(int *line, int *column) {
  int oldest = max(0, con.current - CON_TOTALLINES + 1);
  *line = Q_clip(*line, oldest, con.current);
  *column = Q_clip(*column, 0, Con_LogLineLength(*line));
}

static int Con_CompareLogPosition(int line_a, int column_a,
                                  int line_b, int column_b) {
  if (line_a != line_b)
    return line_a < line_b ? -1 : 1;
  return column_a == column_b ? 0 : (column_a < column_b ? -1 : 1);
}

static bool Con_HasLogSelection(void) {
  return Con_CompareLogPosition(con.logAnchorLine, con.logAnchorColumn,
                                con.logLine, con.logColumn) != 0;
}

static void Con_GetLogSelection(int *start_line, int *start_column,
                                int *end_line, int *end_column) {
  if (Con_CompareLogPosition(con.logAnchorLine, con.logAnchorColumn,
                             con.logLine, con.logColumn) <= 0) {
    *start_line = con.logAnchorLine;
    *start_column = con.logAnchorColumn;
    *end_line = con.logLine;
    *end_column = con.logColumn;
  } else {
    *start_line = con.logLine;
    *start_column = con.logColumn;
    *end_line = con.logAnchorLine;
    *end_column = con.logAnchorColumn;
  }
}

static bool Con_LogPositionFromMouse(int *line, int *column) {
  if (!con.logRectValid || con.mouseY < con.logTopY ||
      con.mouseY >= con.logBottomY + Con_FontCharHeight())
    return false;
  int char_height = max(1, Con_FontCharHeight());
  *line = Q_rint(con.logDisplayLine +
                 (con.mouseY - con.logBottomY) / char_height);
  int oldest = max(0, con.current - CON_TOTALLINES + 1);
  if (*line < oldest || *line > con.current)
    return false;

  const char *text = con.text[*line & CON_TOTALLINES_MASK].text;
  int len = Con_LogLineLength(*line);
  int relative_x = Q_rint(con.mouseX) - con.displayX - Con_FontCharWidth();
  if (relative_x <= 0) {
    *column = 0;
    return true;
  }
  size_t chars = UTF8_CountChars(text, len);
  for (size_t i = 0; i < chars; ++i) {
    size_t start = UTF8_OffsetForChars(text, i);
    size_t end = UTF8_OffsetForChars(text, i + 1);
    int left = Con_MeasureString(text, start);
    int right = Con_MeasureString(text, end);
    if (relative_x < left + (right - left) / 2) {
      *column = (int)start;
      return true;
    }
  }
  *column = len;
  return true;
}

static void Con_DrawLogSelectionRow(int line, int y) {
  if (!Con_HasLogSelection())
    return;
  int start_line, start_column, end_line, end_column;
  Con_GetLogSelection(&start_line, &start_column, &end_line, &end_column);
  if (line < start_line || line > end_line)
    return;
  const char *text = con.text[line & CON_TOTALLINES_MASK].text;
  int len = Con_LogLineLength(line);
  int start = line == start_line ? start_column : 0;
  int end = line == end_line ? end_column : len;
  start = Q_clip(start, 0, len);
  end = Q_clip(end, start, len);
  int left = con.displayX + Con_FontCharWidth() +
             Con_MeasureString(text, start);
  int right = con.displayX + Con_FontCharWidth() +
              Con_MeasureString(text, end);
  if (right > left)
    R_DrawFill32(left, y, right - left, Con_FontCharHeight(),
                 COLOR_RGBA(80, 120, 200, 120));
}

static size_t Con_BuildLogSelectionText(char *output, size_t capacity) {
  if (!Con_HasLogSelection() || !output || !capacity)
    return 0;
  int start_line, start_column, end_line, end_column;
  Con_GetLogSelection(&start_line, &start_column, &end_line, &end_column);
  size_t out = 0;
  for (int line = start_line; line <= end_line; ++line) {
    const char *text = con.text[line & CON_TOTALLINES_MASK].text;
    int len = Con_LogLineLength(line);
    int start = line == start_line ? start_column : 0;
    int end = line == end_line ? end_column : len;
    start = Q_clip(start, 0, len);
    end = Q_clip(end, start, len);
    while (end > start && (unsigned char)text[end - 1] <= ' ')
      end--;
    for (int i = start; i < end && out + 1 < capacity; ++i) {
      if (text[i] == '^' && i + 1 < end &&
          Com_IsColorEscapeCode(text[i + 1])) {
        i++;
        continue;
      }
      output[out++] = text[i];
    }
    if (line != end_line && out + 1 < capacity)
      output[out++] = '\n';
  }
  output[out] = 0;
  return out;
}

static void Con_CopyLogSelection(void) {
  if (!Con_HasLogSelection() || !vid || !vid->set_clipboard_data)
    return;
  int start_line, start_column, end_line, end_column;
  Con_GetLogSelection(&start_line, &start_column, &end_line, &end_column);
  size_t capacity = (size_t)(end_line - start_line + 1) *
                    (CON_LINEWIDTH + 1) + 1;
  char *output = (char *)Z_Malloc(capacity);
  Con_BuildLogSelectionText(output, capacity);
  vid->set_clipboard_data(output);
  Z_Free(output);
}

static void Con_SelectAllLog(void) {
  con.logAnchorLine = max(0, con.current - CON_TOTALLINES + 1);
  con.logAnchorColumn = 0;
  con.logLine = con.current;
  con.logColumn = Con_LogLineLength(con.current);
  con.logFocus = true;
}

static bool Con_LogPositionInSelection(int line, int column) {
  if (!Con_HasLogSelection())
    return false;
  int start_line, start_column, end_line, end_column;
  Con_GetLogSelection(&start_line, &start_column, &end_line, &end_column);
  return Con_CompareLogPosition(line, column, start_line, start_column) >= 0 &&
         Con_CompareLogPosition(line, column, end_line, end_column) <= 0;
}

static void Con_InsertInputText(const char *text, size_t cursor,
                                size_t delete_start, size_t delete_end) {
  inputField_t *field = &con.prompt.inputLine;
  size_t len = strlen(field->text);
  delete_start = min(delete_start, len);
  delete_end = Q_clip(delete_end, delete_start, len);
  cursor = min(cursor, len);
  if (cursor > delete_end)
    cursor -= delete_end - delete_start;
  else if (cursor >= delete_start)
    cursor = delete_start;

  char base[MAX_FIELD_TEXT];
  memcpy(base, field->text, delete_start);
  Q_strlcpy(base + delete_start, field->text + delete_end,
            sizeof(base) - delete_start);
  size_t base_len = strlen(base);
  cursor = min(cursor, base_len);

  char completed[MAX_FIELD_TEXT];
  memcpy(completed, base, cursor);
  completed[cursor] = 0;
  size_t available = field->maxChars > base_len
                         ? field->maxChars - base_len - 1
                         : 0;
  size_t insert_len = min(strlen(text), available);
  while (insert_len > 0 &&
         ((unsigned char)text[insert_len] & 0xc0) == 0x80)
    insert_len--;
  Q_strlcat(completed, va("%.*s", (int)insert_len, text),
            sizeof(completed));
  size_t new_cursor = strlen(completed);
  Q_strlcat(completed, base + cursor, sizeof(completed));
  IF_Replace(field, completed);
  IF_SetCursor(field, new_cursor, false);
  con.completionSnapshotValid = false;
}

static void Con_UpdateScrollbarDrag(void) {
  if (!con.scrollbarDragging || !con.scrollbarValid)
    return;
  int travel = con.scrollbarH - con.scrollbarThumbH;
  if (travel <= 0)
    return;
  float thumb_y = con.mouseY - con.scrollbarDragOffset;
  float frac = (thumb_y - con.scrollbarY) / travel;
  frac = Q_clipf(frac, 0.0f, 1.0f);
  int total_lines = min(CON_TOTALLINES, con.current + 1);
  int scrollable = max(0, total_lines - Con_VisibleTextRows());
  int min_display = con.current - scrollable;
  con.display = min_display + Q_rint(frac * scrollable);
  Con_CheckTop();
}

static void Con_UpdateCompletionScrollbarDrag(void) {
  if (!con.completionScrollbarDragging || !con.completionScrollbarValid)
    return;
  int travel = con.completionScrollbarH - con.completionScrollbarThumbH;
  int max_scroll = max(0, con.completionCount - con.completionVisible);
  if (travel <= 0 || max_scroll <= 0)
    return;
  float thumb_y = con.mouseY - con.completionScrollbarDragOffset;
  float frac = Q_clipf((thumb_y - con.completionScrollbarY) / travel,
                       0.0f, 1.0f);
  con.completionScroll = Q_rint(frac * max_scroll);
}

void Con_MouseEvent(int x, int y) {
  if (!(cls.key_dest & KEY_CONSOLE) || IN_MouseGrabbed())
    return;
  float sx = r_config.width > 0 ? (float)con.vidWidth / r_config.width : 1.0f;
  float sy = r_config.height > 0 ? (float)con.vidHeight / r_config.height : 1.0f;
  con.mouseX = x * sx;
  con.mouseY = y * sy;
  con.mouseInitialized = true;
  Con_ClampMouse();
  if (con.textDragPending && Key_IsDown(K_MOUSE1) &&
      (fabsf(con.mouseX - con.textDragStartX) >= 4.0f ||
       fabsf(con.mouseY - con.textDragStartY) >= 4.0f)) {
    con.textDragPending = false;
    con.textDragging = true;
  }
  if (con.textDragging)
    return;
  if (con.scrollbarDragging)
    Con_UpdateScrollbarDrag();
  if (con.completionScrollbarDragging)
    Con_UpdateCompletionScrollbarDrag();
  if (con.inputSelecting)
    IF_SetCursor(&con.prompt.inputLine, Con_InputCursorFromMouse(), true);
  if (con.logSelecting) {
    int line, column;
    if (Con_LogPositionFromMouse(&line, &column)) {
      con.logLine = line;
      con.logColumn = column;
      Con_ClampLogPosition(&con.logLine, &con.logColumn);
    }
  }
}

void Con_MouseMove(int dx, int dy) {
  if (!(cls.key_dest & KEY_CONSOLE))
    return;
  if (!con.mouseInitialized) {
    con.mouseX = con.vidWidth * 0.5f;
    con.mouseY = con.vidHeight * 0.5f;
    con.mouseInitialized = true;
  }
  con.mouseX += dx;
  con.mouseY += dy;
  Con_ClampMouse();
  if (con.textDragPending && Key_IsDown(K_MOUSE1) &&
      (fabsf(con.mouseX - con.textDragStartX) >= 4.0f ||
       fabsf(con.mouseY - con.textDragStartY) >= 4.0f)) {
    con.textDragPending = false;
    con.textDragging = true;
  }
  if (con.textDragging)
    return;
  if (con.scrollbarDragging)
    Con_UpdateScrollbarDrag();
  if (con.completionScrollbarDragging)
    Con_UpdateCompletionScrollbarDrag();
  if (con.inputSelecting)
    IF_SetCursor(&con.prompt.inputLine, Con_InputCursorFromMouse(), true);
  if (con.logSelecting) {
    int line, column;
    if (Con_LogPositionFromMouse(&line, &column)) {
      con.logLine = line;
      con.logColumn = column;
      Con_ClampLogPosition(&con.logLine, &con.logColumn);
    }
  }
}

void Con_MouseButton(bool down) {
  if (!(cls.key_dest & KEY_CONSOLE))
    return;
  if (!down) {
    if (con.textDragging && con.inputRectValid &&
        con.mouseX >= con.inputX && con.mouseX < con.inputX + con.inputW &&
        con.mouseY >= con.inputY && con.mouseY < con.inputY + con.inputH) {
      size_t target = Con_InputCursorFromMouse();
      if (con.textDragFromInput) {
        inputField_t *field = &con.prompt.inputLine;
        size_t length = con.textDragInputEnd - con.textDragInputStart;
        char dragged[MAX_FIELD_TEXT];
        memcpy(dragged, field->text + con.textDragInputStart, length);
        dragged[length] = 0;
        Con_InsertInputText(dragged, target, con.textDragInputStart,
                            con.textDragInputEnd);
      } else {
        int start_line, start_column, end_line, end_column;
        Con_GetLogSelection(&start_line, &start_column, &end_line, &end_column);
        size_t capacity = (size_t)(end_line - start_line + 1) *
                          (CON_LINEWIDTH + 1) + 1;
        char *dragged = (char *)Z_Malloc(capacity);
        Con_BuildLogSelectionText(dragged, capacity);
        Con_InsertInputText(dragged, target, 0, 0);
        Z_Free(dragged);
      }
    } else if (con.textDragPending) {
      if (con.textDragFromInput) {
        IF_SetCursor(&con.prompt.inputLine, Con_InputCursorFromMouse(), false);
      } else {
        int line, column;
        if (Con_LogPositionFromMouse(&line, &column)) {
          con.logAnchorLine = con.logLine = line;
          con.logAnchorColumn = con.logColumn = column;
        }
      }
    }
    con.textDragPending = false;
    con.textDragging = false;
    con.scrollbarDragging = false;
    con.completionScrollbarDragging = false;
    con.inputSelecting = false;
    con.logSelecting = false;
    return;
  }

  int mx = Q_rint(con.mouseX);
  int my = Q_rint(con.mouseY);
  if (con.completionScrollbarValid &&
      mx >= con.completionScrollbarX - 4 &&
      mx < con.completionScrollbarX + con.completionScrollbarW + 4 &&
      my >= con.completionScrollbarY &&
      my < con.completionScrollbarY + con.completionScrollbarH) {
    con.completionScrollbarDragging = true;
    if (my >= con.completionScrollbarThumbY &&
        my < con.completionScrollbarThumbY +
                 con.completionScrollbarThumbH) {
      con.completionScrollbarDragOffset =
          my - con.completionScrollbarThumbY;
    } else {
      con.completionScrollbarDragOffset =
          con.completionScrollbarThumbH * 0.5f;
      Con_UpdateCompletionScrollbarDrag();
    }
    return;
  }
  if (con.completionRectValid && mx >= con.completionX &&
      mx < con.completionX + con.completionW &&
      my >= con.completionRowY &&
      my < con.completionRowY +
               con.completionVisible * Con_FontCharHeight()) {
    int row = (my - con.completionRowY) / max(1, Con_FontCharHeight());
    int selection = con.completionScroll + row;
    if (selection >= 0 && selection < con.completionCount) {
      con.completionSelection = selection;
      Con_ApplySelectedCompletion(0);
      Con_DismissCompletionPopup();
    }
    return;
  }

  if (con.scrollbarValid && mx >= con.scrollbarX - 8 &&
      mx < con.scrollbarX + con.scrollbarW + 4 &&
      my >= con.scrollbarY && my < con.scrollbarY + con.scrollbarH) {
    con.scrollbarDragging = true;
    if (my >= con.scrollbarThumbY &&
        my < con.scrollbarThumbY + con.scrollbarThumbH) {
      con.scrollbarDragOffset = my - con.scrollbarThumbY;
    } else {
      con.scrollbarDragOffset = con.scrollbarThumbH * 0.5f;
      Con_UpdateScrollbarDrag();
    }
    return;
  }

  if (con.inputRectValid && mx >= con.inputX &&
      mx < con.inputX + con.inputW && my >= con.inputY &&
      my < con.inputY + con.inputH) {
    size_t input_cursor = Con_InputCursorFromMouse();
    inputField_t *field = &con.prompt.inputLine;
    if (!Key_IsDown(K_SHIFT) && field->selecting &&
        field->selectionAnchor != field->cursorPos) {
      size_t start = min(field->selectionAnchor, field->cursorPos);
      size_t end = max(field->selectionAnchor, field->cursorPos);
      if (input_cursor >= start && input_cursor <= end) {
        con.textDragPending = true;
        con.textDragFromInput = true;
        con.textDragStartX = con.mouseX;
        con.textDragStartY = con.mouseY;
        con.textDragInputStart = start;
        con.textDragInputEnd = end;
        return;
      }
    }
    IF_SetCursor(field, input_cursor,
                 Key_IsDown(K_SHIFT));
    con.inputSelecting = true;
    con.logSelecting = false;
    con.logFocus = false;
    return;
  }

  int line, column;
  if (Con_LogPositionFromMouse(&line, &column)) {
    if (!Key_IsDown(K_SHIFT) && Con_LogPositionInSelection(line, column)) {
      con.textDragPending = true;
      con.textDragFromInput = false;
      con.textDragStartX = con.mouseX;
      con.textDragStartY = con.mouseY;
      return;
    }
    if (!Key_IsDown(K_SHIFT) || !con.logFocus) {
      con.logAnchorLine = line;
      con.logAnchorColumn = column;
    }
    con.logLine = line;
    con.logColumn = column;
    con.logSelecting = true;
    con.inputSelecting = false;
    con.logFocus = true;
  }
}

static void Con_DrawMouseCursor(void) {
  if (!(cls.key_dest & KEY_CONSOLE) || !con.mouseInitialized)
    return;
  int size = max(12, Con_FontCharHeight());
  if (con.cursorImage) {
    R_DrawStretchPic(Q_rint(con.mouseX), Q_rint(con.mouseY), size, size,
                     COLOR_WHITE, con.cursorImage);
  } else {
    R_DrawFill32(Q_rint(con.mouseX), Q_rint(con.mouseY), 2, size,
                 COLOR_WHITE);
    R_DrawFill32(Q_rint(con.mouseX), Q_rint(con.mouseY), size, 2,
                 COLOR_WHITE);
  }
}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
static void Con_DrawSolidConsole(void) {
  int i, x, y;
  int rows;
  const char *text;
  int row;
  char buffer[CON_LINEWIDTH];
  int vislines;
  uint8_t bg_alpha;
  int widths[2];
  int char_width;
  int char_height;
  int bottom_line_y;
  int separator_y;
  int input_y;
  int output_bottom_y;
  int text_rows;
  int text_top_y;
  int text_bottom_y;
  int right_edge;
  float alpha_scale;

  con.scrollbarValid = false;
  con.inputRectValid = false;
  con.completionRectValid = false;
  con.logRectValid = false;

  vislines = con.vidHeight * con.currentHeight;
  if (vislines <= 0)
    return;

  if (vislines > con.vidHeight)
    vislines = con.vidHeight;

  char_width = Con_FontCharWidth();
  char_height = Con_FontCharHeight();
  right_edge = con.displayX + con.displayWidth;
  bottom_line_y = vislines - 1;
  separator_y = bottom_line_y - char_height - 1;
  if (separator_y < 0)
    separator_y = 0;
  input_y = separator_y + 1;
  output_bottom_y = input_y - 2 * char_height;
  if (output_bottom_y < 0)
    output_bottom_y = 0;

  // setup transparency
  alpha_scale = con_fade->integer
                    ? Q_clipf(con.currentHeight / 0.5f, 0.0f, 1.0f)
                    : 1.0f;
  color_t color = COLOR_SETA_F(COLOR_WHITE, alpha_scale);
  bg_alpha = Q_rint(Cvar_ClampValue(con_background_opacity, 0.0f, 1.0f) *
                    alpha_scale * 255.0f);

  // draw the background
  if (cls.state < ca_active || (cls.key_dest & KEY_MENU) || con_alpha->value) {
    bool flat = Cvar_ClampInteger(con_background_style, 0, 1) != 0;
    bool use_rerelease =
        (!con.backImage || !con_background || !con_background->string[0] ||
         !Q_strcasecmp(con_background->string, "conback"));
    if (flat) {
      int top = vislines - con.vidHeight;
      R_DrawFill32(con.displayX, top, con.displayWidth, con.vidHeight,
                   COLOR_SETA_U8(con.backgroundColor, bg_alpha));
      R_DrawFill32(con.displayX, separator_y, con.displayWidth, 1,
                   COLOR_SETA_F(con.lineColor, alpha_scale));
      R_DrawFill32(con.displayX, vislines - 1, con.displayWidth, 1,
                   COLOR_SETA_F(con.lineColor, alpha_scale));
    } else if (use_rerelease) {
      int top = vislines - con.vidHeight;
      int height = con.vidHeight;
      int split = (int)(height * 0.65f);
      color_t top_color = COLOR_SETA_U8(COLOR_RGB(18, 18, 18), bg_alpha);
      color_t bottom_color = COLOR_SETA_U8(COLOR_RGB(10, 10, 10), bg_alpha);
      color_t line_light = COLOR_SETA_U8(COLOR_RGB(56, 56, 56), bg_alpha);
      color_t line_dark = COLOR_SETA_U8(COLOR_RGB(32, 32, 32), bg_alpha);
      color_t line_accent = COLOR_SETA_F(con.lineColor, alpha_scale);

      R_DrawFill32(con.displayX, top, con.displayWidth, split, top_color);
      R_DrawFill32(con.displayX, top + split, con.displayWidth,
                   height - split, bottom_color);

      if (separator_y >= top && separator_y < vislines - 1) {
        R_DrawFill32(con.displayX, separator_y, con.displayWidth, 1,
                     line_light);
        if (separator_y + 1 < vislines - 1)
          R_DrawFill32(con.displayX, separator_y + 1, con.displayWidth, 1,
                       line_dark);
      }
      if (vislines > 0)
        R_DrawFill32(con.displayX, vislines - 1, con.displayWidth, 1,
                     line_accent);
    } else {
      color_t bg_color = COLOR_SETA_U8(COLOR_WHITE, bg_alpha);
      R_DrawKeepAspectPic(con.displayX, vislines - con.vidHeight,
                          con.displayWidth,
                          con.vidHeight, bg_color, con.backImage);
    }
  }

  // draw the text
  y = output_bottom_y;
  rows = y / char_height + 1; // rows of text to draw

  // draw arrows to show the buffer is backscrolled
  if (con.display != con.current) {
    for (i = 1; i < con.linewidth / 2; i += 4) {
      R_DrawStretchChar(con.displayX + i * char_width, y, char_width,
                        char_height, 0, '^',
                        COLOR_SETA_U8(con.lineColor, color.a), con.charsetImage);
    }

    y -= char_height;
    rows--;
  }

  text_rows = rows;
  text_bottom_y = y;
  text_top_y = text_bottom_y - (text_rows - 1) * char_height;
  if (text_top_y < 0)
    text_top_y = 0;
  if (text_rows > 0) {
    con.logRectValid = true;
    con.logTopY = text_top_y;
    con.logBottomY = text_bottom_y;
    con.logDisplayLine = con.displayLine;
  }

  // draw from the bottom up
  row = (int)ceilf(con.displayLine);
  y += Q_rint(((float)row - con.displayLine) * char_height);
  widths[0] = widths[1] = 0;
  for (i = 0; i < rows; i++) {
    if (row < 0)
      break;
    if (con.current - row > CON_TOTALLINES - 1)
      break; // past scrollback wrap point

    Con_DrawLogSelectionRow(row, y);
    x = Con_DrawLine(y, row, alpha_scale, false);
    if (i < 2) {
      widths[i] = x;
    }

    y -= char_height;
    row--;
  }

  if (text_rows > 0) {
    int total_lines = con.current + 1;
    if (total_lines > CON_TOTALLINES)
      total_lines = CON_TOTALLINES;
    if (total_lines < 1)
      total_lines = 1;

    int scrollable = total_lines - text_rows;
    if (scrollable > 0) {
      int track_top = text_top_y;
      int track_bottom = text_bottom_y + char_height;
      int track_h = track_bottom - track_top;
      bool scrollbar_hot = con.scrollbarDragging ||
          (con.mouseInitialized &&
           con.mouseX >= con.displayX + con.displayWidth - char_width &&
           con.mouseY >= track_top && con.mouseY < track_bottom);
      CL_AdvanceValue(&con.scrollbarHover, scrollbar_hot ? 1.0f : 0.0f,
                      8.0f);
      int bar_w = max(2, char_width / 6) +
                  Q_rint(con.scrollbarHover * max(2, char_width / 5));
      int bar_x = con.displayX + con.displayWidth - bar_w - 2;
      if (bar_x < 0)
        bar_x = con.displayX + con.displayWidth - bar_w;
      right_edge = bar_x - 2;
      if (right_edge < 0)
        right_edge = 0;

      int scroll_offset = con.current - con.display;
      if (scroll_offset < 0)
        scroll_offset = 0;
      if (scroll_offset > scrollable)
        scroll_offset = scrollable;

      int thumb_h = max(char_height / 2,
                        Q_rint((float)track_h * text_rows / total_lines));
      if (thumb_h > track_h)
        thumb_h = track_h;

      int thumb_y = track_top;
      if (scrollable > 0 && track_h > thumb_h) {
        float frac = 1.0f - ((float)scroll_offset / (float)scrollable);
        thumb_y = track_top + Q_rint(frac * (float)(track_h - thumb_h));
      }

      color_t track_color = COLOR_SETA_U8(COLOR_RGB(255, 255, 255), 60);
      color_t thumb_color = COLOR_SETA_U8(COLOR_RGB(255, 255, 255), 160);

      con.scrollbarValid = true;
      con.scrollbarX = bar_x;
      con.scrollbarY = track_top;
      con.scrollbarW = bar_w;
      con.scrollbarH = track_h;
      con.scrollbarThumbY = thumb_y;
      con.scrollbarThumbH = thumb_h;

      R_DrawFill32(bar_x, track_top, bar_w, track_h, track_color);
      R_DrawFill32(bar_x, thumb_y, bar_w, thumb_h,
                   scrollbar_hot
                       ? COLOR_SETA_U8(COLOR_WHITE, 210)
                       : thumb_color);
    }
  }

  // draw the download bar
  bool show_download_bar = true;
  if (ui_download_active && ui_download_active->integer)
    show_download_bar = false;
  if (cls.download.current && show_download_bar) {
    char pos[16], suf[32];
    int n, j;
    bool use_legacy_bar = !con.font || Font_IsLegacy(con.font);
    char bar_left = use_legacy_bar ? '\x80' : '[';
    char bar_fill = use_legacy_bar ? '\x81' : '=';
    char bar_right = use_legacy_bar ? '\x82' : ']';
    char bar_marker = use_legacy_bar ? '\x83' : '>';

    if ((text = strrchr(cls.download.current->path, '/')) != NULL)
      text++;
    else
      text = cls.download.current->path;

    Com_FormatSizeLong(pos, sizeof(pos), cls.download.position);
    n = 4 +
        Q_scnprintf(suf, sizeof(suf), " %d%% (%s)", cls.download.percent, pos);

    // figure out width
    x = con.linewidth;
    y = x - strlen(text) - n;
    i = x / 3;
    if (strlen(text) > i) {
      y = x - i - n - 3;
      memcpy(buffer, text, i);
      buffer[i] = 0;
      Q_strlcat(buffer, "...", sizeof(buffer));
    } else {
      Q_strlcpy(buffer, text, sizeof(buffer));
    }
    Q_strlcat(buffer, ": ", sizeof(buffer));
    i = strlen(buffer);
    buffer[i++] = bar_left;
    // where's the dot go?
    n = y * cls.download.percent / 100;
    for (j = 0; j < y; j++) {
      if (j == n) {
        buffer[i++] = bar_marker;
      } else {
        buffer[i++] = bar_fill;
      }
    }
    buffer[i++] = bar_right;
    buffer[i] = 0;

    Q_strlcat(buffer, suf, sizeof(buffer));

    // draw it
    y = output_bottom_y + char_height;
    Con_DrawString(con.displayX + char_width, y, 0, con.linewidth, buffer,
                   color);
  } else if (cls.state == ca_loading) {
    // draw loading state
    switch (con.loadstate) {
    case LOAD_MAP:
      text = cl.configstrings[cl.csr.models + 1];
      break;
    case LOAD_MODELS:
      text = "models";
      break;
    case LOAD_IMAGES:
      text = "images";
      break;
    case LOAD_CLIENTS:
      text = "clients";
      break;
    case LOAD_SOUNDS:
      text = "sounds";
      break;
    default:
      text = NULL;
      break;
    }

    if (text) {
      Q_snprintf(buffer, sizeof(buffer), "Loading %s...", text);

      // draw it
      y = output_bottom_y + char_height;
      Con_DrawString(con.displayX + char_width, y, 0, con.linewidth, buffer,
                     color);
    }
  }

  // draw the input prompt, user text, and cursor if desired
  x = 0;
  if (cls.key_dest & KEY_CONSOLE) {
    y = input_y;
    Con_DrawCompletionPopup(input_y);

    // draw command prompt
    i = con.mode == CON_REMOTE ? '#' : '>';
    if (con.font) {
      char prompt[2] = {(char)i, 0};
      Con_DrawString(con.displayX + char_width, y, 0, 1, prompt,
                     COLOR_SETA_U8(COLOR_YELLOW, color.a));
    } else {
      R_DrawStretchChar(con.displayX + char_width, y, char_width, char_height,
                        0, i, COLOR_SETA_U8(COLOR_YELLOW, color.a),
                        con.charsetImage);
    }

    // draw input line
    con.inputRectValid = true;
    con.inputX = con.displayX + 2 * char_width;
    con.inputY = y;
    con.inputW = max(0, con.displayX + con.displayWidth -
                            con.inputX - char_width);
    con.inputH = char_height;
    x = Con_DrawInputField(&con.prompt.inputLine, con.inputX, y,
                           UI_DRAWCURSOR, con.prompt.inputLine.visibleChars,
                           color);
  }

#define APP_VERSION APPLICATION " " VERSION
  int ver_width = (int)(sizeof(APP_VERSION) * char_width);

  y = output_bottom_y;
  row = 0;
  // shift version upwards to prevent overdraw
  if (widths[0] > right_edge - ver_width - char_width) {
    y -= char_height;
    row++;
  }

  // draw clock
  color_t info_color = COLOR_SETA_U8(con.versionColor, color.a);
  if (con_clock->integer) {
    const char *format = con_clock->integer == 2 ? "%I:%M %p" : "%H:%M";
    x = (Com_FormatLocalTime(buffer, sizeof(buffer), format) + 1) * char_width;
    if (widths[row] + x + char_width <= right_edge) {
      int clock_width = Con_MeasureString(buffer, MAX_STRING_CHARS);
      Con_DrawString(right_edge - clock_width, y, 0,
                     MAX_STRING_CHARS, buffer, info_color);
  }
  }

  // draw version
  if (con_show_version->integer &&
      (!row || widths[0] + ver_width + char_width <= right_edge)) {
    int version_width = Con_MeasureString(APP_VERSION, MAX_STRING_CHARS);
    Con_DrawString(right_edge - version_width, y, 0,
                   MAX_STRING_CHARS, APP_VERSION, info_color);
  }
  Con_DrawMouseCursor();
}

//=============================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole(void) {
  Con_UpdateDisplayLine();
  if (cls.key_dest & KEY_CONSOLE)
    Con_RefreshCompletionState();

  if (cls.disable_screen) {
    con.destHeight = con.currentHeight = 0;
    return;
  }

  if (!(cls.key_dest & KEY_MENU)) {
    if (cls.state == ca_disconnected) {
      // draw fullscreen console
      con.destHeight = con.currentHeight = 1;
      return;
    }
    if (cls.state > ca_disconnected && cls.state < ca_active) {
      // draw half-screen console
      con.destHeight = con.currentHeight = 0.5f;
      return;
    }
  }

  // decide on the height of the console
  if (cls.key_dest & KEY_CONSOLE) {
    con.destHeight = Cvar_ClampValue(con_height, 0.1f, 1);
  } else {
    con.destHeight = 0; // none visible
  }

  if (con_speed->value <= 0) {
    con.currentHeight = con.destHeight;
    return;
  }

  CL_AdvanceValue(&con.currentHeight, con.destHeight, con_speed->value);
}

/*
==================
SCR_DrawConsole
==================
*/
void Con_DrawConsole(void) {
  R_SetScale(con.scale);
  Con_DrawSolidConsole();
  R_SetScale(1.0f);
}

/*
==============================================================================

            LINE TYPING INTO THE CONSOLE AND COMMAND COMPLETION

==============================================================================
*/

static void Con_Say(const char *msg) {
  CL_ClientCommand(
      va("say%s \"%s\"", con.chat == CHAT_TEAM ? "_team" : "", msg));
}

// don't close console after connecting
static void Con_InteractiveMode(void) {
  if (con.mode == CON_POPUP) {
    con.mode = CON_DEFAULT;
  }
}

static void Con_Action(void) {
  const char *cmd = Prompt_Action(&con.prompt);

  Con_InteractiveMode();

  if (!cmd) {
    Con_Printf("]\n");
    return;
  }

  // backslash text are commands, else chat
  int backslash = cmd[0] == '\\' || cmd[0] == '/';

  if (con.mode == CON_REMOTE) {
    CL_SendRcon(&con.remoteAddress, con.remotePassword, cmd + backslash);
  } else {
    bool sent_raw_chat = false;
    if (!backslash && cls.state == ca_active && con_say_raw->integer &&
        (con_auto_chat->integer == CHAT_DEFAULT ||
         con_auto_chat->integer == CHAT_TEAM)) {
      CL_ClientCommand(va("say%s \"%s\"",
                          con_auto_chat->integer == CHAT_TEAM ? "_team" : "",
                          cmd));
      sent_raw_chat = true;
    } else if (!backslash && cls.state == ca_active) {
      switch (con_auto_chat->integer) {
      case CHAT_DEFAULT:
        Cbuf_AddText(&cmd_buffer, "cmd say ");
        break;
      case CHAT_TEAM:
        Cbuf_AddText(&cmd_buffer, "cmd say_team ");
        break;
      }
    }
    if (!sent_raw_chat) {
      Cbuf_AddText(&cmd_buffer, cmd + backslash);
      Cbuf_AddText(&cmd_buffer, "\n");
    }
  }

  Con_Printf("]%s\n", cmd);

  if (cls.state == ca_disconnected) {
    // force an update, because the command may take some time
    SCR_UpdateScreen();
  }
}

static void Con_Paste(char *(*func)(void)) {
  char *cbd, *s;

  Con_InteractiveMode();

  if (!func || !(cbd = func())) {
    return;
  }

  s = cbd;
  while (*s) {
    int c = *s++;
    switch (c) {
    case '\n':
      if (*s) {
        Con_Action();
      }
      break;
    case '\r':
    case '\t':
      IF_CharEvent(&con.prompt.inputLine, ' ');
      break;
    default:
      if (!Q_isprint(c)) {
        c = '?';
      }
      IF_CharEvent(&con.prompt.inputLine, c);
      break;
    }
  }

  Z_Free(cbd);
}

// console lines are not necessarily NUL-terminated
static void Con_ClearLine(char *buf, int row) {
  const consoleLine_t *line = &con.text[row & CON_TOTALLINES_MASK];
  const char *s = line->text + line->ts_len;
  int w = con.linewidth - line->ts_len;

  while (w-- > 0 && *s)
    *buf++ = *s++ & 127;
  *buf = 0;
}

static void Con_SearchUp(void) {
  char buf[CON_LINEWIDTH + 1];
  const char *s = con.prompt.inputLine.text;
  int top = con.current - CON_TOTALLINES + 1;

  if (top < 0)
    top = 0;

  if (!*s)
    return;

  for (int row = con.display - 1; row >= top; row--) {
    Con_ClearLine(buf, row);
    if (Q_stristr(buf, s)) {
      con.display = row;
      break;
    }
  }
}

static void Con_SearchDown(void) {
  char buf[CON_LINEWIDTH + 1];
  const char *s = con.prompt.inputLine.text;

  if (!*s)
    return;

  for (int row = con.display + 1; row <= con.current; row++) {
    Con_ClearLine(buf, row);
    if (Q_stristr(buf, s)) {
      con.display = row;
      break;
    }
  }
}

static bool Con_HandleLogSelectionKey(int key) {
  if (!con.logFocus || !Key_IsDown(K_CTRL) || !Key_IsDown(K_SHIFT))
    return false;

  int line = con.logLine;
  int column = con.logColumn;
  switch (key) {
  case K_LEFTARROW: {
    if (column > 0) {
      const char *text = con.text[line & CON_TOTALLINES_MASK].text;
      size_t chars = UTF8_CountChars(text, column);
      column = chars ? (int)UTF8_OffsetForChars(text, chars - 1) : 0;
    } else if (line > max(0, con.current - CON_TOTALLINES + 1)) {
      line--;
      column = Con_LogLineLength(line);
    }
    break;
  }
  case K_RIGHTARROW: {
    int len = Con_LogLineLength(line);
    if (column < len) {
      const char *text = con.text[line & CON_TOTALLINES_MASK].text;
      size_t chars = UTF8_CountChars(text, column);
      column = (int)UTF8_OffsetForChars(text, chars + 1);
    } else if (line < con.current) {
      line++;
      column = 0;
    }
    break;
  }
  case K_UPARROW:
    line--;
    break;
  case K_DOWNARROW:
    line++;
    break;
  case K_PGUP:
    line -= Con_GetScrollStep(true);
    break;
  case K_PGDN:
    line += Con_GetScrollStep(true);
    break;
  case K_HOME:
    line = max(0, con.current - CON_TOTALLINES + 1);
    column = 0;
    break;
  case K_END:
    line = con.current;
    column = Con_LogLineLength(line);
    break;
  default:
    return false;
  }

  Con_ClampLogPosition(&line, &column);
  con.logLine = line;
  con.logColumn = column;
  int rows = Con_VisibleTextRows();
  if (con.logLine > con.display)
    con.display = con.logLine;
  else if (con.logLine < con.display - rows + 1)
    con.display = con.logLine + rows - 1;
  Con_CheckTop();
  return true;
}

/*
====================
Key_Console

Interactive line editing and console scrollback
====================
*/
void Key_Console(int key) {
  if (key == 'c' && Key_IsDown(K_CTRL) && con.logFocus &&
      Con_HasLogSelection()) {
    Con_CopyLogSelection();
    return;
  }

  if (key == 'a' && Key_IsDown(K_CTRL) && con.logFocus) {
    Con_SelectAllLog();
    return;
  }

  if (Con_HandleLogSelectionKey(key))
    return;

  if (key == 'l' && Key_IsDown(K_CTRL)) {
    Con_Clear_f();
    return;
  }

  if (key == 'd' && Key_IsDown(K_CTRL)) {
    con.mode = CON_DEFAULT;
    return;
  }

  Con_RefreshCompletionState();
  if (con.completionPopupVisible) {
    switch (key) {
    case K_UPARROW:
    case K_KP_UPARROW:
    case K_MWHEELUP:
      Con_MoveCompletionSelection(-1);
      return;
    case K_DOWNARROW:
    case K_KP_DOWNARROW:
    case K_MWHEELDOWN:
      Con_MoveCompletionSelection(1);
      return;
    case K_PGUP:
      con.completionSelection -= CON_COMPLETION_MAX_VISIBLE;
      Con_ClampCompletionScroll();
      return;
    case K_PGDN:
      con.completionSelection += CON_COMPLETION_MAX_VISIBLE;
      Con_ClampCompletionScroll();
      return;
    case K_HOME:
      con.completionSelection = 0;
      Con_ClampCompletionScroll();
      return;
    case K_END:
      con.completionSelection = con.completionCount - 1;
      Con_ClampCompletionScroll();
      return;
    case K_ENTER:
    case K_KP_ENTER:
      Con_ApplySelectedCompletion(0);
      Con_DismissCompletionPopup();
      return;
    default:
      break;
    }
  }

  if (key == K_ENTER || key == K_KP_ENTER) {
    Con_Action();
    goto scroll;
  }

  if (key == 'v' && Key_IsDown(K_CTRL)) {
    if (vid)
      Con_Paste(vid->get_clipboard_data);
    goto scroll;
  }

  if ((key == K_INS && Key_IsDown(K_SHIFT)) || key == K_MOUSE3) {
    if (vid)
      Con_Paste(vid->get_selection_data);
    goto scroll;
  }

  if (key == K_TAB) {
    if (con_timestamps->integer)
      Con_CheckResize();
    if (con_completion_popup->integer)
      Con_ApplySelectedCompletion(Key_IsDown(K_SHIFT) ? -1 : 1);
    else
      Prompt_CompleteCommand(&con.prompt, true);
    goto scroll;
  }

  if (key == 'r' && Key_IsDown(K_CTRL)) {
    Prompt_CompleteHistory(&con.prompt, false);
    goto scroll;
  }

  if (key == 's' && Key_IsDown(K_CTRL)) {
    Prompt_CompleteHistory(&con.prompt, true);
    goto scroll;
  }

  if (key == K_UPARROW && Key_IsDown(K_CTRL)) {
    Con_SearchUp();
    return;
  }

  if (key == K_DOWNARROW && Key_IsDown(K_CTRL)) {
    Con_SearchDown();
    return;
  }

  if (key == K_UPARROW || (key == 'p' && Key_IsDown(K_CTRL))) {
    Prompt_HistoryUp(&con.prompt);
    goto scroll;
  }

  if (key == K_DOWNARROW || (key == 'n' && Key_IsDown(K_CTRL))) {
    Prompt_HistoryDown(&con.prompt);
    goto scroll;
  }

  if (key == K_PGUP || key == K_MWHEELUP) {
    con.display -= Con_GetScrollStep(Key_IsDown(K_CTRL));
    Con_CheckTop();
    return;
  }

  if (key == K_PGDN || key == K_MWHEELDOWN) {
    con.display += Con_GetScrollStep(Key_IsDown(K_CTRL));
    if (con.display > con.current) {
      con.display = con.current;
    }
    return;
  } else if (key == K_END) {
    con.display = con.current;
    return;
  }

  if (key == K_HOME && Key_IsDown(K_CTRL)) {
    con.display = 0;
    Con_CheckTop();
    return;
  }

  if (key == K_END && Key_IsDown(K_CTRL)) {
    con.display = con.current;
    return;
  }

  if (IF_KeyEvent(&con.prompt.inputLine, key)) {
    Prompt_ClearState(&con.prompt);
    Con_InteractiveMode();
    con.logFocus = false;
  }

scroll:
  if (con_scroll->integer & 1) {
    con.display = con.current;
  }
}

void Char_Console(int key) {
  if (IF_CharEvent(&con.prompt.inputLine, key)) {
    Con_InteractiveMode();
    con.logFocus = false;
  }
}

/*
====================
Key_Message
====================
*/
void Key_Message(int key) {
  if (key == 'l' && Key_IsDown(K_CTRL)) {
    IF_Clear(&con.chatPrompt.inputLine);
    return;
  }

  if (key == K_MWHEELUP) {
    SCR_NotifyScrollLines(1.0f);
    return;
  }

  if (key == K_MWHEELDOWN) {
    SCR_NotifyScrollLines(-1.0f);
    return;
  }

  if (key == K_MOUSE1) {
    SCR_NotifyMouseDown(key);
    return;
  }

  if (key == K_ENTER || key == K_KP_ENTER) {
    const char *cmd = Prompt_Action(&con.chatPrompt);

    if (cmd) {
      Con_Say(cmd);
    }
    Key_SetDest(static_cast<keydest_t>(cls.key_dest & ~KEY_MESSAGE));
    return;
  }

  if (key == K_ESCAPE) {
    Key_SetDest(static_cast<keydest_t>(cls.key_dest & ~KEY_MESSAGE));
    IF_Clear(&con.chatPrompt.inputLine);
    return;
  }

  if (key == 'r' && Key_IsDown(K_CTRL)) {
    Prompt_CompleteHistory(&con.chatPrompt, false);
    return;
  }

  if (key == 's' && Key_IsDown(K_CTRL)) {
    Prompt_CompleteHistory(&con.chatPrompt, true);
    return;
  }

  if (key == K_UPARROW || (key == 'p' && Key_IsDown(K_CTRL))) {
    Prompt_HistoryUp(&con.chatPrompt);
    return;
  }

  if (key == K_DOWNARROW || (key == 'n' && Key_IsDown(K_CTRL))) {
    Prompt_HistoryDown(&con.chatPrompt);
    return;
  }

  if (IF_KeyEvent(&con.chatPrompt.inputLine, key)) {
    Prompt_ClearState(&con.chatPrompt);
  }
}

void Char_Message(int key) { IF_CharEvent(&con.chatPrompt.inputLine, key); }


