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

#if UI_RML_HAS_RUNTIME

#include "common/common.h"
#include "common/files.h"
#include "client/keys.h"
#include "renderer/renderer.h"
#include "system/system.h"

#ifdef DotProduct
#undef DotProduct
#endif

#ifdef CrossProduct
#undef CrossProduct
#endif

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/URL.h>

static bool ui_rml_core_initialized;
static Rml::Context *ui_rml_context;
static Rml::ElementDocument *ui_rml_document;
static Rml::String ui_rml_active_route;
static Rml::String ui_rml_active_document;
static int ui_rml_context_width;
static int ui_rml_context_height;

class UI_Rml_SystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override
    {
        static const unsigned start_msec = Sys_Milliseconds();
        return (Sys_Milliseconds() - start_msec) / 1000.0;
    }

    int TranslateString(Rml::String &translated, const Rml::String &input) override
    {
        translated = input;
        return 0;
    }

    void JoinPath(Rml::String &translated_path,
                  const Rml::String &document_path,
                  const Rml::String &path) override
    {
        if (path.empty()) {
            translated_path.clear();
            return;
        }

        Rml::String normalized_path = Rml::StringUtilities::Replace(path, '\\', '/');

        if (normalized_path[0] == '/') {
            translated_path = normalized_path.substr(1);
            return;
        }

        const size_t drive_pos = normalized_path.find(':');
        const size_t slash_pos = normalized_path.find('/');
        if (drive_pos != Rml::String::npos &&
            (slash_pos == Rml::String::npos || drive_pos < slash_pos)) {
            translated_path = normalized_path;
            return;
        }

        Rml::String base = Rml::StringUtilities::Replace(document_path, '\\', '/');
        const size_t file_start = base.rfind('/');
        if (file_start != Rml::String::npos) {
            base.resize(file_start + 1);
        } else {
            base.clear();
        }

        const Rml::String url_path = Rml::StringUtilities::Replace(base + normalized_path, ':', '|');
        Rml::URL url(url_path);
        translated_path = Rml::StringUtilities::Replace(url.GetPathedFileName(), '|', ':');
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            Com_EPrintf("RmlUi: %s\n", message.c_str());
            return true;
        case Rml::Log::LT_WARNING:
            Com_WPrintf("RmlUi: %s\n", message.c_str());
            return true;
        default:
            Com_Printf("RmlUi: %s\n", message.c_str());
            return true;
        }
    }
};

class UI_Rml_CoreFileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String &path) override
    {
        qhandle_t handle = 0;
        const int64_t len = FS_OpenFile(path.c_str(), &handle, FS_MODE_READ);

        if (len < 0) {
            return 0;
        }

        return static_cast<Rml::FileHandle>(handle);
    }

    void Close(Rml::FileHandle file) override
    {
        if (file) {
            FS_CloseFile(static_cast<qhandle_t>(file));
        }
    }

    size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
    {
        const int len = FS_Read(buffer, size, static_cast<qhandle_t>(file));
        return len > 0 ? static_cast<size_t>(len) : 0;
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override
    {
        return FS_Seek(static_cast<qhandle_t>(file), offset, origin) == Q_ERR_SUCCESS;
    }

    size_t Tell(Rml::FileHandle file) override
    {
        const int64_t offset = FS_Tell(static_cast<qhandle_t>(file));
        return offset > 0 ? static_cast<size_t>(offset) : 0;
    }

    size_t Length(Rml::FileHandle file) override
    {
        const int64_t length = FS_Length(static_cast<qhandle_t>(file));
        return length > 0 ? static_cast<size_t>(length) : 0;
    }
};

struct UI_Rml_BitmapGlyph {
    char character;
    const char *rows[7];
};

static const UI_Rml_BitmapGlyph ui_rml_bitmap_glyphs[] = {
    {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
    {'C', {"01111", "10000", "10000", "10000", "10000", "10000", "01111"}},
    {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
    {'G', {"01111", "10000", "10000", "10111", "10001", "10001", "01111"}},
    {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'I', {"11111", "00100", "00100", "00100", "00100", "00100", "11111"}},
    {'J', {"00111", "00010", "00010", "00010", "10010", "10010", "01100"}},
    {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
    {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
    {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
    {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
    {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {'W', {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}},
    {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
    {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
    {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
    {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
    {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
    {'4', {"10010", "10010", "10010", "11111", "00010", "00010", "00010"}},
    {'5', {"11111", "10000", "10000", "11110", "00001", "00001", "11110"}},
    {'6', {"01110", "10000", "10000", "11110", "10001", "10001", "01110"}},
    {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {'9', {"01110", "10001", "10001", "01111", "00001", "00001", "01110"}},
    {'.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}},
    {',', {"00000", "00000", "00000", "00000", "01100", "01100", "01000"}},
    {':', {"00000", "01100", "01100", "00000", "01100", "01100", "00000"}},
    {';', {"00000", "01100", "01100", "00000", "01100", "01100", "01000"}},
    {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
    {'_', {"00000", "00000", "00000", "00000", "00000", "00000", "11111"}},
    {'/', {"00001", "00010", "00010", "00100", "01000", "01000", "10000"}},
    {'\\', {"10000", "01000", "01000", "00100", "00010", "00010", "00001"}},
    {'(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"}},
    {')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"}},
    {'[', {"01110", "01000", "01000", "01000", "01000", "01000", "01110"}},
    {']', {"01110", "00010", "00010", "00010", "00010", "00010", "01110"}},
    {'+', {"00000", "00100", "00100", "11111", "00100", "00100", "00000"}},
    {'=', {"00000", "00000", "11111", "00000", "11111", "00000", "00000"}},
    {'!', {"00100", "00100", "00100", "00100", "00100", "00000", "00100"}},
    {'?', {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}},
};

static const char *const ui_rml_missing_glyph[7] = {
    "11111", "10001", "00001", "00010", "00100", "00000", "00100",
};

static unsigned char UI_Rml_NormalizeBitmapGlyph(unsigned char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return static_cast<unsigned char>(ch - ('a' - 'A'));
    }

    return ch;
}

static const char *const *UI_Rml_FindBitmapGlyph(unsigned char ch)
{
    ch = UI_Rml_NormalizeBitmapGlyph(ch);

    for (const UI_Rml_BitmapGlyph &glyph : ui_rml_bitmap_glyphs) {
        if (static_cast<unsigned char>(glyph.character) == ch) {
            return glyph.rows;
        }
    }

    return ui_rml_missing_glyph;
}

static void UI_Rml_AddBitmapQuad(Rml::Mesh &mesh,
                                 float x0,
                                 float y0,
                                 float x1,
                                 float y1,
                                 Rml::ColourbPremultiplied colour)
{
    const int index = static_cast<int>(mesh.vertices.size());

    mesh.vertices.push_back(Rml::Vertex{{x0, y0}, colour, {0.0f, 0.0f}});
    mesh.vertices.push_back(Rml::Vertex{{x1, y0}, colour, {1.0f, 0.0f}});
    mesh.vertices.push_back(Rml::Vertex{{x1, y1}, colour, {1.0f, 1.0f}});
    mesh.vertices.push_back(Rml::Vertex{{x0, y1}, colour, {0.0f, 1.0f}});

    mesh.indices.push_back(index);
    mesh.indices.push_back(index + 1);
    mesh.indices.push_back(index + 2);
    mesh.indices.push_back(index);
    mesh.indices.push_back(index + 2);
    mesh.indices.push_back(index + 3);
}

class UI_Rml_SmokeFontEngineInterface final : public Rml::FontEngineInterface {
public:
    UI_Rml_SmokeFontEngineInterface()
    {
        SetMetrics(16);
    }

    bool LoadFontFace(const Rml::String &file_name,
                      int face_index,
                      bool fallback_face,
                      Rml::Style::FontWeight weight) override
    {
        (void)file_name;
        (void)face_index;
        (void)fallback_face;
        (void)weight;
        return true;
    }

    bool LoadFontFace(Rml::Span<const Rml::byte> data,
                      int face_index,
                      const Rml::String &family,
                      Rml::Style::FontStyle style,
                      Rml::Style::FontWeight weight,
                      bool fallback_face) override
    {
        (void)data;
        (void)face_index;
        (void)family;
        (void)style;
        (void)weight;
        (void)fallback_face;
        return true;
    }

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String &family,
                                          Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight,
                                          int size) override
    {
        (void)family;
        (void)style;
        (void)weight;
        SetMetrics(size > 0 ? size : 16);
        return 1;
    }

    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle,
                                              const Rml::FontEffectList &font_effects) override
    {
        (void)handle;
        (void)font_effects;
        return 1;
    }

    const Rml::FontMetrics &GetFontMetrics(Rml::FontFaceHandle handle) override
    {
        (void)handle;
        return metrics;
    }

    int GetStringWidth(Rml::FontFaceHandle handle,
                       Rml::StringView string,
                       const Rml::TextShapingContext &text_shaping_context,
                       Rml::Character prior_character = Rml::Character::Null) override
    {
        (void)handle;
        (void)text_shaping_context;
        (void)prior_character;
        return CalculateStringWidth(string);
    }

    int GenerateString(Rml::RenderManager &render_manager,
                       Rml::FontFaceHandle face_handle,
                       Rml::FontEffectsHandle font_effects_handle,
                       Rml::StringView string,
                       Rml::Vector2f position,
                       Rml::ColourbPremultiplied colour,
                       float opacity,
                       const Rml::TextShapingContext &text_shaping_context,
                       Rml::TexturedMeshList &mesh_list) override
    {
        (void)render_manager;
        (void)face_handle;
        (void)font_effects_handle;
        (void)text_shaping_context;

        float draw_opacity = opacity;
        if (draw_opacity < 0.0f) {
            draw_opacity = 0.0f;
        } else if (draw_opacity > 1.0f) {
            draw_opacity = 1.0f;
        }

        Rml::TexturedMesh text_mesh;
        const int scale = GlyphScale();
        const int advance = GlyphAdvance();
        const float top = position.y - metrics.ascent;
        const Rml::ColourbPremultiplied draw_colour = colour * draw_opacity;
        int cursor_x = 0;
        int glyph_count = 0;

        for (char raw_ch : string) {
            unsigned char ch = static_cast<unsigned char>(raw_ch);
            if (ch == '\r' || ch == '\n' || ch == '\t') {
                ch = ' ';
            }

            if (ch == ' ') {
                cursor_x += SpaceAdvance();
                continue;
            }

            const char *const *rows = UI_Rml_FindBitmapGlyph(ch);
            for (int row = 0; row < UI_RML_BITMAP_FONT_ROWS; ++row) {
                for (int column = 0; column < UI_RML_BITMAP_FONT_COLUMNS; ++column) {
                    if (rows[row][column] != '1') {
                        continue;
                    }

                    const float x0 = position.x + static_cast<float>(cursor_x + column * scale);
                    const float y0 = top + static_cast<float>(row * scale);
                    UI_Rml_AddBitmapQuad(
                        text_mesh.mesh,
                        x0,
                        y0,
                        x0 + static_cast<float>(scale),
                        y0 + static_cast<float>(scale),
                        draw_colour);
                }
            }

            ++glyph_count;
            cursor_x += advance;
        }

        if (text_mesh.mesh) {
            mesh_list.push_back(text_mesh);
        }

        if (glyph_count > 0) {
            ++generated_strings;
            generated_glyphs += glyph_count;

            if (!reported_geometry) {
                reported_geometry = true;
                Com_Printf(
                    "RmlUi smoke font engine generated glyph geometry: strings=%d glyphs=%d size=%d.\n",
                    generated_strings,
                    generated_glyphs,
                    metrics.size);
            }
        }

        return CalculateStringWidth(string);
    }

    int GetVersion(Rml::FontFaceHandle handle) override
    {
        (void)handle;
        return 2;
    }

private:
    static constexpr int UI_RML_BITMAP_FONT_COLUMNS = 5;
    static constexpr int UI_RML_BITMAP_FONT_ROWS = 7;

    int GlyphScale() const
    {
        return max(1, metrics.size / UI_RML_BITMAP_FONT_ROWS);
    }

    int GlyphAdvance() const
    {
        return UI_RML_BITMAP_FONT_COLUMNS * GlyphScale() + max(1, GlyphScale());
    }

    int SpaceAdvance() const
    {
        return max(1, GlyphAdvance() / 2);
    }

    int CalculateStringWidth(Rml::StringView string) const
    {
        int width = 0;

        for (char raw_ch : string) {
            unsigned char ch = static_cast<unsigned char>(raw_ch);
            if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
                width += SpaceAdvance();
                continue;
            }

            width += GlyphAdvance();
        }

        return width;
    }

    void SetMetrics(int size)
    {
        metrics.size = size;
        metrics.ascent = size * 0.8f;
        metrics.descent = size * 0.2f;
        metrics.line_spacing = size * 1.25f;
        metrics.x_height = size * 0.5f;
        metrics.underline_position = size * 0.08f;
        metrics.underline_thickness = max(1, size / 12);
        metrics.has_ellipsis = false;
    }

    Rml::FontMetrics metrics = {};
    int generated_strings = 0;
    int generated_glyphs = 0;
    bool reported_geometry = false;
};

static UI_Rml_SystemInterface ui_rml_system_interface;
static UI_Rml_CoreFileInterface ui_rml_file_interface;
static UI_Rml_SmokeFontEngineInterface ui_rml_font_interface;

static void UI_Rml_InstallCoreInterfaces(void)
{
    const ui_rml_renderer_interface_t *renderer = UI_Rml_RendererInterface();

    Rml::SetSystemInterface(&ui_rml_system_interface);
    Rml::SetFileInterface(&ui_rml_file_interface);
    Rml::SetFontEngineInterface(&ui_rml_font_interface);

    if (renderer && renderer->NativeRenderInterface) {
        void *native_render_interface = renderer->NativeRenderInterface();

        if (native_render_interface) {
            Rml::SetRenderInterface(
                static_cast<Rml::RenderInterface *>(native_render_interface));
        }
    }
}

static bool UI_Rml_CompiledRuntimeCanOpenRoutes(void)
{
    return UI_Rml_RendererIsAvailable();
}

static const char *UI_Rml_CompiledRuntimeName(void)
{
    static char runtime_name[64];
    const Rml::String version = Rml::GetVersion();

    Q_snprintf(runtime_name, sizeof(runtime_name), "RmlUi %s", version.c_str());
    return runtime_name;
}

static bool UI_Rml_CompiledRuntimeInit(void)
{
    if (ui_rml_core_initialized) {
        return true;
    }

    UI_Rml_InstallCoreInterfaces();

    if (!Rml::Initialise()) {
        Com_Printf("RmlUi core initialization failed; keeping legacy UI fallback active.\n");
        return false;
    }

    ui_rml_core_initialized = true;
    Com_Printf("RmlUi core initialized through %s; guarded route context rendering is available (renderer='%s', family='%s').\n",
               UI_Rml_CompiledRuntimeName(),
               UI_Rml_RendererName(),
               UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
    return true;
}

static Rml::Vector2i UI_Rml_CompiledRuntimeDimensions(int width, int height)
{
    if (width <= 0) {
        width = VIRTUAL_SCREEN_WIDTH;
    }

    if (height <= 0) {
        height = VIRTUAL_SCREEN_HEIGHT;
    }

    return Rml::Vector2i(width, height);
}

static bool UI_Rml_CompiledRuntimeEnsureContext(int width, int height)
{
    const Rml::Vector2i dimensions = UI_Rml_CompiledRuntimeDimensions(width, height);

    if (!ui_rml_core_initialized) {
        return false;
    }

    if (!ui_rml_context) {
        ui_rml_context = Rml::CreateContext("worr_ui", dimensions);

        if (!ui_rml_context) {
            Com_Printf("RmlUi failed to create the guarded route context; keeping legacy UI fallback active.\n");
            return false;
        }
    } else if (ui_rml_context_width != dimensions.x ||
               ui_rml_context_height != dimensions.y) {
        ui_rml_context->SetDimensions(dimensions);
    }

    ui_rml_context_width = dimensions.x;
    ui_rml_context_height = dimensions.y;
    return true;
}

static bool UI_Rml_CompiledRuntimeRouteIsAllowed(const char *route_id)
{
    static const char *const allowed_routes[] = {
        "core.runtime_smoke",
        "main",
        "game",
        "download_status",
    };

    if (!route_id || !route_id[0]) {
        return false;
    }

    for (const char *allowed_route : allowed_routes) {
        if (!strcmp(route_id, allowed_route)) {
            return true;
        }
    }

    return false;
}

static int UI_Rml_CompiledRuntimeModifiers(void)
{
    int modifiers = 0;

    if (Key_IsDown(K_CTRL)) {
        modifiers |= Rml::Input::KM_CTRL;
    }
    if (Key_IsDown(K_SHIFT)) {
        modifiers |= Rml::Input::KM_SHIFT;
    }
    if (Key_IsDown(K_ALT)) {
        modifiers |= Rml::Input::KM_ALT;
    }

    return modifiers;
}

static Rml::Input::KeyIdentifier UI_Rml_CompiledRuntimeKeyIdentifier(int key)
{
    if (key >= 'a' && key <= 'z') {
        key -= 'a' - 'A';
    }

    if (key >= 'A' && key <= 'Z') {
        return static_cast<Rml::Input::KeyIdentifier>(
            Rml::Input::KI_A + (key - 'A'));
    }

    if (key >= '0' && key <= '9') {
        return static_cast<Rml::Input::KeyIdentifier>(
            Rml::Input::KI_0 + (key - '0'));
    }

    switch (key) {
    case K_SPACE:
        return Rml::Input::KI_SPACE;
    case K_BACKSPACE:
        return Rml::Input::KI_BACK;
    case K_TAB:
        return Rml::Input::KI_TAB;
    case K_ENTER:
        return Rml::Input::KI_RETURN;
    case K_ESCAPE:
        return Rml::Input::KI_ESCAPE;
    case K_DEL:
        return Rml::Input::KI_DELETE;
    case K_UPARROW:
        return Rml::Input::KI_UP;
    case K_DOWNARROW:
        return Rml::Input::KI_DOWN;
    case K_LEFTARROW:
        return Rml::Input::KI_LEFT;
    case K_RIGHTARROW:
        return Rml::Input::KI_RIGHT;
    case K_INS:
        return Rml::Input::KI_INSERT;
    case K_HOME:
        return Rml::Input::KI_HOME;
    case K_END:
        return Rml::Input::KI_END;
    case K_PGUP:
        return Rml::Input::KI_PRIOR;
    case K_PGDN:
        return Rml::Input::KI_NEXT;
    case K_PAUSE:
        return Rml::Input::KI_PAUSE;
    case K_CAPSLOCK:
        return Rml::Input::KI_CAPITAL;
    case K_NUMLOCK:
        return Rml::Input::KI_NUMLOCK;
    case K_SCROLLOCK:
        return Rml::Input::KI_SCROLL;
    case K_SHIFT:
    case K_LSHIFT:
    case K_RSHIFT:
        return Rml::Input::KI_LSHIFT;
    case K_CTRL:
    case K_LCTRL:
    case K_RCTRL:
        return Rml::Input::KI_LCONTROL;
    case K_ALT:
    case K_LALT:
    case K_RALT:
        return Rml::Input::KI_LMENU;
    case K_F1:
    case K_F2:
    case K_F3:
    case K_F4:
    case K_F5:
    case K_F6:
    case K_F7:
    case K_F8:
    case K_F9:
    case K_F10:
    case K_F11:
    case K_F12:
        return static_cast<Rml::Input::KeyIdentifier>(
            Rml::Input::KI_F1 + (key - K_F1));
    case K_KP_ENTER:
        return Rml::Input::KI_NUMPADENTER;
    case K_KP_SLASH:
        return Rml::Input::KI_DIVIDE;
    case K_KP_MULTIPLY:
        return Rml::Input::KI_MULTIPLY;
    case K_KP_MINUS:
        return Rml::Input::KI_SUBTRACT;
    case K_KP_PLUS:
        return Rml::Input::KI_ADD;
    case K_KP_INS:
        return Rml::Input::KI_NUMPAD0;
    case K_KP_END:
        return Rml::Input::KI_NUMPAD1;
    case K_KP_DOWNARROW:
        return Rml::Input::KI_NUMPAD2;
    case K_KP_PGDN:
        return Rml::Input::KI_NUMPAD3;
    case K_KP_LEFTARROW:
        return Rml::Input::KI_NUMPAD4;
    case K_KP_5:
        return Rml::Input::KI_NUMPAD5;
    case K_KP_RIGHTARROW:
        return Rml::Input::KI_NUMPAD6;
    case K_KP_HOME:
        return Rml::Input::KI_NUMPAD7;
    case K_KP_UPARROW:
        return Rml::Input::KI_NUMPAD8;
    case K_KP_PGUP:
        return Rml::Input::KI_NUMPAD9;
    case K_KP_DEL:
        return Rml::Input::KI_DECIMAL;
    case ';':
    case ':':
        return Rml::Input::KI_OEM_1;
    case '=':
    case '+':
        return Rml::Input::KI_OEM_PLUS;
    case ',':
    case '<':
        return Rml::Input::KI_OEM_COMMA;
    case '-':
    case '_':
        return Rml::Input::KI_OEM_MINUS;
    case '.':
    case '>':
        return Rml::Input::KI_OEM_PERIOD;
    case '/':
    case '?':
        return Rml::Input::KI_OEM_2;
    case '`':
    case '~':
        return Rml::Input::KI_OEM_3;
    case '[':
    case '{':
        return Rml::Input::KI_OEM_4;
    case '\\':
    case '|':
        return Rml::Input::KI_OEM_5;
    case ']':
    case '}':
        return Rml::Input::KI_OEM_6;
    case '\'':
    case '"':
        return Rml::Input::KI_OEM_7;
    default:
        return Rml::Input::KI_UNKNOWN;
    }
}

static int UI_Rml_CompiledRuntimeMouseButtonIndex(int key)
{
    if (key >= K_MOUSE1 && key <= K_MOUSE8) {
        return key - K_MOUSE1;
    }

    return -1;
}

static void UI_Rml_CompiledRuntimeCloseRoute(void);

static bool UI_Rml_CompiledRuntimeProbeRoute(const char *route_id, const char *document_path)
{
    Rml::String contents;

    if (!document_path || !document_path[0]) {
        Com_Printf("RmlUi runtime file probe failed for route '%s': no document path.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!Rml::GetFileInterface() ||
        !Rml::GetFileInterface()->LoadFile(document_path, contents)) {
        Com_Printf("RmlUi runtime file probe failed for route '%s': %s.\n",
                   route_id ? route_id : "<null>",
                   document_path);
        return false;
    }

    Com_Printf("RmlUi runtime file probe OK: route '%s' loaded %s (%zu bytes) through WORR filesystem.\n",
               route_id ? route_id : "<null>",
               document_path,
               contents.size());
    return true;
}

static void UI_Rml_CompiledRuntimeShutdown(void)
{
    if (!ui_rml_core_initialized) {
        return;
    }

    UI_Rml_CompiledRuntimeCloseRoute();

    if (ui_rml_context) {
        Rml::RemoveContext("worr_ui");
        ui_rml_context = NULL;
        ui_rml_context_width = 0;
        ui_rml_context_height = 0;
    }

    Rml::Shutdown();
    ui_rml_core_initialized = false;
}

static bool UI_Rml_CompiledRuntimeOpenRoute(const char *route_id, const char *document_path)
{
    if (!UI_Rml_RendererIsAvailable()) {
        Com_Printf("RmlUi route '%s' resolved to '%s', but no native renderer bridge is available (renderer='%s', family='%s').\n",
                   route_id ? route_id : "<null>",
                   document_path ? document_path : "<null>",
                   UI_Rml_RendererName(),
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
        return false;
    }

    if (UI_Rml_RendererFamily() != UI_RML_RENDERER_FAMILY_OPENGL) {
        Com_Printf("RmlUi route '%s' resolved to '%s', but renderer family '%s' does not have a guarded native context path yet.\n",
                   route_id ? route_id : "<null>",
                   document_path ? document_path : "<null>",
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
        return false;
    }

    if (!UI_Rml_CompiledRuntimeRouteIsAllowed(route_id)) {
        Com_Printf("RmlUi route '%s' is outside the guarded sample-route allow-list.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!document_path || !document_path[0]) {
        Com_Printf("RmlUi route '%s' cannot open without a document path.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!UI_Rml_CompiledRuntimeEnsureContext(VIRTUAL_SCREEN_WIDTH, VIRTUAL_SCREEN_HEIGHT)) {
        return false;
    }

    UI_Rml_CompiledRuntimeCloseRoute();

    ui_rml_document = ui_rml_context->LoadDocument(document_path);
    if (!ui_rml_document) {
        Com_Printf("RmlUi route '%s' failed to load document '%s'.\n",
                   route_id ? route_id : "<null>",
                   document_path);
        return false;
    }

    ui_rml_document->Show();
    ui_rml_active_route = route_id ? route_id : "";
    ui_rml_active_document = document_path;
    ui_rml_context->Update();

    Com_Printf("RmlUi route '%s' opened document '%s' in guarded context '%s'.\n",
               ui_rml_active_route.c_str(),
               ui_rml_active_document.c_str(),
               "worr_ui");
    return true;
}

static void UI_Rml_CompiledRuntimeCloseRoute(void)
{
    if (ui_rml_document) {
        ui_rml_document->Close();
        ui_rml_document = NULL;
    } else if (ui_rml_context) {
        ui_rml_context->UnloadAllDocuments();
    }

    if (ui_rml_context) {
        ui_rml_context->Update();
    }

    ui_rml_active_route.clear();
    ui_rml_active_document.clear();
}

static bool UI_Rml_CompiledRuntimeUpdate(int width, int height, unsigned realtime)
{
    (void)realtime;

    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    if (!UI_Rml_CompiledRuntimeEnsureContext(width, height)) {
        return false;
    }

    (void)ui_rml_context->Update();
    return true;
}

static bool UI_Rml_CompiledRuntimeRender(void)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    return ui_rml_context->Render();
}

static bool UI_Rml_CompiledRuntimeKeyEvent(int key, bool down)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    const int modifiers = UI_Rml_CompiledRuntimeModifiers();
    const int button = UI_Rml_CompiledRuntimeMouseButtonIndex(key);
    if (button >= 0) {
        if (down) {
            (void)ui_rml_context->ProcessMouseButtonDown(button, modifiers);
        } else {
            (void)ui_rml_context->ProcessMouseButtonUp(button, modifiers);
        }
        return true;
    }

    if (down) {
        switch (key) {
        case K_MWHEELUP:
            (void)ui_rml_context->ProcessMouseWheel(Rml::Vector2f(0.0f, -1.0f), modifiers);
            return true;
        case K_MWHEELDOWN:
            (void)ui_rml_context->ProcessMouseWheel(Rml::Vector2f(0.0f, 1.0f), modifiers);
            return true;
        case K_MWHEELLEFT:
            (void)ui_rml_context->ProcessMouseWheel(Rml::Vector2f(-1.0f, 0.0f), modifiers);
            return true;
        case K_MWHEELRIGHT:
            (void)ui_rml_context->ProcessMouseWheel(Rml::Vector2f(1.0f, 0.0f), modifiers);
            return true;
        default:
            break;
        }
    }

    const Rml::Input::KeyIdentifier key_identifier =
        UI_Rml_CompiledRuntimeKeyIdentifier(key);
    if (key_identifier == Rml::Input::KI_UNKNOWN) {
        return false;
    }

    if (down) {
        (void)ui_rml_context->ProcessKeyDown(key_identifier, modifiers);
    } else {
        (void)ui_rml_context->ProcessKeyUp(key_identifier, modifiers);
    }

    return true;
}

static bool UI_Rml_CompiledRuntimeCharEvent(int key)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    if (key < 32 || (key >= 0x7f && key < 0xa0) || key > UNICODE_MAX ||
        (key >= 0xd800 && key <= 0xdfff)) {
        return false;
    }

    (void)ui_rml_context->ProcessTextInput(static_cast<Rml::Character>(key));
    return true;
}

static bool UI_Rml_CompiledRuntimeMouseEvent(int x, int y)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    (void)ui_rml_context->ProcessMouseMove(x, y, UI_Rml_CompiledRuntimeModifiers());
    return true;
}

void UI_Rml_RegisterCompiledRuntime(void)
{
    static const ui_rml_runtime_interface_t runtime = {
        UI_Rml_CompiledRuntimeInit,
        UI_Rml_CompiledRuntimeShutdown,
        UI_Rml_CompiledRuntimeOpenRoute,
        UI_Rml_CompiledRuntimeCloseRoute,
        UI_Rml_CompiledRuntimeUpdate,
        UI_Rml_CompiledRuntimeRender,
        UI_Rml_CompiledRuntimeKeyEvent,
        UI_Rml_CompiledRuntimeCharEvent,
        UI_Rml_CompiledRuntimeMouseEvent,
        UI_Rml_CompiledRuntimeProbeRoute,
        UI_Rml_CompiledRuntimeName,
        UI_Rml_CompiledRuntimeCanOpenRoutes,
    };

    UI_Rml_SetRuntimeInterface(&runtime);
}

#endif
