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

#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/loc.h"
#include "common/mapdb.h"
#include "common/net/net.h"
#include "../client.h"
#include "client/keys.h"
#include "client/sound/sound.h"
#include "renderer/renderer.h"
#include "server/server.h"
#include "system/system.h"
#include "../../game/sgame/monsters/m_player.hpp"

#ifdef DotProduct
#undef DotProduct
#endif

#ifdef CrossProduct
#undef CrossProduct
#endif

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/ScrollTypes.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/StyleTypes.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/URL.h>

#if USE_SDL3_TTF
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_ttf/SDL_ttf.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static bool ui_rml_core_initialized;
static Rml::Context *ui_rml_context;
static Rml::ElementDocument *ui_rml_document;
static Rml::String ui_rml_active_route;
static Rml::String ui_rml_active_document;
static int ui_rml_context_width;
static int ui_rml_context_height;
static int ui_rml_last_menu_sound = -1;
static unsigned ui_rml_last_menu_sound_msec;
static bool ui_rml_applying_cvar_bindings;
static cvar_t *ui_rml_log_missing_data_models;
static cvar_t *ui_rml_high_visibility;
static cvar_t *ui_rml_reduced_motion;
static cvar_t *ui_rml_large_text;
static cvar_t *ui_rml_localization_language;
static int ui_rml_localization_modified_count = -1;
static Rml::Element *ui_rml_keybind_capture_element;
static Rml::String ui_rml_keybind_capture_command;
static int ui_rml_keybind_capture_slot;
static unsigned ui_rml_keybind_capture_started;
static Rml::Element *ui_rml_keybind_conflict_element;
static Rml::String ui_rml_keybind_conflict_command;
static Rml::String ui_rml_keybind_conflict_previous_command;
static int ui_rml_keybind_conflict_key = -1;

static bool UI_Rml_IsMissingDataModelNotice(const Rml::String &message)
{
    return message.find("Could not locate data model") != Rml::String::npos;
}

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
        if (UI_Rml_IsMissingDataModelNotice(message) &&
            (!ui_rml_log_missing_data_models ||
             !ui_rml_log_missing_data_models->integer)) {
            return true;
        }

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

#if USE_SDL3_TTF
class UI_Rml_TtfFontEngineInterface final : public Rml::FontEngineInterface {
private:
    struct UI_Rml_TtfFace;
    struct UI_Rml_TtfSizedFace;
    struct UI_Rml_TtfFontCandidate {
        const char *path = nullptr;
        bool quake2_rerelease = false;
    };

public:
    void Initialize() override
    {
        if (ttf_initialized) {
            return;
        }

        if (!TTF_Init()) {
            Com_WPrintf("RmlUi TTF font engine could not initialize SDL3_ttf: %s\n",
                        SDL_GetError());
            return;
        }

        ttf_initialized = true;
        fallback_metrics = MetricsForSize(16);

        LoadDefaultFace("WORR Display", {
            { "fonts/RussoOne-Regular.ttf", true },
            { "fonts/Montserrat-Regular.ttf", true },
            { "fonts/NotoSansKR-Regular.otf", true },
            { "fonts/AtkinsonHyperLegible-Regular.otf", true },
            { "C:/Windows/Fonts/segoeui.ttf", false },
            { "C:/Windows/Fonts/arial.ttf", false },
        });

        LoadDefaultFace("WORR UI", {
            { "fonts/Montserrat-Regular.ttf", true },
            { "fonts/NotoSansKR-Regular.otf", true },
            { "fonts/NotoSansJP-Regular.otf", true },
            { "fonts/AtkinsonHyperLegible-Regular.otf", true },
            { "fonts/RussoOne-Regular.ttf", true },
            { "C:/Windows/Fonts/segoeui.ttf", false },
            { "C:/Windows/Fonts/arial.ttf", false },
            { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", false },
            { "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf", false },
            { "/System/Library/Fonts/Supplemental/Arial.ttf", false },
        });

        LoadDefaultFace("WORR Mono", {
            { "fonts/RobotoMono-Regular.ttf", true },
            { "fonts/RobotoMono-Medium.ttf", true },
            { "fonts/RobotoMono-Bold.ttf", true },
            { "C:/Windows/Fonts/consola.ttf", false },
            { "C:/Windows/Fonts/cour.ttf", false },
            { "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", false },
            { "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf", false },
            { "/System/Library/Fonts/Menlo.ttc", false },
        });

        if (faces.empty()) {
            Com_WPrintf("RmlUi TTF font engine found no usable UI fonts; menu text will be unavailable.\n");
        }
    }

    void Shutdown() override
    {
        ReleaseFontResources();
        faces.clear();

        if (ttf_initialized) {
            TTF_Quit();
            ttf_initialized = false;
        }
    }

    bool LoadFontFace(const Rml::String &file_name,
                      int face_index,
                      bool fallback_face,
                      Rml::Style::FontWeight weight) override
    {
        (void)face_index;

        std::vector<Rml::byte> data;
        if (!ReadFontFile(file_name, data)) {
            return false;
        }

        const Rml::String family = FontFamilyFromPath(file_name);
        return RegisterFontFace(std::move(data),
                                family.empty() ? Rml::String("WORR UI") : family,
                                Rml::Style::FontStyle::Normal,
                                weight,
                                fallback_face,
                                file_name);
    }

    bool LoadFontFace(Rml::Span<const Rml::byte> data,
                      int face_index,
                      const Rml::String &family,
                      Rml::Style::FontStyle style,
                      Rml::Style::FontWeight weight,
                      bool fallback_face) override
    {
        (void)face_index;

        if (data.empty()) {
            return false;
        }

        std::vector<Rml::byte> copy(data.data(), data.data() + data.size());
        return RegisterFontFace(std::move(copy),
                                family.empty() ? Rml::String("WORR UI") : family,
                                style,
                                weight,
                                fallback_face,
                                "memory");
    }

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String &family,
                                          Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight,
                                          int size) override
    {
        if (!ttf_initialized || faces.empty()) {
            return 0;
        }

        const size_t face_index = ResolveFaceIndex(family, style, weight);
        UI_Rml_TtfSizedFace *face = OpenSizedFace(face_index, size);
        return reinterpret_cast<Rml::FontFaceHandle>(face);
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
        const UI_Rml_TtfSizedFace *face = SizedFaceFromHandle(handle);
        return face ? face->metrics : fallback_metrics;
    }

    int GetStringWidth(Rml::FontFaceHandle handle,
                       Rml::StringView string,
                       const Rml::TextShapingContext &text_shaping_context,
                       Rml::Character prior_character = Rml::Character::Null) override
    {
        (void)text_shaping_context;
        (void)prior_character;

        UI_Rml_TtfSizedFace *face = SizedFaceFromHandle(handle);
        return face ? MeasureString(face, string) : 0;
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
        (void)font_effects_handle;
        (void)text_shaping_context;

        UI_Rml_TtfSizedFace *face = SizedFaceFromHandle(face_handle);
        if (!face || string.empty()) {
            return 0;
        }

        const std::string text = NormalizeText(string);
        const int measured_width = MeasureString(face, text);
        if (measured_width <= 0) {
            return 0;
        }

        float draw_opacity = opacity;
        if (draw_opacity < 0.0f) {
            draw_opacity = 0.0f;
        } else if (draw_opacity > 1.0f) {
            draw_opacity = 1.0f;
        }

        // Textures are colour-independent alpha masks, so identical strings
        // at the same face and size share one texture for the lifetime of
        // the current menu session (released via ReleaseFontResources).
        const std::string cache_key =
            std::to_string(face->face_index) + ":" +
            std::to_string(face->size) + ":" + text;
        auto cached = string_texture_cache.find(cache_key);
        if (cached != string_texture_cache.end()) {
            Rml::TexturedMesh cached_mesh;
            cached_mesh.texture = cached->second.texture;

            const float x0 = position.x;
            const float y0 = position.y - face->metrics.ascent;
            const float x1 =
                x0 + static_cast<float>(cached->second.dimensions.x) / face->pixel_scale;
            const float y1 =
                y0 + static_cast<float>(cached->second.dimensions.y) / face->pixel_scale;
            AddTexturedQuad(cached_mesh.mesh, x0, y0, x1, y1, colour * draw_opacity);
            mesh_list.push_back(std::move(cached_mesh));
            return measured_width;
        }

        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface *surface =
            TTF_RenderText_Blended(face->font, text.c_str(), text.size(), white);
        if (!surface) {
            return measured_width;
        }

        if (surface->format != SDL_PIXELFORMAT_ARGB8888) {
            SDL_Surface *converted =
                SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
            SDL_DestroySurface(surface);
            surface = converted;
        }

        if (!surface || surface->w <= 0 || surface->h <= 0 || !surface->pixels ||
            surface->pitch < 0 ||
            static_cast<size_t>(surface->pitch) <
                static_cast<size_t>(surface->w) * sizeof(uint32_t)) {
            if (surface) {
                SDL_DestroySurface(surface);
            }
            return measured_width;
        }

        const size_t width = static_cast<size_t>(surface->w);
        const size_t height = static_cast<size_t>(surface->h);
        if (height > (std::numeric_limits<size_t>::max)() / width / 4) {
            SDL_DestroySurface(surface);
            return measured_width;
        }

        auto pixels = std::make_shared<std::vector<Rml::byte>>(width * height * 4);
        for (size_t y = 0; y < height; ++y) {
            const auto *src = reinterpret_cast<const uint32_t *>(
                static_cast<const uint8_t *>(surface->pixels) + y * surface->pitch);
            Rml::byte *dst = pixels->data() + y * width * 4;

            for (size_t x = 0; x < width; ++x) {
                const Rml::byte alpha =
                    static_cast<Rml::byte>((src[x] >> 24) & 0xffu);
                dst[x * 4 + 0] = alpha;
                dst[x * 4 + 1] = alpha;
                dst[x * 4 + 2] = alpha;
                dst[x * 4 + 3] = alpha;
            }
        }

        const Rml::Vector2i texture_dimensions(surface->w, surface->h);
        SDL_DestroySurface(surface);

        Rml::CallbackTexture callback_texture =
            render_manager.MakeCallbackTexture(
                [pixels, texture_dimensions](
                    const Rml::CallbackTextureInterface &texture_interface) {
                    return texture_interface.GenerateTexture(
                        Rml::Span<const Rml::byte>(pixels->data(), pixels->size()),
                        texture_dimensions);
                });

        if (!callback_texture) {
            return measured_width;
        }

        Rml::TexturedMesh text_mesh;
        text_mesh.texture = callback_texture;

        const float x0 = position.x;
        const float y0 = position.y - face->metrics.ascent;
        const float x1 =
            x0 + static_cast<float>(texture_dimensions.x) / face->pixel_scale;
        const float y1 =
            y0 + static_cast<float>(texture_dimensions.y) / face->pixel_scale;
        AddTexturedQuad(text_mesh.mesh, x0, y0, x1, y1, colour * draw_opacity);

        string_texture_cache.emplace(
            cache_key,
            UI_Rml_CachedStringTexture{std::move(callback_texture), texture_dimensions});
        mesh_list.push_back(std::move(text_mesh));

        ++generated_strings;
        if (!reported_geometry) {
            reported_geometry = true;
            Com_Printf("RmlUi TTF font engine generated text texture: strings=%d size=%d source='%s'.\n",
                       generated_strings,
                       face->metrics.size,
                       faces[face->face_index].source.c_str());
        }

        return measured_width;
    }

    int GetVersion(Rml::FontFaceHandle handle) override
    {
        const UI_Rml_TtfSizedFace *face = SizedFaceFromHandle(handle);
        return face ? face->version : font_version;
    }

    void ReleaseFontResources() override
    {
        string_texture_cache.clear();
        sized_face_cache.clear();
        sized_faces.clear();
        ++font_version;
        reported_geometry = false;
        generated_strings = 0;
    }

    // Physical-pixel scale for glyph rasterization. Layout stays in canvas
    // units; glyphs render at canvas*scale so magnified framebuffers stay
    // sharp. Callers must invalidate via Rml::ReleaseFontResources() after
    // changing the scale.
    void SetPixelScale(float scale)
    {
        pixel_scale = scale >= 0.25f ? scale : 1.0f;
    }

    float GetPixelScale() const
    {
        return pixel_scale;
    }

private:
    struct UI_Rml_TtfFace {
        Rml::String family;
        Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal;
        Rml::Style::FontWeight weight = Rml::Style::FontWeight::Normal;
        bool fallback = false;
        std::vector<Rml::byte> data;
        Rml::String source;
    };

    struct UI_Rml_TtfSizedFace {
        ~UI_Rml_TtfSizedFace()
        {
            if (font) {
                TTF_CloseFont(font);
                font = nullptr;
            }
        }

        size_t face_index = 0;
        int size = 16;
        int version = 1;
        float pixel_scale = 1.0f;
        TTF_Font *font = nullptr;
        Rml::FontMetrics metrics = {};
    };

    struct UI_Rml_CachedStringTexture {
        Rml::CallbackTexture texture;
        Rml::Vector2i dimensions;
    };

    static Rml::FontMetrics MetricsForSize(int size)
    {
        if (size <= 0) {
            size = 16;
        }

        Rml::FontMetrics metrics = {};
        metrics.size = size;
        metrics.ascent = size * 0.8f;
        metrics.descent = size * 0.2f;
        metrics.line_spacing = size * 1.25f;
        metrics.x_height = size * 0.5f;
        metrics.underline_position = size * 0.08f;
        metrics.underline_thickness = max(1, size / 12);
        metrics.has_ellipsis = true;
        return metrics;
    }

    static Rml::FontMetrics MetricsForFont(TTF_Font *font, int size, float scale)
    {
        Rml::FontMetrics metrics = MetricsForSize(size);

        const int ascent = TTF_GetFontAscent(font);
        const int descent = max(0, -TTF_GetFontDescent(font));
        const int height = TTF_GetFontHeight(font);
        const int line_skip = TTF_GetFontLineSkip(font);
        int x_width = 0;
        int x_height = 0;

        if (scale <= 0.0f) {
            scale = 1.0f;
        }

        // TTF metrics are in physical pixels; convert back to canvas units.
        metrics.ascent = static_cast<float>(max(1, ascent)) / scale;
        metrics.descent = static_cast<float>(descent) / scale;
        metrics.line_spacing = static_cast<float>(
            max(max(1, line_skip), max(height, ascent + descent))) / scale;

        if (TTF_GetStringSize(font, "x", 1, &x_width, &x_height) && x_height > 0) {
            metrics.x_height = static_cast<float>(x_height) / scale;
        }

        return metrics;
    }

    static std::string NormalizeFamily(const Rml::String &family)
    {
        std::string key;
        key.reserve(family.size());

        for (char raw_ch : family) {
            unsigned char ch = static_cast<unsigned char>(raw_ch);
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<unsigned char>(ch - 'A' + 'a');
            }
            key.push_back(static_cast<char>(ch));
        }

        return key;
    }

    static std::string NormalizeText(Rml::StringView string)
    {
        std::string text;
        text.reserve(string.size());

        for (char ch : string) {
            if (ch == '\r' || ch == '\n' || ch == '\t') {
                text.push_back(' ');
            } else {
                text.push_back(ch);
            }
        }

        return text;
    }

    static Rml::String FontFamilyFromPath(const Rml::String &file_name)
    {
        Rml::String normalized = Rml::StringUtilities::Replace(file_name, '\\', '/');
        const size_t start = normalized.find_last_of('/');
        const size_t end = normalized.find_last_of('.');
        const size_t stem_start = start == Rml::String::npos ? 0 : start + 1;

        if (end == Rml::String::npos || end <= stem_start) {
            return normalized.substr(stem_start);
        }

        return normalized.substr(stem_start, end - stem_start);
    }

    static bool IsAbsoluteDiskPath(const Rml::String &path)
    {
        if (path.size() >= 2 && path[1] == ':') {
            return true;
        }

        return !path.empty() && (path[0] == '/' || path[0] == '\\');
    }

    static bool ReadFontFile(const Rml::String &path, std::vector<Rml::byte> &data)
    {
        data.clear();

        if (path.empty()) {
            return false;
        }

        if (!IsAbsoluteDiskPath(path)) {
            void *loaded = nullptr;
            const int len = FS_LoadFile(path.c_str(), &loaded);
            if (len > 0 && loaded) {
                const auto *begin = static_cast<const Rml::byte *>(loaded);
                data.assign(begin, begin + len);
                FS_FreeFile(loaded);
                return true;
            }
            if (loaded) {
                FS_FreeFile(loaded);
            }
        }

        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file) {
            return false;
        }

        file.seekg(0, std::ios::end);
        const std::streamoff len = file.tellg();
        if (len <= 0 ||
            static_cast<unsigned long long>(len) >
                (std::numeric_limits<size_t>::max)()) {
            return false;
        }

        file.seekg(0, std::ios::beg);
        data.resize(static_cast<size_t>(len));
        return static_cast<bool>(
            file.read(reinterpret_cast<char *>(data.data()), len));
    }

    static void AddTexturedQuad(Rml::Mesh &mesh,
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

    bool ValidateFontBytes(const std::vector<Rml::byte> &data) const
    {
        if (!ttf_initialized || data.empty()) {
            return false;
        }

        SDL_IOStream *io = SDL_IOFromConstMem(data.data(), data.size());
        if (!io) {
            return false;
        }

        TTF_Font *font = TTF_OpenFontIO(io, true, 16.0f);
        if (!font) {
            return false;
        }

        TTF_CloseFont(font);
        return true;
    }

    bool RegisterFontFace(std::vector<Rml::byte> data,
                          const Rml::String &family,
                          Rml::Style::FontStyle style,
                          Rml::Style::FontWeight weight,
                          bool fallback_face,
                          const Rml::String &source)
    {
        if (!ValidateFontBytes(data)) {
            return false;
        }

        UI_Rml_TtfFace face = {};
        face.family = family;
        face.style = style;
        face.weight = weight;
        face.fallback = fallback_face;
        face.data = std::move(data);
        face.source = source;
        faces.push_back(std::move(face));
        return true;
    }

    bool LoadDefaultFace(const Rml::String &family,
                         std::initializer_list<UI_Rml_TtfFontCandidate> candidates)
    {
        for (const UI_Rml_TtfFontCandidate &candidate : candidates) {
            std::vector<Rml::byte> data;
            if (!candidate.path || !ReadFontFile(candidate.path, data)) {
                continue;
            }

            Rml::String source = candidate.path;
            if (candidate.quake2_rerelease) {
                source = Rml::String("Quake II Rerelease: ") + candidate.path;
            }

            if (!RegisterFontFace(std::move(data),
                                  family,
                                  Rml::Style::FontStyle::Normal,
                                  Rml::Style::FontWeight::Normal,
                                  false,
                                  source)) {
                continue;
            }

            if (candidate.quake2_rerelease) {
                Com_Printf("RmlUi TTF font face '%s' loaded from Quake II Rerelease font %s.\n",
                           family.c_str(),
                           candidate.path);
            } else {
                Com_WPrintf("RmlUi TTF font face '%s' fell back to non-rerelease font %s.\n",
                            family.c_str(),
                            candidate.path);
            }
            return true;
        }

        Com_WPrintf("RmlUi TTF font face '%s' could not be loaded from Quake II Rerelease or fallback fonts.\n",
                    family.c_str());
        return false;
    }

    size_t ResolveFaceIndex(const Rml::String &family,
                            Rml::Style::FontStyle style,
                            Rml::Style::FontWeight weight) const
    {
        const std::string requested = NormalizeFamily(family);
        const bool wants_mono =
            requested.find("mono") != std::string::npos ||
            requested.find("console") != std::string::npos ||
            requested.find("courier") != std::string::npos;

        size_t fallback_index = 0;
        for (size_t i = 0; i < faces.size(); ++i) {
            const UI_Rml_TtfFace &face = faces[i];
            if (face.fallback) {
                fallback_index = i;
            }

            if (face.style == style && face.weight == weight &&
                NormalizeFamily(face.family) == requested) {
                return i;
            }
        }

        for (size_t i = 0; i < faces.size(); ++i) {
            const std::string available = NormalizeFamily(faces[i].family);
            if ((wants_mono && available.find("mono") != std::string::npos) ||
                (!wants_mono && available == "worr ui")) {
                return i;
            }
        }

        return fallback_index;
    }

    UI_Rml_TtfSizedFace *OpenSizedFace(size_t face_index, int size)
    {
        if (face_index >= faces.size()) {
            return nullptr;
        }

        size = max(1, size);
        float physical_size = static_cast<float>(size) * pixel_scale;
        if (physical_size > 256.0f) {
            physical_size = 256.0f;
        }
        if (physical_size < 1.0f) {
            physical_size = 1.0f;
        }

        // A pixel-scale change fully invalidates through
        // ReleaseFontResources, so face:size stays a sufficient key.
        const std::string key =
            std::to_string(face_index) + ":" + std::to_string(size);

        auto found = sized_face_cache.find(key);
        if (found != sized_face_cache.end()) {
            return found->second;
        }

        const UI_Rml_TtfFace &face = faces[face_index];
        SDL_IOStream *io = SDL_IOFromConstMem(face.data.data(), face.data.size());
        if (!io) {
            return nullptr;
        }

        TTF_Font *font = TTF_OpenFontIO(io, true, physical_size);
        if (!font) {
            return nullptr;
        }

        TTF_SetFontHinting(font, TTF_HINTING_LIGHT);

        const float effective_scale =
            physical_size / static_cast<float>(size);

        auto sized_face = std::make_unique<UI_Rml_TtfSizedFace>();
        sized_face->face_index = face_index;
        sized_face->size = size;
        sized_face->version = font_version;
        sized_face->pixel_scale = effective_scale;
        sized_face->font = font;
        sized_face->metrics = MetricsForFont(font, size, effective_scale);

        UI_Rml_TtfSizedFace *ptr = sized_face.get();
        sized_faces.push_back(std::move(sized_face));
        sized_face_cache.emplace(key, ptr);
        return ptr;
    }

    UI_Rml_TtfSizedFace *SizedFaceFromHandle(Rml::FontFaceHandle handle)
    {
        return reinterpret_cast<UI_Rml_TtfSizedFace *>(handle);
    }

    const UI_Rml_TtfSizedFace *SizedFaceFromHandle(Rml::FontFaceHandle handle) const
    {
        return reinterpret_cast<const UI_Rml_TtfSizedFace *>(handle);
    }

    int MeasureString(UI_Rml_TtfSizedFace *face, Rml::StringView string) const
    {
        return MeasureString(face, NormalizeText(string));
    }

    int MeasureString(UI_Rml_TtfSizedFace *face, const std::string &text) const
    {
        if (!face || !face->font || text.empty()) {
            return 0;
        }

        int width = 0;
        int height = 0;
        if (TTF_GetStringSize(face->font, text.c_str(), text.size(), &width, &height)) {
            // Physical pixels back to canvas units.
            return max(0, Q_rint(static_cast<float>(width) / face->pixel_scale));
        }

        return static_cast<int>(text.size()) * max(1, face->size / 2);
    }

    bool ttf_initialized = false;
    int font_version = 1;
    float pixel_scale = 1.0f;
    Rml::FontMetrics fallback_metrics = {};
    std::vector<UI_Rml_TtfFace> faces;
    std::vector<std::unique_ptr<UI_Rml_TtfSizedFace>> sized_faces;
    std::unordered_map<std::string, UI_Rml_TtfSizedFace *> sized_face_cache;
    std::unordered_map<std::string, UI_Rml_CachedStringTexture> string_texture_cache;
    int generated_strings = 0;
    bool reported_geometry = false;
};
#endif

static UI_Rml_SystemInterface ui_rml_system_interface;
static UI_Rml_CoreFileInterface ui_rml_file_interface;
#if USE_SDL3_TTF
static UI_Rml_TtfFontEngineInterface ui_rml_font_interface;
#else
static UI_Rml_SmokeFontEngineInterface ui_rml_font_interface;
#endif
static constexpr float UI_RML_REFERENCE_WIDTH = 960.0f;
static constexpr float UI_RML_REFERENCE_HEIGHT = 720.0f;

static bool UI_Rml_HandleServerBrowserAction(Rml::Element *element);
static bool UI_Rml_HandleDemoBrowserAction(Rml::Element *element);

static Rml::Element *UI_Rml_FindCommandElement(Rml::Element *element)
{
    for (Rml::Element *current = element; current; current = current->GetParentNode()) {
        if (!current->GetAttribute<Rml::String>("data-route-target", "").empty() ||
            !current->GetAttribute<Rml::String>("data-route", "").empty() ||
            !current->GetAttribute<Rml::String>("data-server-action", "").empty() ||
            !current->GetAttribute<Rml::String>("data-demo-action", "").empty() ||
            !current->GetAttribute<Rml::String>("data-command-cvar", "").empty() ||
            !current->GetAttribute<Rml::String>("data-command", "").empty()) {
            return current;
        }
    }

    return nullptr;
}

static bool UI_Rml_QueueCommand(const char *command)
{
    if (!command || !command[0]) {
        return false;
    }

    Cbuf_AddText(&cmd_buffer, command);
    Cbuf_AddText(&cmd_buffer, "\n");
    return true;
}

// Back/close commands must run before any already-buffered capture or config
// commands. Inserting the complete sequence also preserves the required
// runtime-pop-before-owner-cleanup ordering.
static bool UI_Rml_InsertCommandSequence(const char *first, const char *second)
{
    if (!first || !first[0]) {
        return false;
    }

    Rml::String sequence = first;
    sequence += "\n";
    if (second && second[0]) {
        sequence += second;
        sequence += "\n";
    }
    Cbuf_InsertText(&cmd_buffer, sequence.c_str());
    return true;
}

static bool UI_Rml_QueueRouteOpen(const Rml::String &route_id)
{
    if (route_id.empty() || !UI_Rml_DocumentForRoute(route_id.c_str())) {
        return false;
    }

    return UI_Rml_QueueCommand(va("ui_rml_runtime_open %s", route_id.c_str()));
}

static bool UI_Rml_QueueRoutePopup(const Rml::String &route_id)
{
    if (route_id.empty() || !UI_Rml_DocumentForRoute(route_id.c_str())) {
        return false;
    }

    return UI_Rml_QueueCommand(va("ui_rml_runtime_popup %s", route_id.c_str()));
}

static Rml::String UI_Rml_RouteTargetForElement(Rml::Element *element,
                                                const Rml::String &command)
{
    Rml::String route_target =
        element->GetAttribute<Rml::String>("data-route-target", "");

    if (!route_target.empty()) {
        return route_target;
    }

    if (command == "ui.open_route") {
        return element->GetAttribute<Rml::String>("data-route", "");
    }

    return "";
}

static Rml::String UI_Rml_CommandFirstArgumentAfterToken(const Rml::String &command,
                                                        const char *token)
{
    size_t position = strlen(token);

    while (position < command.size() &&
           (command[position] == ' ' || command[position] == '\t')) {
        position++;
    }

    if (position >= command.size() || command[position] == ';') {
        return "";
    }

    size_t end = position;
    while (end < command.size() &&
           command[end] != ' ' &&
           command[end] != '\t' &&
           command[end] != ';') {
        end++;
    }

    return command.substr(position, end - position);
}

static bool UI_Rml_CommandStartsWithToken(const Rml::String &command,
                                          const char *token)
{
    const size_t token_length = strlen(token);

    if (command.size() < token_length ||
        command.compare(0, token_length, token) != 0) {
        return false;
    }

    if (command.size() == token_length) {
        return true;
    }

    return command[token_length] == ';' ||
           command[token_length] == ' ' ||
           command[token_length] == '\t';
}

static const char *UI_Rml_CommandTailAfterToken(const Rml::String &command,
                                                const char *token)
{
    size_t position = strlen(token);

    while (position < command.size() &&
           (command[position] == ' ' || command[position] == '\t')) {
        position++;
    }

    if (position >= command.size() || command[position] != ';') {
        return nullptr;
    }

    position++;
    while (position < command.size() &&
           (command[position] == ' ' || command[position] == '\t')) {
        position++;
    }

    if (position >= command.size()) {
        return nullptr;
    }

    return command.c_str() + position;
}

static int UI_Rml_MenuSoundForName(const Rml::String &sound_name)
{
    if (sound_name.empty() ||
        !Q_stricmp(sound_name.c_str(), "none") ||
        !Q_stricmp(sound_name.c_str(), "silent")) {
        return -1;
    }

    if (!Q_stricmp(sound_name.c_str(), "open") ||
        !Q_stricmp(sound_name.c_str(), "in") ||
        !Q_stricmp(sound_name.c_str(), "activate")) {
        return UI_FEEDBACK_OPEN;
    }

    if (!Q_stricmp(sound_name.c_str(), "move") ||
        !Q_stricmp(sound_name.c_str(), "change")) {
        return UI_FEEDBACK_MOVE;
    }

    if (!Q_stricmp(sound_name.c_str(), "back") ||
        !Q_stricmp(sound_name.c_str(), "close") ||
        !Q_stricmp(sound_name.c_str(), "cancel") ||
        !Q_stricmp(sound_name.c_str(), "out")) {
        return UI_FEEDBACK_CLOSE;
    }

    if (!Q_stricmp(sound_name.c_str(), "alert") ||
        !Q_stricmp(sound_name.c_str(), "beep") ||
        !Q_stricmp(sound_name.c_str(), "confirm")) {
        return UI_FEEDBACK_ALERT;
    }

    return UI_FEEDBACK_OPEN;
}

static void UI_Rml_PlayMenuFeedbackSound(uiFeedbackSound_t sound)
{
    const unsigned now = Sys_Milliseconds();

    if (ui_rml_last_menu_sound == sound &&
        now - ui_rml_last_menu_sound_msec < 75) {
        return;
    }

    ui_rml_last_menu_sound = sound;
    ui_rml_last_menu_sound_msec = now;
    UI_StartFeedbackSound(sound);
}

static void UI_Rml_PlayElementMenuSound(Rml::Element *element,
                                        const Rml::String &command,
                                        const Rml::String &route_target)
{
    const Rml::String explicit_sound =
        element->GetAttribute<Rml::String>("data-menu-sound", "");
    int sound = -1;

    if (!explicit_sound.empty()) {
        sound = UI_Rml_MenuSoundForName(explicit_sound);
    } else if (command == "ui.close" ||
               command == "ui.back" ||
               UI_Rml_CommandStartsWithToken(command, "popmenu")) {
        sound = UI_FEEDBACK_CLOSE;
    } else if (command == "ui.popup" ||
               UI_Rml_CommandStartsWithToken(command, "pushpopup")) {
        sound = UI_FEEDBACK_ALERT;
    } else if (!route_target.empty() ||
               command == "ui.open_route" ||
               UI_Rml_CommandStartsWithToken(command, "pushmenu")) {
        sound = UI_FEEDBACK_OPEN;
    } else if (!command.empty()) {
        sound = UI_FEEDBACK_OPEN;
    }

    if (sound >= 0) {
        UI_Rml_PlayMenuFeedbackSound(static_cast<uiFeedbackSound_t>(sound));
    }
}

static Rml::Element *UI_Rml_DocumentBody(Rml::ElementDocument *document)
{
    if (!document) {
        return nullptr;
    }

    Rml::Element *body = document->QuerySelector("body");
    if (body) {
        return body;
    }

    return document->GetFirstChild();
}

static bool UI_Rml_ElementBoolAttribute(Rml::Element *element,
                                        const char *name)
{
    if (!element || !element->HasAttribute(name)) {
        return false;
    }

    const Rml::String value = element->GetAttribute<Rml::String>(name, "");
    if (value.empty() ||
        !Q_stricmp(value.c_str(), "1") ||
        !Q_stricmp(value.c_str(), "true") ||
        !Q_stricmp(value.c_str(), "yes") ||
        !Q_stricmp(value.c_str(), "on")) {
        return true;
    }

    return false;
}

static int UI_Rml_ElementIntAttribute(Rml::Element *element,
                                      const char *name,
                                      int fallback)
{
    if (!element || !element->HasAttribute(name)) {
        return fallback;
    }

    const Rml::String value = element->GetAttribute<Rml::String>(name, "");
    if (value.empty()) {
        return fallback;
    }

    return static_cast<int>(strtol(value.c_str(), nullptr, 10));
}

static Rml::String UI_Rml_EscapeTextForInnerRml(const char *text)
{
    Rml::String escaped;

    if (!text) {
        return escaped;
    }

    for (const char *cursor = text; *cursor; cursor++) {
        switch (*cursor) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        default:
            escaped += *cursor;
            break;
        }
    }

    return escaped;
}

static void UI_Rml_SetElementInnerText(Rml::Element *element,
                                       const char *text)
{
    if (!element) {
        return;
    }

    const Rml::String escaped = UI_Rml_EscapeTextForInnerRml(text);
    if (element->GetInnerRML() != escaped) {
        element->SetInnerRML(escaped);
    }
}

static bool UI_Rml_ParseDouble(const Rml::String &value, double *out);

static Rml::String UI_Rml_FormatCvarDisplayText(cvar_t *var)
{
    Rml::String text = (var && var->string) ? var->string : "";
    double parsed = 0.0;
    // Only trim trailing zeros when the whole string is one number, so
    // multi-dot values (IP addresses, version strings) pass through intact.
    const bool numeric =
        UI_Rml_ParseDouble(text, &parsed) &&
        text.find('.') != Rml::String::npos;

    if (!numeric) {
        return text;
    }

    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }

    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }

    return text.empty() ? "0" : text;
}

static bool UI_Rml_CvarHasDisplayText(cvar_t *var)
{
    return var && var->string && var->string[0] != '\0';
}

static Rml::String UI_Rml_TrimString(Rml::String value)
{
    size_t start = 0;
    while (start < value.size() &&
           static_cast<unsigned char>(value[start]) <= ' ') {
        start++;
    }

    size_t end = value.size();
    while (end > start &&
           static_cast<unsigned char>(value[end - 1]) <= ' ') {
        end--;
    }

    return value.substr(start, end - start);
}

static bool UI_Rml_ParseDouble(const Rml::String &value, double *out)
{
    if (value.empty()) {
        return false;
    }

    char *end = nullptr;
    const double parsed = strtod(value.c_str(), &end);
    if (!end || *end != '\0') {
        return false;
    }

    if (out) {
        *out = parsed;
    }
    return true;
}

static bool UI_Rml_CvarStringTruthy(const char *value)
{
    if (!value || !value[0]) {
        return false;
    }

    if (!Q_stricmp(value, "0") ||
        !Q_stricmp(value, "false") ||
        !Q_stricmp(value, "no") ||
        !Q_stricmp(value, "off")) {
        return false;
    }

    return true;
}

static bool UI_Rml_ConditionNameIsValid(const Rml::String &name)
{
    if (name.empty()) {
        return false;
    }

    for (char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!Q_isalnum(c) && c != '_' && c != '.' && c != '-') {
            return false;
        }
    }

    return true;
}

typedef enum {
    UI_RML_CONDITION_OP_EQUAL,
    UI_RML_CONDITION_OP_NOT_EQUAL,
    UI_RML_CONDITION_OP_GREATER,
    UI_RML_CONDITION_OP_GREATER_EQUAL,
    UI_RML_CONDITION_OP_LESS,
    UI_RML_CONDITION_OP_LESS_EQUAL
} ui_rml_condition_op_t;

static bool UI_Rml_ConditionTermMatches(Rml::String term)
{
    term = UI_Rml_TrimString(term);
    if (term.empty()) {
        return true;
    }

    bool negate = false;
    if (term[0] == '!') {
        negate = true;
        term = UI_Rml_TrimString(term.substr(1));
    }

    ui_rml_condition_op_t op = UI_RML_CONDITION_OP_EQUAL;
    size_t operator_pos = Rml::String::npos;
    size_t operator_len = 0;

    static const struct {
        const char *token;
        ui_rml_condition_op_t op;
    } operators[] = {
        { "!=", UI_RML_CONDITION_OP_NOT_EQUAL },
        { ">=", UI_RML_CONDITION_OP_GREATER_EQUAL },
        { "<=", UI_RML_CONDITION_OP_LESS_EQUAL },
        { ">", UI_RML_CONDITION_OP_GREATER },
        { "<", UI_RML_CONDITION_OP_LESS },
        { "=", UI_RML_CONDITION_OP_EQUAL },
    };

    for (const auto &candidate : operators) {
        const size_t pos = term.find(candidate.token);
        if (pos != Rml::String::npos &&
            (operator_pos == Rml::String::npos || pos < operator_pos)) {
            operator_pos = pos;
            operator_len = strlen(candidate.token);
            op = candidate.op;
        }
    }

    bool matches = false;

    if (operator_pos == Rml::String::npos) {
        // Bare tokens: engine-state names first, then truthy cvars. The
        // legacy JSON menus special-cased 'ingame' as client state.
        if (!Q_stricmp(term.c_str(), "ingame") ||
            !Q_stricmp(term.c_str(), "in_game")) {
            matches = cl.frame.valid;
        } else {
            cvar_t *var = Cvar_FindVar(term.c_str());
            matches = var && UI_Rml_CvarStringTruthy(var->string);
        }
    } else {
        const Rml::String name = UI_Rml_TrimString(term.substr(0, operator_pos));
        const Rml::String expected =
            UI_Rml_TrimString(term.substr(operator_pos + operator_len));
        double actual_number = 0.0;
        double expected_number = 0.0;

        if (!UI_Rml_ConditionNameIsValid(name)) {
            Com_WPrintf("RmlUi condition term '%s' uses unsupported syntax.\n",
                        term.c_str());
            return negate;
        }

        const char *actual_text = Cvar_VariableString(name.c_str());
        const bool expected_numeric =
            UI_Rml_ParseDouble(expected, &expected_number);
        // Missing/empty cvars compare as numeric 0 so 'cvar!=0' stays false
        // until the owner publishes a value (legacy fail-closed behavior).
        const bool actual_numeric =
            UI_Rml_ParseDouble(actual_text, &actual_number) ||
            !actual_text[0];

        switch (op) {
        case UI_RML_CONDITION_OP_EQUAL:
        case UI_RML_CONDITION_OP_NOT_EQUAL:
            if (expected_numeric && actual_numeric) {
                matches = fabs(actual_number - expected_number) < 0.000001;
            } else {
                matches = !Q_stricmp(actual_text, expected.c_str());
            }
            if (op == UI_RML_CONDITION_OP_NOT_EQUAL) {
                matches = !matches;
            }
            break;
        case UI_RML_CONDITION_OP_GREATER:
            matches = expected_numeric && actual_numeric &&
                      actual_number > expected_number;
            break;
        case UI_RML_CONDITION_OP_GREATER_EQUAL:
            matches = expected_numeric && actual_numeric &&
                      actual_number >= expected_number;
            break;
        case UI_RML_CONDITION_OP_LESS:
            matches = expected_numeric && actual_numeric &&
                      actual_number < expected_number;
            break;
        case UI_RML_CONDITION_OP_LESS_EQUAL:
            matches = expected_numeric && actual_numeric &&
                      actual_number <= expected_number;
            break;
        }
    }

    return negate ? !matches : matches;
}

static bool UI_Rml_ConditionExpressionMatches(const Rml::String &expression)
{
    if (expression.empty()) {
        return true;
    }

    size_t start = 0;
    while (start <= expression.size()) {
        const size_t end = expression.find(';', start);
        const Rml::String term =
            end == Rml::String::npos
                ? expression.substr(start)
                : expression.substr(start, end - start);

        if (!UI_Rml_ConditionTermMatches(term)) {
            return false;
        }

        if (end == Rml::String::npos) {
            break;
        }
        start = end + 1;
    }

    return true;
}

static Rml::String UI_Rml_ElementCondition(Rml::Element *element,
                                           const char *first,
                                           const char *second)
{
    if (!element) {
        return "";
    }

    Rml::String value = element->GetAttribute<Rml::String>(first, "");
    if (value.empty() && second) {
        value = element->GetAttribute<Rml::String>(second, "");
    }
    return value;
}

static void UI_Rml_ApplyElementConditions(Rml::Element *element)
{
    if (!element) {
        return;
    }

    bool visible = true;
    const Rml::String visible_if =
        UI_Rml_ElementCondition(element, "data-visible-if", nullptr);
    const Rml::String show_if =
        UI_Rml_ElementCondition(element, "data-show-if", nullptr);
    const bool has_visible_condition =
        !visible_if.empty() || !show_if.empty();
    if (!visible_if.empty()) {
        visible = visible && UI_Rml_ConditionExpressionMatches(visible_if);
    }
    if (!show_if.empty()) {
        visible = visible && UI_Rml_ConditionExpressionMatches(show_if);
    }

    // This pass runs every frame: only touch properties/attributes on state
    // changes, or the redundant sets dirty style definitions and force
    // relayout each frame.
    if (has_visible_condition) {
        // The local display property stores the parsed keyword as an int, so
        // compare against the enum -- string comparison never matches "none".
        const Rml::Property *display = element->GetLocalProperty("display");
        const bool currently_hidden =
            display && display->unit == Rml::Unit::KEYWORD &&
            display->Get<int>() ==
                static_cast<int>(Rml::Style::Display::None);

        if (visible && currently_hidden) {
            element->RemoveProperty("display");
        } else if (!visible && !currently_hidden) {
            element->SetProperty("display", "none");
        }
    }

    const Rml::String enable_if =
        UI_Rml_ElementCondition(element, "data-enable-if", "data-enabled-if");
    if (!enable_if.empty()) {
        const bool enabled = UI_Rml_ConditionExpressionMatches(enable_if);
        const bool currently_disabled = element->HasAttribute("disabled");

        if (enabled && currently_disabled) {
            element->RemoveAttribute("disabled");
        } else if (!enabled && !currently_disabled) {
            element->SetAttribute("disabled", "");
        }
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_ApplyElementConditions(element->GetChild(child_index));
    }
}

static bool UI_Rml_ElementIsCheckbox(Rml::Element *element)
{
    if (!element || Q_stricmp(element->GetTagName().c_str(), "input")) {
        return false;
    }

    const Rml::String type = element->GetAttribute<Rml::String>("type", "");
    return !Q_stricmp(type.c_str(), "checkbox");
}

static bool UI_Rml_CvarValueToChecked(Rml::Element *element, cvar_t *var)
{
    const int bit = UI_Rml_ElementIntAttribute(element, "data-bit", -1);
    const bool negate = UI_Rml_ElementBoolAttribute(element, "data-negate");
    bool checked = false;

    if (var) {
        if (bit >= 0 && bit < 31) {
            checked = (var->integer & (1 << bit)) != 0;
        } else {
            checked = var->integer != 0;
        }
    }

    return negate ? !checked : checked;
}

static void UI_Rml_SetCheckboxValue(Rml::Element *element, bool checked)
{
    if (!element) {
        return;
    }

    if (checked) {
        if (!element->HasAttribute("checked")) {
            element->SetAttribute("checked", "");
        }
    } else if (element->HasAttribute("checked")) {
        element->RemoveAttribute("checked");
    }
}

static void UI_Rml_SyncSelectCustomOption(Rml::Element *element,
                                          const Rml::String &value)
{
    auto *select =
        rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(element);
    if (!select) {
        return;
    }

    int custom_index = -1;
    bool preset_matches = false;
    bool has_placeholder = false;
    const int num_options = select->GetNumOptions();

    for (int i = 0; i < num_options; i++) {
        Rml::Element *option = select->GetOption(i);
        if (!option) {
            continue;
        }

        const bool is_custom = option->HasAttribute("data-custom-option");
        if (is_custom) {
            custom_index = i;
            continue;
        }

        const Rml::String option_value =
            option->GetAttribute<Rml::String>("value", "");
        if (option_value == value) {
            preset_matches = true;
        }
        if (option_value.empty()) {
            has_placeholder = true;
        }
    }

    // Selects with an authored empty-value placeholder represent 'nothing
    // chosen yet'; unmatched sentinel values fall back to the placeholder
    // instead of minting a '(custom)' entry.
    if (preset_matches || value.empty() || has_placeholder) {
        if (custom_index >= 0) {
            select->Remove(custom_index);
        }
        return;
    }

    // Keep out-of-preset config values representable instead of showing a
    // stale first option.
    if (custom_index >= 0) {
        Rml::Element *option = select->GetOption(custom_index);
        if (option &&
            option->GetAttribute<Rml::String>("value", "") == value) {
            return;
        }
        select->Remove(custom_index);
    }

    const Rml::String label =
        UI_Rml_EscapeTextForInnerRml(value.c_str()) + " (custom)";
    const int added = select->Add(label, value);
    if (added >= 0) {
        if (Rml::Element *option = select->GetOption(added)) {
            option->SetAttribute("data-custom-option", "");
        }
    }
}

static void UI_Rml_ApplyCvarToControl(Rml::Element *element, cvar_t *var)
{
    if (!element || !var) {
        return;
    }

    auto *control =
        rmlui_dynamic_cast<Rml::ElementFormControl *>(element);
    if (!control) {
        return;
    }

    if (UI_Rml_ElementIsCheckbox(element)) {
        UI_Rml_SetCheckboxValue(element, UI_Rml_CvarValueToChecked(element, var));
        return;
    }

    const Rml::String value = var->string ? var->string : "";
    UI_Rml_SyncSelectCustomOption(element, value);
    control->SetValue(value);
}

static void UI_Rml_ApplyCvarToProgress(Rml::Element *element, cvar_t *var)
{
    if (!element || !var ||
        Q_stricmp(element->GetTagName().c_str(), "progress")) {
        return;
    }

    element->SetAttribute("value", var->string ? var->string : "0");
}

static void UI_Rml_ApplyCvarToText(Rml::Element *element, cvar_t *var)
{
    if (!element || !var) {
        return;
    }

    Rml::String text = UI_Rml_FormatCvarDisplayText(var);

    // Optional display transforms so readouts can show units ('75%')
    // instead of raw cvar fractions ('0.75').
    const Rml::String display_scale =
        element->GetAttribute<Rml::String>("data-display-scale", "");
    if (!display_scale.empty()) {
        double value = 0.0;
        double scale = 1.0;
        if (UI_Rml_ParseDouble(text, &value) &&
            UI_Rml_ParseDouble(display_scale, &scale)) {
            text = va("%g", value * scale);
        }
    }

    const Rml::String display_suffix =
        element->GetAttribute<Rml::String>("data-display-suffix", "");
    if (!display_suffix.empty()) {
        text += display_suffix;
    }

    UI_Rml_SetElementInnerText(element, text.c_str());
}

static bool UI_Rml_ImageSourceExists(const Rml::String &source)
{
    if (source.empty()) {
        return false;
    }

    const char *path = source.c_str();
    while (*path == '/') {
        ++path;
    }
    if (!*path) {
        return false;
    }

    if (FS_FileExists(path)) {
        return true;
    }

    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(path, '.');
    if (dot && (!slash || dot > slash)) {
        return false;
    }

    static const char *const image_extensions[] = {
        ".png", ".pcx", ".tga", ".jpg", ".jpeg", ".wal",
    };
    char candidate[MAX_QPATH];
    for (const char *extension : image_extensions) {
        Q_concat(candidate, sizeof(candidate), path, extension);
        if (FS_FileExists(candidate)) {
            return true;
        }
    }

    return false;
}

// data-src-cvar: rebinds an <img> src from a cvar (optionally wrapped with
// data-src-prefix/data-src-suffix), e.g. levelshot previews that follow the
// map select. Missing dynamic images restore data-src-fallback instead of
// displaying RmlUi's untextured white quad. Change-guarded: setting src
// reloads the texture.
static void UI_Rml_ApplyCvarToSrc(Rml::Element *element, cvar_t *var)
{
    if (!element || !var || !var->string || !var->string[0]) {
        return;
    }

    const Rml::String prefix =
        element->GetAttribute<Rml::String>("data-src-prefix", "");
    const Rml::String suffix =
        element->GetAttribute<Rml::String>("data-src-suffix", "");
    const Rml::String desired = prefix + var->string + suffix;
    const Rml::String bound_source =
        element->GetAttribute<Rml::String>("data-src-bound-source", "");

    if (!UI_Rml_ImageSourceExists(desired)) {
        const Rml::String fallback =
            element->GetAttribute<Rml::String>("data-src-fallback", "");
        if (!fallback.empty() && !bound_source.empty()) {
            element->SetAttribute("src", fallback);
            element->RemoveAttribute("data-src-bound-source");
        }
        return;
    }

    if (bound_source != desired) {
        element->SetAttribute("src", desired);
        element->SetAttribute("data-src-bound-source", desired);
    }
}

// data-gauge-cvar: maps a numeric cvar onto quartile classes is-q0..is-q4 so
// sprite-ring gauges can present percentages without shader arcs.
static void UI_Rml_ApplyCvarToGauge(Rml::Element *element, cvar_t *var)
{
    if (!element || !var) {
        return;
    }

    double value = 0.0;
    double minimum = 0.0;
    double maximum = 1.0;
    int quartile = 0;

    if (UI_Rml_ParseDouble(var->string ? var->string : "", &value) &&
        UI_Rml_ParseDouble(
            element->GetAttribute<Rml::String>("data-gauge-min", "0"),
            &minimum) &&
        UI_Rml_ParseDouble(
            element->GetAttribute<Rml::String>("data-gauge-max", "1"),
            &maximum) &&
        maximum > minimum) {
        double ratio = (value - minimum) / (maximum - minimum);

        if (ratio < 0.0) {
            ratio = 0.0;
        } else if (ratio > 1.0) {
            ratio = 1.0;
        }
        quartile = (int)(ratio * 4.0 + 0.5);
    }

    for (int q = 0; q <= 4; q++) {
        const char *name = va("is-q%d", q);
        const bool want = (q == quartile);

        if (element->IsClassSet(name) != want) {
            element->SetClass(name, want);
        }
    }
}

static void UI_Rml_ApplyCvarToMeter(Rml::Element *element, cvar_t *var)
{
    if (!element || !var) {
        return;
    }

    double value = 0.0;
    double minimum = 0.0;
    double maximum = 1.0;

    if (!UI_Rml_ParseDouble(var->string ? var->string : "", &value) ||
        !UI_Rml_ParseDouble(
            element->GetAttribute<Rml::String>("data-meter-min", "0"),
            &minimum) ||
        !UI_Rml_ParseDouble(
            element->GetAttribute<Rml::String>("data-meter-max", "1"),
            &maximum) ||
        maximum <= minimum) {
        element->SetProperty("width", "0%");
        return;
    }

    double ratio = (value - minimum) / (maximum - minimum);
    if (ratio < 0.0) {
        ratio = 0.0;
    } else if (ratio > 1.0) {
        ratio = 1.0;
    }
    element->SetProperty("width", va("%.3f%%", ratio * 100.0));
}

static Rml::String UI_Rml_DataBindCvarName(Rml::Element *element)
{
    if (!element) {
        return "";
    }

    const Rml::String data_bind =
        element->GetAttribute<Rml::String>("data-bind", "");
    static const char prefix[] = "cvars.";
    const size_t prefix_length = sizeof(prefix) - 1;

    if (data_bind.size() <= prefix_length ||
        data_bind.compare(0, prefix_length, prefix)) {
        return "";
    }

    return data_bind.substr(prefix_length);
}

static void UI_Rml_RefreshCvarBindings(Rml::Element *element,
                                       const Rml::String *only_cvar,
                                       bool include_controls)
{
    if (!element) {
        return;
    }

    const Rml::String data_cvar =
        element->GetAttribute<Rml::String>("data-cvar", "");
    if (!data_cvar.empty() &&
        (!only_cvar || data_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(data_cvar.c_str())) {
            if (include_controls) {
                UI_Rml_ApplyCvarToControl(element, var);
            }
            UI_Rml_ApplyCvarToProgress(element, var);
        }
    }

    const Rml::String meter_cvar =
        element->GetAttribute<Rml::String>("data-meter-cvar", "");
    if (!meter_cvar.empty() &&
        (!only_cvar || meter_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(meter_cvar.c_str())) {
            UI_Rml_ApplyCvarToMeter(element, var);
        }
    }

    const Rml::String src_cvar =
        element->GetAttribute<Rml::String>("data-src-cvar", "");
    if (!src_cvar.empty() &&
        (!only_cvar || src_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(src_cvar.c_str())) {
            UI_Rml_ApplyCvarToSrc(element, var);
        }
    }

    const Rml::String gauge_cvar =
        element->GetAttribute<Rml::String>("data-gauge-cvar", "");
    if (!gauge_cvar.empty() &&
        (!only_cvar || gauge_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(gauge_cvar.c_str())) {
            UI_Rml_ApplyCvarToGauge(element, var);
        }
    }

    // Text bindings replace inner RML, which corrupts form controls; those
    // synchronize exclusively through the data-cvar control path.
    const bool is_form_control =
        rmlui_dynamic_cast<Rml::ElementFormControl *>(element) != nullptr;

    const Rml::String bind_cvar =
        element->GetAttribute<Rml::String>("data-bind-cvar", "");
    if (!bind_cvar.empty() && !is_form_control &&
        (!only_cvar || bind_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(bind_cvar.c_str())) {
            if (UI_Rml_CvarHasDisplayText(var)) {
                UI_Rml_ApplyCvarToText(element, var);
            }
        }
    }

    const Rml::String label_cvar =
        element->GetAttribute<Rml::String>("data-label-cvar", "");
    if (!label_cvar.empty() && !is_form_control &&
        (!only_cvar || label_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(label_cvar.c_str())) {
            if (UI_Rml_CvarHasDisplayText(var)) {
                UI_Rml_ApplyCvarToText(element, var);
            }
        }
    }

    const Rml::String data_bind_cvar = UI_Rml_DataBindCvarName(element);
    if (!data_bind_cvar.empty() && !is_form_control &&
        (!only_cvar || data_bind_cvar == *only_cvar)) {
        if (cvar_t *var = Cvar_FindVar(data_bind_cvar.c_str())) {
            if (UI_Rml_CvarHasDisplayText(var)) {
                UI_Rml_ApplyCvarToText(element, var);
            }
        }
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_RefreshCvarBindings(element->GetChild(child_index),
                                   only_cvar,
                                   include_controls);
    }
}

static void UI_Rml_ApplyDocumentCvarBindings(Rml::ElementDocument *document)
{
    if (!document) {
        return;
    }

    ui_rml_applying_cvar_bindings = true;
    UI_Rml_RefreshCvarBindings(document, nullptr, true);
    UI_Rml_ApplyElementConditions(document);
    ui_rml_applying_cvar_bindings = false;
}

static void UI_Rml_RefreshDocumentCvarDisplays(Rml::ElementDocument *document)
{
    if (!document) {
        return;
    }

    ui_rml_applying_cvar_bindings = true;
    UI_Rml_RefreshCvarBindings(document, nullptr, false);
    UI_Rml_ApplyElementConditions(document);
    ui_rml_applying_cvar_bindings = false;
}

static cvar_t *UI_Rml_SetCvarInteger(const Rml::String &name,
                                     cvar_t *var,
                                     int value)
{
    if (var) {
        Cvar_SetInteger(var, value, FROM_MENU);
        return var;
    }

    const std::string value_text = std::to_string(value);
    return Cvar_SetEx(name.c_str(), value_text.c_str(), FROM_MENU);
}

static cvar_t *UI_Rml_SetCvarFloat(const Rml::String &name,
                                   cvar_t *var,
                                   float value)
{
    if (var) {
        Cvar_SetValue(var, value, FROM_MENU);
        return var;
    }

    return Cvar_SetEx(name.c_str(), va("%g", value), FROM_MENU);
}

static cvar_t *UI_Rml_SetCvarString(const Rml::String &name,
                                    cvar_t *var,
                                    const Rml::String &value)
{
    if (var) {
        Cvar_SetByVar(var, value.c_str(), FROM_MENU);
        return var;
    }

    return Cvar_SetEx(name.c_str(), value.c_str(), FROM_MENU);
}

static Rml::Element *UI_Rml_FindCvarControlElement(Rml::Element *element)
{
    for (Rml::Element *current = element; current; current = current->GetParentNode()) {
        if (current->GetAttribute<Rml::String>("data-cvar", "").empty()) {
            continue;
        }

        if (rmlui_dynamic_cast<Rml::ElementFormControl *>(current)) {
            return current;
        }
    }

    return nullptr;
}

static void UI_Rml_SetCvarFromControl(Rml::Element *element)
{
    if (ui_rml_applying_cvar_bindings || !element) {
        return;
    }

    auto *control =
        rmlui_dynamic_cast<Rml::ElementFormControl *>(element);
    if (!control) {
        return;
    }

    const Rml::String cvar_name =
        element->GetAttribute<Rml::String>("data-cvar", "");
    if (cvar_name.empty()) {
        return;
    }

    cvar_t *var = Cvar_FindVar(cvar_name.c_str());

    if (UI_Rml_ElementIsCheckbox(element)) {
        const int bit = UI_Rml_ElementIntAttribute(element, "data-bit", -1);
        const bool negate = UI_Rml_ElementBoolAttribute(element, "data-negate");
        const bool visible_checked = element->HasAttribute("checked");
        const bool cvar_checked = negate ? !visible_checked : visible_checked;

        if (bit >= 0 && bit < 31) {
            const int mask = 1 << bit;
            int next_value = var ? var->integer : 0;
            if (cvar_checked) {
                next_value |= mask;
            } else {
                next_value &= ~mask;
            }
            var = UI_Rml_SetCvarInteger(cvar_name, var, next_value);
        } else {
            var = UI_Rml_SetCvarInteger(cvar_name, var, cvar_checked ? 1 : 0);
        }
    } else {
        const Rml::String value = control->GetValue();
        const Rml::String type = element->GetAttribute<Rml::String>("type", "");

        if (UI_Rml_ElementBoolAttribute(element, "data-integer")) {
            var = UI_Rml_SetCvarInteger(cvar_name,
                                        var,
                                        static_cast<int>(strtol(value.c_str(), nullptr, 10)));
        } else if (!Q_stricmp(type.c_str(), "range")) {
            var = UI_Rml_SetCvarFloat(cvar_name,
                                      var,
                                      static_cast<float>(atof(value.c_str())));
        } else if (UI_Rml_ElementBoolAttribute(element, "data-numeric")) {
            double parsed = 0.0;
            if (UI_Rml_ParseDouble(UI_Rml_TrimString(value), &parsed)) {
                var = UI_Rml_SetCvarFloat(cvar_name, var,
                                          static_cast<float>(parsed));
            } else if (var) {
                // Reject free-form text on numeric fields; restore the
                // control from the unchanged cvar.
                ui_rml_applying_cvar_bindings = true;
                UI_Rml_ApplyCvarToControl(element, var);
                ui_rml_applying_cvar_bindings = false;
                return;
            } else {
                return;
            }
        } else {
            var = UI_Rml_SetCvarString(cvar_name, var, value);
        }
    }

    if (!ui_rml_document || !var) {
        return;
    }

    ui_rml_applying_cvar_bindings = true;
    UI_Rml_RefreshCvarBindings(ui_rml_document, &cvar_name, true);
    UI_Rml_ApplyElementConditions(ui_rml_document);
    ui_rml_applying_cvar_bindings = false;
}

class UI_Rml_CvarEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &event) override
    {
        if (event.GetId() != Rml::EventId::Change) {
            return;
        }

        UI_Rml_SetCvarFromControl(
            UI_Rml_FindCvarControlElement(event.GetTargetElement()));
    }
};

static UI_Rml_CvarEventListener ui_rml_cvar_event_listener;

static void UI_Rml_AttachElementCvarListeners(Rml::Element *element)
{
    if (!element) {
        return;
    }

    if (!element->GetAttribute<Rml::String>("data-cvar", "").empty() &&
        rmlui_dynamic_cast<Rml::ElementFormControl *>(element)) {
        element->AddEventListener(Rml::EventId::Change,
                                  &ui_rml_cvar_event_listener);
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_AttachElementCvarListeners(element->GetChild(child_index));
    }
}

static void UI_Rml_ApplyDocumentAudioHints(const char *route_id,
                                           Rml::ElementDocument *document)
{
    Rml::String music;
    Rml::String open_sound;
    Rml::Element *body = UI_Rml_DocumentBody(document);

    if (!document) {
        return;
    }

    open_sound = document->GetAttribute<Rml::String>("data-menu-sound-open", "");
    if (open_sound.empty() && body) {
        open_sound = body->GetAttribute<Rml::String>("data-menu-sound-open", "");
    }

    if (!open_sound.empty()) {
        const int sound = UI_Rml_MenuSoundForName(open_sound);

        if (sound >= 0) {
            UI_Rml_PlayMenuFeedbackSound(static_cast<uiFeedbackSound_t>(sound));
            if (Cvar_VariableInteger("ui_rml_debug")) {
                Com_Printf("RmlUi route '%s' requested menu open sound '%s'.\n",
                           route_id && route_id[0] ? route_id : "<unknown>",
                           open_sound.c_str());
            }
        }
    }

    music = document->GetAttribute<Rml::String>("data-menu-music", "");
    if (music.empty() && body) {
        music = body->GetAttribute<Rml::String>("data-menu-music", "");
    }

    if (music.empty() ||
        !Q_stricmp(music.c_str(), "none") ||
        !Q_stricmp(music.c_str(), "silent")) {
        return;
    }

    if (!Q_stricmp(music.c_str(), "menu") ||
        !Q_stricmp(music.c_str(), "auto")) {
        OGG_Play();
        if (Cvar_VariableInteger("ui_rml_debug")) {
            Com_Printf("RmlUi route '%s' requested menu music cue '%s'.\n",
                       route_id && route_id[0] ? route_id : "<unknown>",
                       music.c_str());
        }
        return;
    }

    if (!Q_stricmp(music.c_str(), "off") ||
        !Q_stricmp(music.c_str(), "stop")) {
        OGG_Stop();
        if (Cvar_VariableInteger("ui_rml_debug")) {
            Com_Printf("RmlUi route '%s' requested music stop cue '%s'.\n",
                       route_id && route_id[0] ? route_id : "<unknown>",
                       music.c_str());
        }
    }
}

static bool UI_Rml_ElementIsInteractionTag(Rml::Element *element)
{
    if (!element) {
        return false;
    }

    const Rml::String &tag = element->GetTagName();

    return !Q_stricmp(tag.c_str(), "button") ||
           !Q_stricmp(tag.c_str(), "input") ||
           !Q_stricmp(tag.c_str(), "select") ||
           !Q_stricmp(tag.c_str(), "textarea");
}

static bool UI_Rml_ElementWantsInteractionAudio(Rml::Element *element)
{
    return element &&
           (UI_Rml_ElementIsInteractionTag(element) ||
            element->HasAttribute("data-command") ||
            element->HasAttribute("data-command-cvar") ||
            element->HasAttribute("data-route") ||
            element->HasAttribute("data-route-target") ||
            element->HasAttribute("data-server-action") ||
            element->HasAttribute("data-demo-action") ||
            element->HasAttribute("data-cvar") ||
            element->HasAttribute("data-event-click") ||
            element->HasAttribute("data-event-change") ||
            element->HasAttribute("data-menu-sound-focus") ||
           element->HasAttribute("data-menu-sound-change"));
}

static void UI_Rml_ApplyDemoFocusSelection(Rml::Element *element)
{
    if (!element || !ui_rml_document ||
        element->GetAttribute<Rml::String>("data-demo-action", "") !=
            "activate") {
        return;
    }

    Rml::Element *row = element;
    while (row && Q_stricmp(row->GetTagName().c_str(), "tr")) {
        row = row->GetParentNode();
    }
    if (!row) {
        return;
    }

    if (Rml::Element *selected =
            ui_rml_document->QuerySelector(".demo-row.is-selected")) {
        if (selected != row) {
            selected->SetClass("is-selected", false);
        }
    }
    row->SetClass("is-selected", true);
}

static void UI_Rml_ApplyServerFocusSelection(Rml::Element *element)
{
    if (!element || !ui_rml_document ||
        element->GetAttribute<Rml::String>("data-server-action", "") !=
            "select") {
        return;
    }

    Rml::Element *row = element;
    while (row && Q_stricmp(row->GetTagName().c_str(), "tr")) {
        row = row->GetParentNode();
    }
    if (!row) {
        return;
    }

    if (Rml::Element *selected =
            ui_rml_document->QuerySelector(".server-row.is-selected")) {
        if (selected != row) {
            selected->SetClass("is-selected", false);
        }
    }
    row->SetClass("is-selected", true);
}

class UI_Rml_AudioEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &event) override
    {
        if (ui_rml_applying_cvar_bindings) {
            return;
        }

        const Rml::EventId event_id = event.GetId();

        if (event_id != Rml::EventId::Focus &&
            event_id != Rml::EventId::Change) {
            return;
        }

        Rml::Element *element = event.GetTargetElement();
        if (event_id == Rml::EventId::Focus) {
            UI_Rml_ApplyServerFocusSelection(element);
            UI_Rml_ApplyDemoFocusSelection(element);
        }
        if (!UI_Rml_ElementWantsInteractionAudio(element)) {
            return;
        }

        const char *attribute =
            event_id == Rml::EventId::Focus
                ? "data-menu-sound-focus"
                : "data-menu-sound-change";
        const Rml::String sound_name =
            element->GetAttribute<Rml::String>(attribute, "");
        const int sound = sound_name.empty()
                              ? UI_FEEDBACK_MOVE
                              : UI_Rml_MenuSoundForName(sound_name);

        if (sound >= 0) {
            UI_Rml_PlayMenuFeedbackSound(static_cast<uiFeedbackSound_t>(sound));
        }
    }
};

static UI_Rml_AudioEventListener ui_rml_audio_event_listener;

static void UI_Rml_AttachElementAudioListeners(Rml::Element *element)
{
    if (!element) {
        return;
    }

    if (UI_Rml_ElementWantsInteractionAudio(element)) {
        element->AddEventListener(Rml::EventId::Focus,
                                  &ui_rml_audio_event_listener);
        element->AddEventListener(Rml::EventId::Change,
                                  &ui_rml_audio_event_listener);
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_AttachElementAudioListeners(element->GetChild(child_index));
    }
}

static bool UI_Rml_CommandIsPlainRouteOpen(const Rml::String &command,
                                           const Rml::String &route_target)
{
    if (route_target.empty()) {
        return false;
    }

    if (command.empty() || command == "ui.open_route") {
        return true;
    }

    if (!UI_Rml_CommandStartsWithToken(command, "pushmenu")) {
        return false;
    }

    const Rml::String argument =
        UI_Rml_CommandFirstArgumentAfterToken(command, "pushmenu");
    if (argument != route_target) {
        return false;
    }

    size_t position = command.find(argument, strlen("pushmenu"));
    if (position == Rml::String::npos) {
        return false;
    }

    position += argument.size();
    while (position < command.size() &&
           (command[position] == ' ' || command[position] == '\t')) {
        position++;
    }

    return position >= command.size();
}

static Rml::Element *UI_Rml_FindKeybindElement(Rml::Element *element)
{
    for (Rml::Element *current = element; current; current = current->GetParentNode()) {
        if (!current->GetAttribute<Rml::String>("data-bind-command", "").empty()) {
            return current;
        }
    }

    return nullptr;
}

static void UI_Rml_SetKeybindKeyText(Rml::Element *element, const char *text)
{
    if (!element) {
        return;
    }

    if (Rml::Element *key_span = element->QuerySelector(".bind-key")) {
        UI_Rml_SetElementInnerText(key_span, text);
    }
}

static int UI_Rml_MapKeynumToMouseIcon(int keynum)
{
    switch (keynum) {
    case K_MOUSE1:
        return 1;
    case K_MOUSE2:
        return 2;
    case K_MOUSE3:
        return 0;
    case K_MOUSE4:
        return 5;
    case K_MOUSE5:
        return 6;
    case K_MOUSE6:
        return 7;
    case K_MOUSE7:
        return 8;
    case K_MOUSE8:
        return 9;
    case K_MWHEELUP:
        return 3;
    case K_MWHEELDOWN:
        return 4;
    default:
        return -1;
    }
}

static int UI_Rml_MapKeynumToKeyboardIcon(int keynum)
{
    switch (keynum) {
    case K_BACKSPACE:
        return 8;
    case K_TAB:
        return 9;
    case K_ENTER:
        return 13;
    case K_PAUSE:
        return 271;
    case K_ESCAPE:
        return 27;
    case K_SPACE:
        return 32;
    case K_DEL:
        return 275;
    case K_CAPSLOCK:
        return 256;
    case K_F1:
        return 257;
    case K_F2:
        return 258;
    case K_F3:
        return 259;
    case K_F4:
        return 260;
    case K_F5:
        return 261;
    case K_F6:
        return 262;
    case K_F7:
        return 263;
    case K_F8:
        return 264;
    case K_F9:
        return 265;
    case K_F10:
        return 266;
    case K_F11:
        return 267;
    case K_F12:
        return 268;
    case K_PRINTSCREEN:
        return 269;
    case K_SCROLLOCK:
        return 270;
    case K_INS:
        return 272;
    case K_HOME:
        return 273;
    case K_PGUP:
        return 274;
    case K_END:
        return 276;
    case K_PGDN:
        return 277;
    case K_RIGHTARROW:
        return 278;
    case K_LEFTARROW:
        return 279;
    case K_DOWNARROW:
        return 280;
    case K_UPARROW:
        return 281;
    case K_NUMLOCK:
        return 282;
    case K_KP_SLASH:
        return 283;
    case K_KP_MULTIPLY:
        return 42;
    case K_KP_MINUS:
        return 285;
    case K_KP_PLUS:
        return 286;
    case K_KP_ENTER:
        return 287;
    case K_KP_END:
        return 288;
    case K_KP_DOWNARROW:
        return 289;
    case K_KP_PGDN:
        return 290;
    case K_KP_LEFTARROW:
        return 291;
    case K_KP_5:
        return 292;
    case K_KP_RIGHTARROW:
        return 293;
    case K_KP_HOME:
        return 294;
    case K_KP_UPARROW:
        return 295;
    case K_KP_PGUP:
        return 296;
    case K_KP_INS:
        return 297;
    case K_KP_DEL:
        return 298;
    case K_CTRL:
    case K_LCTRL:
        return 299;
    case K_SHIFT:
    case K_LSHIFT:
        return 300;
    case K_ALT:
    case K_LALT:
        return 301;
    case K_RCTRL:
        return 302;
    case K_RSHIFT:
        return 303;
    case K_RALT:
        return 304;
    default:
        break;
    }

    if (keynum >= K_ASCIIFIRST && keynum <= K_ASCIILAST) {
        return keynum;
    }

    return -1;
}

static bool UI_Rml_BuildKeybindIconPath(int keynum,
                                        char *out_path,
                                        size_t out_size)
{
    const int mouse_icon = UI_Rml_MapKeynumToMouseIcon(keynum);
    if (mouse_icon >= 0) {
        Q_snprintf(out_path,
                   out_size,
                   "/gfx/controller/mouse/f000%i.png",
                   mouse_icon);
        return true;
    }

    if (keynum >= K_GAMEPAD_FIRST && keynum <= K_GAMEPAD_LAST) {
        Q_snprintf(out_path,
                   out_size,
                   "/gfx/controller/generic/f%04x.png",
                   keynum - K_GAMEPAD_FIRST);
        return true;
    }

    const int keyboard_icon = UI_Rml_MapKeynumToKeyboardIcon(keynum);
    if (keyboard_icon >= 0) {
        Q_snprintf(out_path,
                   out_size,
                   "/gfx/controller/keyboard/%i.png",
                   keyboard_icon);
        return true;
    }

    return false;
}

static void UI_Rml_SetKeybindIcon(Rml::Element *element, int keynum)
{
    if (!element) {
        return;
    }

    Rml::Element *icon = element->QuerySelector(".bind-icon");
    if (!icon) {
        return;
    }

    char icon_path[MAX_QPATH];
    if (keynum >= 0 &&
        UI_Rml_BuildKeybindIconPath(keynum, icon_path, sizeof(icon_path))) {
        icon->SetAttribute("src", icon_path);
        icon->RemoveProperty("display");
        return;
    }

    icon->SetProperty("display", "none");
}

static int UI_Rml_KeybindSlotForElement(Rml::Element *element)
{
    if (!element) {
        return 0;
    }

    return max(0,
               atoi(element->GetAttribute<Rml::String>("data-bind-slot", "0")
                        .c_str()));
}

static int UI_Rml_KeybindKeyForSlot(const char *command, int slot)
{
    int key = 0;

    for (int index = 0; index <= slot; index++) {
        key = Key_EnumBindings(key, command);
        if (key == -1) {
            return -1;
        }
        if (index != slot) {
            key++;
        }
    }

    return key;
}

static void UI_Rml_RefreshKeybindDisplays(Rml::Element *element)
{
    if (!element) {
        return;
    }

    const Rml::String bind_command =
        element->GetAttribute<Rml::String>("data-bind-command", "");
    if (!bind_command.empty() && element != ui_rml_keybind_capture_element) {
        const int keynum = UI_Rml_KeybindKeyForSlot(
            bind_command.c_str(), UI_Rml_KeybindSlotForElement(element));
        const char *key_name =
            keynum >= 0 ? Key_KeynumToString(keynum) : nullptr;
        UI_Rml_SetKeybindKeyText(element,
                                 key_name && key_name[0] ? key_name : "---");
        UI_Rml_SetKeybindIcon(element, keynum);
    }

    const int num_children = element->GetNumChildren();
    for (int child_index = 0; child_index < num_children; child_index++) {
        UI_Rml_RefreshKeybindDisplays(element->GetChild(child_index));
    }
}

static Rml::Element *UI_Rml_KeybindRowForElement(Rml::Element *element)
{
    for (Rml::Element *walk = element; walk; walk = walk->GetParentNode()) {
        if (walk->IsClassSet("bind-row")) {
            return walk;
        }
    }

    return nullptr;
}

static void UI_Rml_ClearKeybindConflict(void)
{
    if (ui_rml_keybind_conflict_element) {
        ui_rml_keybind_conflict_element->SetClass("has-conflict", false);
        if (Rml::Element *row =
                UI_Rml_KeybindRowForElement(ui_rml_keybind_conflict_element)) {
            row->SetClass("has-bind-conflict", false);
            if (Rml::Element *panel =
                    row->QuerySelector("[data-keybind-conflict]")) {
                panel->SetProperty("display", "none");
            }
        }
    }

    ui_rml_keybind_conflict_element = nullptr;
    ui_rml_keybind_conflict_command.clear();
    ui_rml_keybind_conflict_previous_command.clear();
    ui_rml_keybind_conflict_key = -1;
}

static void UI_Rml_EndKeybindCapture(void)
{
    if (ui_rml_keybind_capture_element) {
        ui_rml_keybind_capture_element->SetClass("is-capturing", false);
        ui_rml_keybind_capture_element = nullptr;
    }

    ui_rml_keybind_capture_command.clear();
    ui_rml_keybind_capture_slot = 0;
    ui_rml_keybind_capture_started = 0;

    if (ui_rml_document) {
        UI_Rml_RefreshKeybindDisplays(ui_rml_document);
    }
}

static void UI_Rml_BeginKeybindCapture(Rml::Element *element)
{
    UI_Rml_ClearKeybindConflict();
    UI_Rml_EndKeybindCapture();

    const Rml::String bind_command =
        element->GetAttribute<Rml::String>("data-bind-command", "");
    if (bind_command.empty()) {
        return;
    }

    ui_rml_keybind_capture_element = element;
    ui_rml_keybind_capture_command = bind_command;
    ui_rml_keybind_capture_slot = UI_Rml_KeybindSlotForElement(element);
    ui_rml_keybind_capture_started = Sys_Milliseconds();
    element->SetClass("is-capturing", true);
    UI_Rml_SetKeybindIcon(element, -1);
    UI_Rml_SetKeybindKeyText(element, "PRESS A KEY...");
}

static void UI_Rml_ClearKeybindSlot(const char *command, int slot)
{
    const int key = UI_Rml_KeybindKeyForSlot(command, slot);
    if (key >= 0) {
        Key_SetBinding(key, NULL);
    }
}

static void UI_Rml_AssignKeybindSlot(const char *command, int slot, int new_key)
{
    // The engine stores a command's bindings as an unordered key table. Keep
    // the other authored chip, replace the selected chip, and normalize any
    // console-created extras back to the two-slot UI contract.
    const int other_key =
        UI_Rml_KeybindKeyForSlot(command, slot == 0 ? 1 : 0);
    int key = 0;
    while ((key = Key_EnumBindings(key, command)) != -1) {
        Key_SetBinding(key, NULL);
        key++;
    }

    if (other_key >= 0 && other_key != new_key) {
        Key_SetBinding(other_key, command);
    }
    Key_SetBinding(new_key, command);
}

static void UI_Rml_ShowKeybindConflict(Rml::Element *element,
                                       const Rml::String &command,
                                       int key,
                                       const char *previous_command)
{
    ui_rml_keybind_conflict_element = element;
    ui_rml_keybind_conflict_command = command;
    ui_rml_keybind_conflict_previous_command =
        previous_command ? previous_command : "";
    ui_rml_keybind_conflict_key = key;

    element->SetClass("has-conflict", true);
    Rml::Element *row = UI_Rml_KeybindRowForElement(element);
    if (!row) {
        return;
    }

    row->SetClass("has-bind-conflict", true);
    Rml::Element *panel = row->QuerySelector("[data-keybind-conflict]");
    if (!panel) {
        return;
    }

    char detail[256];
    Q_snprintf(detail,
               sizeof(detail),
               "%s is already bound to %s. Replace it?",
               Key_KeynumToString(key),
               ui_rml_keybind_conflict_previous_command.c_str());
    if (Rml::Element *detail_element =
            panel->QuerySelector(".bind-conflict-detail")) {
        UI_Rml_SetElementInnerText(detail_element, detail);
    }
    panel->RemoveProperty("display");

    if (Rml::Element *replace =
            panel->QuerySelector("[data-keybind-conflict-action=replace]")) {
        replace->Focus(true);
        replace->ScrollIntoView(Rml::ScrollAlignment::Nearest);
    }
}

static void UI_Rml_CommitKeybindConflict(void)
{
    if (!ui_rml_keybind_conflict_element ||
        ui_rml_keybind_conflict_key < 0 ||
        ui_rml_keybind_conflict_command.empty()) {
        UI_Rml_ClearKeybindConflict();
        return;
    }

    UI_Rml_AssignKeybindSlot(
        ui_rml_keybind_conflict_command.c_str(),
        UI_Rml_KeybindSlotForElement(ui_rml_keybind_conflict_element),
        ui_rml_keybind_conflict_key);

    UI_Rml_ClearKeybindConflict();
    if (ui_rml_document) {
        UI_Rml_RefreshKeybindDisplays(ui_rml_document);
    }
}

static Rml::Element *UI_Rml_FindKeybindConflictAction(Rml::Element *element)
{
    for (Rml::Element *walk = element; walk; walk = walk->GetParentNode()) {
        if (!walk->GetAttribute<Rml::String>("data-keybind-conflict-action", "")
                 .empty()) {
            return walk;
        }
    }

    return nullptr;
}

static bool UI_Rml_HandleKeybindConflictAction(Rml::Element *element)
{
    if (!element) {
        return false;
    }

    const Rml::String action = element->GetAttribute<Rml::String>(
        "data-keybind-conflict-action", "");
    if (action == "replace") {
        UI_Rml_CommitKeybindConflict();
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_ALERT);
        return true;
    }
    if (action == "cancel") {
        UI_Rml_ClearKeybindConflict();
        if (ui_rml_document) {
            UI_Rml_RefreshKeybindDisplays(ui_rml_document);
        }
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_CLOSE);
        return true;
    }

    return false;
}

static bool UI_Rml_HandleKeybindCaptureKey(int key, bool down)
{
    if (!ui_rml_keybind_capture_element) {
        return false;
    }

    if (!down) {
        return true;
    }

    if (key == K_ESCAPE) {
        UI_Rml_EndKeybindCapture();
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_CLOSE);
        return true;
    }

    const Rml::String command = ui_rml_keybind_capture_command;
    Rml::Element *capture_element = ui_rml_keybind_capture_element;
    const int capture_slot = ui_rml_keybind_capture_slot;

    if (key == K_BACKSPACE || key == K_DEL) {
        UI_Rml_ClearKeybindSlot(command.c_str(), capture_slot);
        UI_Rml_EndKeybindCapture();
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_MOVE);
        return true;
    }

    const char *previous_command = Key_GetBindingForKey(key);
    if (previous_command && previous_command[0] &&
        Q_stricmp(previous_command, command.c_str())) {
        Rml::String conflict_command = previous_command;
        UI_Rml_EndKeybindCapture();
        UI_Rml_ShowKeybindConflict(
            capture_element, command, key, conflict_command.c_str());
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_ALERT);
        return true;
    }

    UI_Rml_AssignKeybindSlot(command.c_str(), capture_slot, key);
    UI_Rml_EndKeybindCapture();
    UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_ALERT);
    return true;
}

// Disabled controls (attribute or pseudo class, on the element or any
// ancestor) must not dispatch commands: pointer-events only guards mouse
// clicks, while Enter/Space activation still reaches this listener.
static bool UI_Rml_ElementIsDisabled(Rml::Element *element)
{
    for (Rml::Element *walk = element; walk; walk = walk->GetParentNode()) {
        if (walk->HasAttribute("disabled") ||
            walk->IsPseudoClassSet("disabled")) {
            return true;
        }
    }

    return false;
}

static bool UI_Rml_CompiledRuntimeHandleBackKey(int key);

static bool UI_Rml_MatchHubCanCloseLocally(const Rml::String &command)
{
    return command == "worr_dm_join_close" &&
           !Cvar_VariableInteger("ui_dm_initial") &&
           Cvar_VariableInteger("ui_dm_show_resume");
}

static const char *UI_Rml_RemoteSessionCommandWhenConnected(const char *command)
{
    if (!command ||
        (cls.state >= ca_connected && cls.state <= ca_active)) {
        return command;
    }

    if (!strcmp(command, "worr_dm_join_close") ||
        !strcmp(command, "worr_welcome_continue") ||
        !strcmp(command, "worr_vote_close") ||
        !strcmp(command, "worr_mapselector_close") ||
        !strcmp(command, "worr_matchstats_close")) {
        return nullptr;
    }

    return command;
}

class UI_Rml_CommandEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &event) override
    {
        Rml::Element *conflict_action =
            UI_Rml_FindKeybindConflictAction(event.GetTargetElement());
        if (conflict_action &&
            UI_Rml_HandleKeybindConflictAction(conflict_action)) {
            event.StopPropagation();
            return;
        }

        Rml::Element *keybind_element =
            UI_Rml_FindKeybindElement(event.GetTargetElement());
        if (keybind_element) {
            if (UI_Rml_ElementIsDisabled(keybind_element)) {
                event.StopPropagation();
                return;
            }

            UI_Rml_BeginKeybindCapture(keybind_element);
            UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_MOVE);
            event.StopPropagation();
            return;
        }

        Rml::Element *element = UI_Rml_FindCommandElement(event.GetTargetElement());
        if (!element) {
            return;
        }

        if (UI_Rml_ElementIsDisabled(element)) {
            event.StopPropagation();
            return;
        }

        if (!element->GetAttribute<Rml::String>("data-server-action", "").empty()) {
            UI_Rml_PlayElementMenuSound(
                element,
                element->GetAttribute<Rml::String>("data-server-action", ""),
                "");
            if (UI_Rml_HandleServerBrowserAction(element)) {
                event.StopPropagation();
            }
            return;
        }

        if (!element->GetAttribute<Rml::String>("data-demo-action", "").empty()) {
            UI_Rml_PlayElementMenuSound(
                element,
                element->GetAttribute<Rml::String>("data-demo-action", ""),
                "");
            if (UI_Rml_HandleDemoBrowserAction(element)) {
                event.StopPropagation();
            }
            return;
        }

        Rml::String command =
            element->GetAttribute<Rml::String>("data-command", "");
        const Rml::String command_cvar =
            element->GetAttribute<Rml::String>("data-command-cvar", "");
        if (command.empty() && !command_cvar.empty()) {
            command = Cvar_VariableString(command_cvar.c_str());
        }
        const Rml::String route_target =
            UI_Rml_RouteTargetForElement(element, command);
        UI_Rml_PlayElementMenuSound(element, command, route_target);

        if (command == "ui.popup" ||
            UI_Rml_CommandStartsWithToken(command, "pushpopup")) {
            Rml::String popup_target = route_target;

            if (popup_target.empty()) {
                popup_target = UI_Rml_CommandFirstArgumentAfterToken(command, "pushpopup");
            }

            if (!UI_Rml_QueueRoutePopup(popup_target)) {
                Com_WPrintf("RmlUi popup target '%s' is not a registered route.\n",
                            popup_target.empty() ? "<none>" : popup_target.c_str());
            }

            // Popup tokens are UI-internal; never leak them to the console.
            event.StopPropagation();
            return;
        }

        // The route-target shortcut may only replace a data-command that is
        // exactly a plain 'pushmenu <route>'. Compound commands (cvar seeds,
        // command tails, extra arguments) must run through the console so
        // their side effects execute.
        if (UI_Rml_CommandIsPlainRouteOpen(command, route_target) &&
            UI_Rml_QueueRouteOpen(route_target)) {
            event.StopPropagation();
            return;
        }

        if (command == "ui.close") {
            UI_Rml_QueueCommand("ui_rml_runtime_close");
            event.StopPropagation();
            return;
        }

        if (command == "ui.back") {
            if (!UI_Rml_CompiledRuntimeHandleBackKey(K_ESCAPE)) {
                UI_Rml_QueueCommand("ui_rml_runtime_back");
            }
            event.StopPropagation();
            return;
        }

        if (UI_Rml_CommandStartsWithToken(command, "popmenu")) {
            const char *tail = UI_Rml_CommandTailAfterToken(command, "popmenu");

            UI_Rml_InsertCommandSequence(
                "ui_rml_runtime_back",
                UI_Rml_RemoteSessionCommandWhenConnected(tail));
            event.StopPropagation();
            return;
        }

        if (UI_Rml_CommandStartsWithToken(command, "forcemenuoff")) {
            const char *tail = UI_Rml_CommandTailAfterToken(command, "forcemenuoff");

            UI_Rml_InsertCommandSequence(
                "ui_rml_runtime_close",
                UI_Rml_RemoteSessionCommandWhenConnected(tail));
            event.StopPropagation();
            return;
        }

        if (command == "ui.open_route") {
            event.StopPropagation();
            return;
        }

        // A resumable match hub should disappear immediately while its
        // authoritative sgame close command clears server-side menu state.
        // The first-connect hub remains modal until team/spectator selection.
        if (UI_Rml_MatchHubCanCloseLocally(command)) {
            UI_Rml_InsertCommandSequence("ui_rml_runtime_close",
                                         UI_Rml_RemoteSessionCommandWhenConnected(
                                             command.c_str()));
            event.StopPropagation();
            return;
        }

        if (UI_Rml_QueueCommand(command.c_str())) {
            event.StopPropagation();
        }
    }
};

static UI_Rml_CommandEventListener ui_rml_command_event_listener;

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

    ui_rml_log_missing_data_models =
        Cvar_Get("ui_rml_log_missing_data_models", "0", 0);
    ui_rml_high_visibility =
        Cvar_Get("ui_rml_high_visibility", "0", CVAR_ARCHIVE);
    ui_rml_reduced_motion =
        Cvar_Get("ui_rml_reduced_motion", "0", CVAR_ARCHIVE);
    ui_rml_large_text =
        Cvar_Get("ui_rml_large_text", "0", CVAR_ARCHIVE);
    ui_rml_localization_language =
        Cvar_Get("loc_language", "auto", CVAR_ARCHIVE);
    ui_rml_localization_modified_count = -1;

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

static float UI_Rml_CompiledRuntimeCanvasPixelScale(void)
{
    return UI_Rml_CanvasScale();
}

static float UI_Rml_CompiledRuntimeRendererDrawScale(void)
{
    return UI_Rml_DrawScale();
}

static Rml::Vector2i UI_Rml_CompiledRuntimeCurrentDimensions(void)
{
    const int framebuffer_width =
        r_config.width > 0 ? r_config.width : (int)UI_RML_REFERENCE_WIDTH;
    const int framebuffer_height =
        r_config.height > 0 ? r_config.height : (int)UI_RML_REFERENCE_HEIGHT;
    float scale = UI_Rml_CompiledRuntimeCanvasPixelScale();

    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    return UI_Rml_CompiledRuntimeDimensions(
        max(1, Q_rint((float)framebuffer_width / scale)),
        max(1, Q_rint((float)framebuffer_height / scale)));
}

static bool UI_Rml_CompiledRuntimeEnsureContext(int width, int height)
{
    const Rml::Vector2i dimensions = UI_Rml_CompiledRuntimeDimensions(width, height);

    if (!ui_rml_core_initialized) {
        return false;
    }

#if USE_SDL3_TTF
    // Rasterize glyphs and SVG skins at physical resolution so they stay
    // sharp when the canvas is drawn magnified; re-rasterize on mode change.
    const float font_pixel_scale = UI_Rml_CompiledRuntimeCanvasPixelScale();
    if (fabsf(font_pixel_scale - ui_rml_font_interface.GetPixelScale()) > 0.01f) {
        const bool was_default_scale =
            fabsf(ui_rml_font_interface.GetPixelScale() - 1.0f) < 0.001f &&
            !ui_rml_context;

        ui_rml_font_interface.SetPixelScale(font_pixel_scale);
        Rml::ReleaseFontResources();

        // SVG decorator textures are cached per path at load-time scale.
        if (!was_default_scale) {
            Rml::ReleaseTextures();
        }
    }
#endif

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
    return route_id && route_id[0];
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
    // The engine only reports K_KP_* for the navigation cluster (numlock
    // off), so map them to navigation like the legacy menus did.
    case K_KP_INS:
        return Rml::Input::KI_INSERT;
    case K_KP_END:
        return Rml::Input::KI_END;
    case K_KP_DOWNARROW:
        return Rml::Input::KI_DOWN;
    case K_KP_PGDN:
        return Rml::Input::KI_NEXT;
    case K_KP_LEFTARROW:
        return Rml::Input::KI_LEFT;
    case K_KP_5:
        return Rml::Input::KI_NUMPAD5;
    case K_KP_RIGHTARROW:
        return Rml::Input::KI_RIGHT;
    case K_KP_HOME:
        return Rml::Input::KI_HOME;
    case K_KP_UPARROW:
        return Rml::Input::KI_UP;
    case K_KP_PGUP:
        return Rml::Input::KI_PRIOR;
    case K_KP_DEL:
        return Rml::Input::KI_DELETE;
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
static void UI_Rml_CompiledRuntimeCloseActiveDocument(void);

static Rml::ElementFormControlSelect *UI_Rml_SelectFromElement(Rml::Element *element)
{
    return rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(element);
}

// Expands select controls marked data-source-list="$$com_maplist" with the
// installed map list, mirroring the legacy JSON menu macro expansion.
static void UI_Rml_ExpandSourceLists(Rml::ElementDocument *document)
{
    Rml::ElementList elements;
    document->QuerySelectorAll(elements, "select[data-source-list]");

    for (Rml::Element *element : elements) {
        const Rml::String source =
            element->GetAttribute<Rml::String>("data-source-list", "");
        Rml::ElementFormControlSelect *select = UI_Rml_SelectFromElement(element);

        if (!select) {
            continue;
        }

        if (source == "$$r_modelist") {
            // Fullscreen video modes: r_fullscreen selects a 1-based index
            // into the r_modelist cvar; value 0 (windowed) stays authored.
            const char *modes = Cvar_VariableString("r_modelist");
            char token[64];
            int mode_index = 1;

            while (select->GetNumOptions() > 1) {
                select->Remove(select->GetNumOptions() - 1);
            }

            while (*modes) {
                size_t token_len = 0;

                while (*modes == ' ' || *modes == '\t') {
                    modes++;
                }
                while (*modes && *modes != ' ' && *modes != '\t' &&
                       token_len < sizeof(token) - 1) {
                    token[token_len++] = *modes++;
                }
                token[token_len] = '\0';

                if (!token_len) {
                    break;
                }

                select->Add(UI_Rml_EscapeTextForInnerRml(token),
                            va("%d", mode_index));
                mode_index++;
            }
            continue;
        }

        if (source != "$$com_maplist") {
            Com_WPrintf("RmlUi data-source-list '%s' is not supported.\n",
                        source.c_str());
            continue;
        }

        int num_files = 0;
        void **files = FS_ListFiles("maps", ".bsp", FS_SEARCH_STRIPEXT, &num_files);
        if (!files || num_files <= 0) {
            if (files) {
                FS_FreeList(files);
            }
            continue;
        }

        const mapdb_t *mapdb = MapDB_Get();

        select->RemoveAll();
        for (int i = 0; i < num_files; i++) {
            const char *bsp = static_cast<const char *>(files[i]);
            Rml::String label = UI_Rml_EscapeTextForInnerRml(bsp);

            // Show the friendly title when the map database knows the map.
            if (mapdb) {
                for (size_t m = 0; m < mapdb->num_maps; m++) {
                    if (!Q_stricmp(mapdb->maps[m].bsp, bsp) &&
                        mapdb->maps[m].title[0]) {
                        label += " - ";
                        label += UI_Rml_EscapeTextForInnerRml(mapdb->maps[m].title);
                        break;
                    }
                }
            }

            select->Add(label, bsp);
        }

        FS_FreeList(files);

        // Seed the bound cvar from the first option when it is still empty:
        // commands like 'map $_ui_nextserver force' must not macro-collapse
        // when the user never touches the select.
        const Rml::String bound_cvar =
            element->GetAttribute<Rml::String>("data-cvar", "");
        if (!bound_cvar.empty() && select->GetNumOptions() > 0) {
            cvar_t *var = Cvar_WeakGet(bound_cvar.c_str());

            if (var && !var->string[0]) {
                if (Rml::Element *option = select->GetOption(0)) {
                    const Rml::String value =
                        option->GetAttribute<Rml::String>("value", "");

                    if (!value.empty()) {
                        Cvar_SetByVar(var, value.c_str(), FROM_MENU);
                    }
                }
            }
        }
    }
}

static void UI_Rml_SetSelectPlaceholderLabel(Rml::ElementFormControlSelect *select,
                                             const char *label)
{
    if (!select || select->GetNumOptions() < 1) {
        return;
    }

    if (Rml::Element *option = select->GetOption(0)) {
        if (option->GetAttribute<Rml::String>("value", "").empty()) {
            UI_Rml_SetElementInnerText(option, label);
        }
    }
}

// Populates the single-player episode/level selects from the map database.
// The authored empty-value placeholder stays as the first option so the
// visible selection matches the '-1' (nothing chosen) cvar sentinel.
static void UI_Rml_PopulateMapDbSelects(Rml::ElementDocument *document)
{
    const mapdb_t *mapdb = MapDB_Get();
    if (!mapdb) {
        return;
    }

    if (Rml::ElementFormControlSelect *episodes = UI_Rml_SelectFromElement(
            document->QuerySelector("select[data-model='singleplayer.episodes']"))) {
        while (episodes->GetNumOptions() > 1) {
            episodes->Remove(episodes->GetNumOptions() - 1);
        }

        if (mapdb->num_episodes) {
            UI_Rml_SetSelectPlaceholderLabel(episodes, "Select an episode...");
            for (size_t i = 0; i < mapdb->num_episodes; i++) {
                episodes->Add(UI_Rml_EscapeTextForInnerRml(mapdb->episodes[i].name),
                              va("%zu", i));
            }
        } else {
            UI_Rml_SetSelectPlaceholderLabel(episodes, "No campaigns found");
        }
    }

    if (Rml::ElementFormControlSelect *units = UI_Rml_SelectFromElement(
            document->QuerySelector("select[data-model='singleplayer.units']"))) {
        size_t num_sp_maps = 0;

        while (units->GetNumOptions() > 1) {
            units->Remove(units->GetNumOptions() - 1);
        }

        for (size_t i = 0; i < mapdb->num_maps; i++) {
            if (!mapdb->maps[i].sp) {
                continue;
            }

            // Option values are indices into mapdb->maps, matching what
            // MapDB_Run_f expects from _mapdb_level.
            units->Add(UI_Rml_EscapeTextForInnerRml(mapdb->maps[i].title),
                       va("%zu", i));
            num_sp_maps++;
        }

        UI_Rml_SetSelectPlaceholderLabel(units,
                                         num_sp_maps ? "Select a level..."
                                                     : "No levels found");
    }
}

// Populates imagevalues pickers (crosshair) by enumerating pics on disk.
static void UI_Rml_PopulateImageValueSelects(Rml::ElementDocument *document)
{
    Rml::ElementList elements;
    document->QuerySelectorAll(elements, "select[data-source-menu-type='imagevalues']");

    for (Rml::Element *element : elements) {
        Rml::ElementFormControlSelect *select = UI_Rml_SelectFromElement(element);
        Rml::Element *row = element->GetParentNode();
        while (row && !row->HasAttribute("data-path")) {
            row = row->GetParentNode();
        }

        if (!select || !row) {
            continue;
        }

        const Rml::String path =
            row->GetAttribute<Rml::String>("data-path", "pics");
        const Rml::String filter =
            row->GetAttribute<Rml::String>("data-filter", "");
        const Rml::String value_prefix =
            row->GetAttribute<Rml::String>("data-value-prefix", "");

        int num_files = 0;
        void **files = FS_ListFiles(path.c_str(),
                                    filter.empty() ? nullptr : filter.c_str(),
                                    FS_SEARCH_STRIPEXT | FS_SEARCH_BYFILTER,
                                    &num_files);
        if (!files || num_files <= 0) {
            if (files) {
                FS_FreeList(files);
            }
            continue;
        }

        std::vector<int> values;
        for (int i = 0; i < num_files; i++) {
            const char *name = static_cast<const char *>(files[i]);
            const char *stem = strrchr(name, '/');
            stem = stem ? stem + 1 : name;

            if (!value_prefix.empty() &&
                Q_stricmpn(stem, value_prefix.c_str(), value_prefix.size())) {
                continue;
            }

            const char *digits = stem + value_prefix.size();
            if (!digits[0]) {
                continue;
            }

            char *end = nullptr;
            const long value = strtol(digits, &end, 10);
            if (!end || *end || value <= 0) {
                continue;
            }

            values.push_back(static_cast<int>(value));
        }
        FS_FreeList(files);

        if (values.empty()) {
            continue;
        }

        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());

        // Keep the authored leading option (value 0 / Default) and append
        // the enumerated set after it.
        while (select->GetNumOptions() > 1) {
            select->Remove(select->GetNumOptions() - 1);
        }

        for (const int value : values) {
            select->Add(va("%s%d", value_prefix.c_str(), value), va("%d", value));
        }
    }
}

// ---- Server browser: live legacy discovery with native RmlUi presentation ----

static constexpr size_t UI_RML_SERVER_PAGE_SIZE = 8;
static constexpr size_t UI_RML_SERVER_MAX_ENTRIES = 1024;
static constexpr int UI_RML_SERVER_PING_STAGES = 3;
static constexpr const char *UI_RML_SERVER_PUBLIC_ARGUMENTS =
    "\"+https://q2servers.com/?raw=2\" \"https://q2servers.com/?raw=0\"";
static constexpr const char *UI_RML_SERVER_LOCAL_ARGUMENTS =
    "\"favorites://\" \"file:///servers.lst\" \"broadcast://\"";

enum class UI_Rml_ServerStatus {
    Idle,
    Pending,
    Error,
    Valid,
};

enum UI_Rml_ServerSortColumn {
    UI_RML_SERVER_SORT_NAME = 0,
    UI_RML_SERVER_SORT_MOD,
    UI_RML_SERVER_SORT_MAP,
    UI_RML_SERVER_SORT_PLAYERS,
    UI_RML_SERVER_SORT_RTT,
    UI_RML_SERVER_SORT_COUNT,
};

struct UI_Rml_ServerEntry {
    UI_Rml_ServerStatus status = UI_Rml_ServerStatus::Idle;
    netadr_t address = {};
    Rml::String source_name;
    Rml::String name;
    Rml::String mod = "???";
    Rml::String map = "???";
    Rml::String players = "?/?";
    Rml::String rtt = "???";
    int num_players = 0;
    int max_players = 0;
    unsigned timestamp = 0;
    unsigned ping = 999;
    bool restricted = false;
};

static std::vector<UI_Rml_ServerEntry> ui_rml_server_entries;
static Rml::String ui_rml_server_explicit_arguments;
static Rml::String ui_rml_server_selected_address;
static Rml::String ui_rml_server_status_text;
static size_t ui_rml_server_page;
static size_t ui_rml_server_ping_index;
static int ui_rml_server_ping_stage;
static unsigned ui_rml_server_ping_interval;
static unsigned ui_rml_server_next_ping;
static unsigned ui_rml_server_refresh_timestamp;
static bool ui_rml_server_refreshing;
static bool ui_rml_server_dirty;

static bool UI_Rml_ServerAddressEqual(const netadr_t &left,
                                      const netadr_t &right)
{
    return NET_IsEqualBaseAdr(&left, &right) && left.port == right.port;
}

static Rml::String UI_Rml_ServerAddressString(const netadr_t &address)
{
    return NET_AdrToString(&address);
}

static int UI_Rml_ServerStatusRank(UI_Rml_ServerStatus status)
{
    switch (status) {
    case UI_Rml_ServerStatus::Valid: return 0;
    case UI_Rml_ServerStatus::Pending: return 1;
    case UI_Rml_ServerStatus::Idle: return 2;
    case UI_Rml_ServerStatus::Error: return 3;
    }
    return 4;
}

static UI_Rml_ServerEntry *UI_Rml_FindServerEntry(const netadr_t &address)
{
    for (UI_Rml_ServerEntry &entry : ui_rml_server_entries) {
        if (UI_Rml_ServerAddressEqual(entry.address, address)) {
            return &entry;
        }
    }
    return nullptr;
}

static const UI_Rml_ServerEntry *UI_Rml_SelectedServerEntry(void)
{
    for (const UI_Rml_ServerEntry &entry : ui_rml_server_entries) {
        if (!ui_rml_server_selected_address.empty() &&
            UI_Rml_ServerAddressString(entry.address) ==
                ui_rml_server_selected_address) {
            return &entry;
        }
    }
    return nullptr;
}

static bool UI_Rml_IsLikelyTextServerList(const char *data, size_t length)
{
    if (!data || !length) {
        return false;
    }
    const size_t checked = min(length, static_cast<size_t>(256));
    bool digit = false;
    bool separator = false;
    bool newline = false;
    for (size_t index = 0; index < checked; index++) {
        const unsigned char character = static_cast<unsigned char>(data[index]);
        if (character == '\n' || character == '\r') {
            newline = true;
            continue;
        }
        if (character < 32 || character > 126) {
            return false;
        }
        digit = digit || (character >= '0' && character <= '9');
        separator = separator || character == '.' || character == ':';
    }
    return digit && separator && (newline || length < 64);
}

static bool UI_Rml_AddServerEntry(const netadr_t *address,
                                  const char *hostname)
{
    netadr_t resolved = {};
    if (ui_rml_server_entries.size() >= UI_RML_SERVER_MAX_ENTRIES) {
        return false;
    }
    if (!address) {
        if (!hostname || !hostname[0] ||
            !NET_StringToAdr(hostname, &resolved, PORT_SERVER)) {
            if (hostname && hostname[0]) {
                Com_WPrintf("RmlUi server browser ignored invalid address '%s'.\n",
                            hostname);
            }
            return false;
        }
        address = &resolved;
    }
    if (UI_Rml_FindServerEntry(*address)) {
        return false;
    }
    if (BigShort(address->port) < 1024) {
        Com_WPrintf("RmlUi server browser ignored privileged port '%s'.\n",
                    hostname && hostname[0] ? hostname
                                            : NET_AdrToString(address));
        return false;
    }

    UI_Rml_ServerEntry entry;
    entry.address = *address;
    entry.source_name = hostname && hostname[0]
                            ? hostname
                            : NET_AdrToString(address);
    entry.name = entry.source_name;
    entry.timestamp = Sys_Milliseconds();
    ui_rml_server_entries.push_back(std::move(entry));
    return true;
}

static void UI_Rml_ParsePlainServerList(const void *data, size_t length)
{
    if (!data || !length) {
        return;
    }
    const char *bytes = static_cast<const char *>(data);
    size_t begin = 0;
    while (begin < length) {
        size_t end = begin;
        while (end < length && bytes[end] != '\n' && bytes[end] != '\0') {
            end++;
        }
        size_t trimmed_end = end;
        while (trimmed_end > begin &&
               (bytes[trimmed_end - 1] == '\r' || bytes[trimmed_end - 1] == ' ' ||
                bytes[trimmed_end - 1] == '\t')) {
            trimmed_end--;
        }
        size_t trimmed_begin = begin;
        while (trimmed_begin < trimmed_end &&
               (bytes[trimmed_begin] == ' ' || bytes[trimmed_begin] == '\t')) {
            trimmed_begin++;
        }
        if (trimmed_end > trimmed_begin) {
            const std::string hostname(bytes + trimmed_begin,
                                       trimmed_end - trimmed_begin);
            UI_Rml_AddServerEntry(nullptr, hostname.c_str());
        }
        begin = end + 1;
    }
}

static void UI_Rml_ParseBinaryServerList(const void *data,
                                         size_t length,
                                         size_t chunk)
{
    if (!data || chunk < 6) {
        return;
    }
    const byte *bytes = static_cast<const byte *>(data);
    while (length >= chunk) {
        netadr_t address = {};
        address.type = NA_IP;
        memcpy(address.ip.u8, bytes, 4);
        memcpy(&address.port, bytes + 4, 2);
        UI_Rml_AddServerEntry(&address, nullptr);
        bytes += chunk;
        length -= chunk;
    }
}

static void UI_Rml_ParseServerAddressBook(void)
{
    for (size_t index = 0; index < UI_RML_SERVER_MAX_ENTRIES; index++) {
        cvar_t *address = Cvar_FindVar(va("adr%zu", index));
        if (!address) {
            break;
        }
        if (address->string[0]) {
            UI_Rml_AddServerEntry(nullptr, address->string);
        }
    }
}

static bool UI_Rml_ServerArgumentsArePublic(const Rml::String &arguments)
{
    return arguments.find("q2servers.com") != Rml::String::npos;
}

static bool UI_Rml_ServerArgumentsAreLocal(const Rml::String &arguments)
{
    return arguments.find("favorites://") != Rml::String::npos ||
           arguments.find("broadcast://") != Rml::String::npos ||
           arguments.find("servers.lst") != Rml::String::npos;
}

static const char *UI_Rml_ServerSourceName(void)
{
    if (!ui_rml_server_explicit_arguments.empty()) {
        if (UI_Rml_ServerArgumentsArePublic(ui_rml_server_explicit_arguments)) {
            return "Public";
        }
        if (UI_Rml_ServerArgumentsAreLocal(ui_rml_server_explicit_arguments)) {
            return "Saved + LAN";
        }
        return "Custom";
    }
    return !Q_stricmp(Cvar_VariableString("ui_server_source"), "public")
               ? "Public"
               : "Saved + LAN";
}

static Rml::String UI_Rml_CurrentServerArguments(void)
{
    if (!ui_rml_server_explicit_arguments.empty()) {
        return ui_rml_server_explicit_arguments;
    }
    return !Q_stricmp(Cvar_VariableString("ui_server_source"), "public")
               ? UI_RML_SERVER_PUBLIC_ARGUMENTS
               : UI_RML_SERVER_LOCAL_ARGUMENTS;
}

static bool UI_Rml_ParseServerSources(const Rml::String &arguments,
                                      netadr_t *broadcast)
{
    bool source_seen = false;
    Cmd_TokenizeString(arguments.c_str(), false);

    for (int index = 0; index < Cmd_Argc(); index++) {
        const char *token = Cmd_Argv(index);
        if (!token || !token[0]) {
            continue;
        }

        bool binary = false;
        size_t chunk = 0;
        const char *source = token;
        if (*source == '+' || *source == '-') {
            char *end = nullptr;
            chunk = strtoul(source, &end, 10);
            if (source == end) {
                chunk = 6;
                source++;
            } else {
                source = end;
            }
            if (chunk < 6) {
                Com_WPrintf("RmlUi server browser ignored invalid binary source '%s'.\n",
                            token);
                continue;
            }
            binary = true;
        }

        if (!strncmp(source, "favorites://", 12)) {
            UI_Rml_ParseServerAddressBook();
            source_seen = true;
            continue;
        }
        if (!strncmp(source, "broadcast://", 12)) {
            broadcast->type = NA_BROADCAST;
            broadcast->port = BigShort(PORT_SERVER);
            source_seen = true;
            continue;
        }
        if (!strncmp(source, "quake2://", 9)) {
            UI_Rml_AddServerEntry(nullptr, source + 9);
            source_seen = true;
            continue;
        }

        void *data = nullptr;
        int length = -1;
        bool http_data = false;
        if (!strncmp(source, "file://", 7)) {
            length = FS_LoadFile(source + 7, &data);
        } else if (!strncmp(source, "http://", 7) ||
                   !strncmp(source, "https://", 8)) {
#if USE_CURL
            length = HTTP_FetchFile(source, &data);
            http_data = true;
#else
            Com_WPrintf("RmlUi server browser cannot fetch '%s': HTTP support is disabled.\n",
                        source);
#endif
        } else {
            Com_WPrintf("RmlUi server browser ignored source '%s'.\n", source);
            continue;
        }

        if (length >= 0 && data) {
            const size_t data_length = static_cast<size_t>(length);
            const bool parse_text = !binary || !chunk ||
                                    data_length % chunk != 0 ||
                                    UI_Rml_IsLikelyTextServerList(
                                        static_cast<const char *>(data),
                                        data_length);
            if (parse_text) {
                UI_Rml_ParsePlainServerList(data, data_length);
            } else {
                UI_Rml_ParseBinaryServerList(data, data_length, chunk);
            }
            source_seen = true;
        }

        if (data) {
            if (http_data) {
#if USE_CURL
                HTTP_FreeFile(data);
#endif
            } else {
                FS_FreeFile(data);
            }
        }
    }
    return source_seen;
}

static int UI_Rml_ServerCompareText(const Rml::String &left,
                                    const Rml::String &right)
{
    return Q_stricmp(left.c_str(), right.c_str());
}

static void UI_Rml_SortServerEntries(void)
{
    int encoded = Q_clip(Cvar_VariableInteger("ui_sortservers"),
                         -UI_RML_SERVER_SORT_COUNT,
                         UI_RML_SERVER_SORT_COUNT);
    const int direction = encoded < 0 ? -1 : 1;
    const int column = encoded ? abs(encoded) - 1 : -1;

    std::stable_sort(
        ui_rml_server_entries.begin(), ui_rml_server_entries.end(),
        [column, direction](const UI_Rml_ServerEntry &left,
                            const UI_Rml_ServerEntry &right) {
            const int left_rank = UI_Rml_ServerStatusRank(left.status);
            const int right_rank = UI_Rml_ServerStatusRank(right.status);
            if (left_rank != right_rank) {
                return left_rank < right_rank;
            }

            int comparison = 0;
            switch (column) {
            case UI_RML_SERVER_SORT_MOD:
                comparison = UI_Rml_ServerCompareText(left.mod, right.mod);
                break;
            case UI_RML_SERVER_SORT_MAP:
                comparison = UI_Rml_ServerCompareText(left.map, right.map);
                break;
            case UI_RML_SERVER_SORT_PLAYERS:
                comparison = right.num_players - left.num_players;
                break;
            case UI_RML_SERVER_SORT_RTT:
                comparison = left.ping < right.ping
                                 ? -1
                                 : left.ping > right.ping ? 1 : 0;
                break;
            case UI_RML_SERVER_SORT_NAME:
                comparison = UI_Rml_ServerCompareText(left.name, right.name);
                break;
            default:
                break;
            }
            comparison *= direction;
            if (comparison) {
                return comparison < 0;
            }
            comparison = UI_Rml_ServerCompareText(left.name, right.name);
            if (comparison) {
                return comparison < 0;
            }
            return UI_Rml_ServerAddressString(left.address) <
                   UI_Rml_ServerAddressString(right.address);
        });
}

static void UI_Rml_UpdateServerStatusText(void)
{
    int valid_servers = 0;
    int total_players = 0;
    for (const UI_Rml_ServerEntry &entry : ui_rml_server_entries) {
        if (entry.status == UI_Rml_ServerStatus::Valid) {
            valid_servers++;
            total_players += entry.num_players;
        }
    }

    if (ui_rml_server_refreshing) {
        ui_rml_server_status_text = va(
            "Querying %zu server%s: %d replied, %d player%s found.",
            ui_rml_server_entries.size(),
            ui_rml_server_entries.size() == 1 ? "" : "s",
            valid_servers,
            total_players,
            total_players == 1 ? "" : "s");
    } else if (valid_servers) {
        ui_rml_server_status_text = va(
            "%d player%s on %d server%s. Select a row, then Connect.",
            total_players,
            total_players == 1 ? "" : "s",
            valid_servers,
            valid_servers == 1 ? "" : "s");
    } else if (!ui_rml_server_entries.empty()) {
        ui_rml_server_status_text = va(
            "No replies from %zu server%s. Refresh or switch sources.",
            ui_rml_server_entries.size(),
            ui_rml_server_entries.size() == 1 ? "" : "s");
    } else {
        ui_rml_server_status_text =
            "No servers found. Refresh or switch between Public and Saved + LAN.";
    }
}

static void UI_Rml_SetServerControlDisabled(Rml::ElementDocument *document,
                                            const char *id,
                                            bool disabled)
{
    if (Rml::Element *element = document ? document->GetElementById(id) : nullptr) {
        if (disabled) {
            element->SetAttribute("disabled", "");
        } else {
            element->RemoveAttribute("disabled");
        }
    }
}

static const char *UI_Rml_ServerStatusClass(UI_Rml_ServerStatus status)
{
    switch (status) {
    case UI_Rml_ServerStatus::Pending: return " is-pending";
    case UI_Rml_ServerStatus::Error: return " is-error";
    case UI_Rml_ServerStatus::Valid: return " is-valid";
    default: return "";
    }
}

static void UI_Rml_RenderServerBrowser(Rml::ElementDocument *document,
                                       bool attach_dynamic_listeners = false)
{
    Rml::Element *body = document
                             ? document->GetElementById("servers-table-body")
                             : nullptr;
    if (!body) {
        return;
    }

    UI_Rml_SortServerEntries();
    const size_t page_count = ui_rml_server_entries.empty()
                                  ? 1
                                  : (ui_rml_server_entries.size() +
                                     UI_RML_SERVER_PAGE_SIZE - 1) /
                                        UI_RML_SERVER_PAGE_SIZE;
    if (ui_rml_server_page >= page_count) {
        ui_rml_server_page = page_count - 1;
    }
    const size_t first = ui_rml_server_page * UI_RML_SERVER_PAGE_SIZE;
    const size_t last = min(first + UI_RML_SERVER_PAGE_SIZE,
                            ui_rml_server_entries.size());

    Rml::String rows;
    if (first >= last) {
        rows = "<tr class=\"placeholder-row\"><td colspan=\"5\">"
               "No servers found for this source.</td></tr>";
    } else {
        for (size_t index = first; index < last; index++) {
            const UI_Rml_ServerEntry &entry = ui_rml_server_entries[index];
            const Rml::String address = UI_Rml_ServerAddressString(entry.address);
            rows += "<tr id=\"server-row-";
            rows += std::to_string(index);
            rows += "\" class=\"server-row";
            rows += UI_Rml_ServerStatusClass(entry.status);
            if (address == ui_rml_server_selected_address) {
                rows += " is-selected";
            }
            if (entry.restricted) {
                rows += " is-restricted";
            }
            rows += "\"><td><button class=\"data-table-row-action\" "
                    "type=\"button\" data-server-action=\"select\" "
                    "data-server-index=\"";
            rows += std::to_string(index);
            rows += "\" data-menu-sound=\"move\">";
            rows += UI_Rml_EscapeTextForInnerRml(entry.name.c_str());
            rows += "</button></td><td>";
            rows += UI_Rml_EscapeTextForInnerRml(entry.mod.c_str());
            rows += "</td><td>";
            rows += UI_Rml_EscapeTextForInnerRml(entry.map.c_str());
            rows += "</td><td>";
            rows += UI_Rml_EscapeTextForInnerRml(entry.players.c_str());
            rows += "</td><td>";
            rows += UI_Rml_EscapeTextForInnerRml(entry.rtt.c_str());
            rows += "</td></tr>";
        }
    }
    body->SetInnerRML(rows);

    const UI_Rml_ServerEntry *selected = UI_Rml_SelectedServerEntry();
    UI_Rml_SetServerControlDisabled(document, "servers-connect",
                                    !selected ||
                                        selected->status == UI_Rml_ServerStatus::Error);
    UI_Rml_SetServerControlDisabled(document, "servers-previous",
                                    ui_rml_server_page == 0);
    UI_Rml_SetServerControlDisabled(document, "servers-next",
                                    ui_rml_server_page + 1 >= page_count);

    if (Rml::Element *source = document->GetElementById("servers-source")) {
        UI_Rml_SetElementInnerText(source,
                                   va("Source: %s", UI_Rml_ServerSourceName()));
    }
    if (Rml::Element *page = document->GetElementById("servers-page-label")) {
        UI_Rml_SetElementInnerText(
            page, va("Page %zu / %zu", ui_rml_server_page + 1, page_count));
    }
    if (Rml::Element *summary = document->GetElementById("servers-summary")) {
        UI_Rml_SetElementInnerText(
            summary, va("Browse %s multiplayer servers.",
                        UI_Rml_ServerSourceName()));
    }
    UI_Rml_UpdateServerStatusText();
    if (Rml::Element *status = document->GetElementById("servers-status")) {
        UI_Rml_SetElementInnerText(status, ui_rml_server_status_text.c_str());
        status->SetClass("is-ready", !ui_rml_server_entries.empty());
    }

    static const char *header_ids[UI_RML_SERVER_SORT_COUNT] = {
        "servers-sort-name", "servers-sort-mod", "servers-sort-map",
        "servers-sort-players", "servers-sort-rtt"
    };
    const int encoded = Cvar_VariableInteger("ui_sortservers");
    const int active_sort = encoded ? abs(encoded) - 1 : -1;
    for (int column = 0; column < UI_RML_SERVER_SORT_COUNT; column++) {
        if (Rml::Element *header = document->GetElementById(header_ids[column])) {
            if (column == active_sort) {
                header->SetAttribute("data-sort-state",
                                     encoded > 0 ? "ascending" : "descending");
            } else {
                header->RemoveAttribute("data-sort-state");
            }
        }
    }

    if (attach_dynamic_listeners) {
        UI_Rml_AttachElementAudioListeners(body);
    }
    ui_rml_server_dirty = false;
}

static void UI_Rml_CalculateServerPingInterval(void)
{
    cvar_t *ping_rate = Cvar_Get("ui_pingrate", "0", 0);
    int rate = ping_rate ? Q_clip(ping_rate->integer, 0, 100) : 0;
    if (!rate) {
        cvar_t *network_rate = Cvar_FindVar("rate");
        rate = Q_clip(network_rate ? network_rate->integer / 450 : 33, 1, 100);
    }
    ui_rml_server_ping_interval = max(
        1, (1000 * UI_RML_SERVER_PING_STAGES) /
               max(1, rate * max(1, ui_rml_server_ping_stage)));
}

static void UI_Rml_FinishServerRefresh(void)
{
    ui_rml_server_ping_stage = 0;
    ui_rml_server_ping_index = 0;
    ui_rml_server_refreshing = false;
    ui_rml_server_dirty = true;
    UI_Rml_UpdateServerStatusText();
}

static void UI_Rml_StartServerRefresh(Rml::ElementDocument *document)
{
    Cvar_Get("ui_sortservers", "1", 0);
    Cvar_Get("ui_server_source", "local", 0);
    ui_rml_server_entries.clear();
    ui_rml_server_selected_address.clear();
    ui_rml_server_page = 0;
    ui_rml_server_ping_index = 0;
    ui_rml_server_ping_stage = 0;
    ui_rml_server_refreshing = true;
    ui_rml_server_status_text = "Resolving server sources...";

    netadr_t broadcast = {};
    const Rml::String arguments = UI_Rml_CurrentServerArguments();
    const bool source_seen = UI_Rml_ParseServerSources(arguments, &broadcast);
    ui_rml_server_refresh_timestamp = Sys_Milliseconds();
    for (UI_Rml_ServerEntry &entry : ui_rml_server_entries) {
        entry.timestamp = ui_rml_server_refresh_timestamp;
    }

    if (broadcast.type != NA_UNSPECIFIED) {
        CL_SendStatusRequest(&broadcast);
    }
    if (!source_seen || ui_rml_server_entries.empty()) {
        UI_Rml_FinishServerRefresh();
    } else {
        ui_rml_server_ping_stage = UI_RML_SERVER_PING_STAGES;
        UI_Rml_CalculateServerPingInterval();
        ui_rml_server_next_ping = Sys_Milliseconds();
    }
    UI_Rml_RenderServerBrowser(document, false);
}

static void UI_Rml_InitializeServerBrowser(Rml::ElementDocument *document)
{
    if (!document || !document->GetElementById("servers-table-body")) {
        return;
    }

    const char *arguments = UI_Rml_RouteArguments();
    ui_rml_server_explicit_arguments =
        arguments && arguments[0] ? arguments : "";
    cvar_t *source = Cvar_Get("ui_server_source", "local", 0);
    if (!ui_rml_server_explicit_arguments.empty()) {
        if (UI_Rml_ServerArgumentsArePublic(ui_rml_server_explicit_arguments)) {
            Cvar_SetByVar(source, "public", FROM_MENU);
        } else if (UI_Rml_ServerArgumentsAreLocal(ui_rml_server_explicit_arguments)) {
            Cvar_SetByVar(source, "local", FROM_MENU);
        }
    }
    UI_Rml_StartServerRefresh(document);
}

static void UI_Rml_AdvanceServerRefresh(unsigned realtime)
{
    (void)realtime;
    const unsigned now = Sys_Milliseconds();

    if (!ui_rml_server_refreshing || ui_rml_server_ping_stage <= 0 ||
        static_cast<int>(now - ui_rml_server_next_ping) < 0) {
        return;
    }

    while (ui_rml_server_ping_index < ui_rml_server_entries.size()) {
        UI_Rml_ServerEntry &entry =
            ui_rml_server_entries[ui_rml_server_ping_index++];
        if (entry.status == UI_Rml_ServerStatus::Valid ||
            entry.status == UI_Rml_ServerStatus::Error) {
            continue;
        }
        entry.status = UI_Rml_ServerStatus::Pending;
        entry.timestamp = Sys_Milliseconds();
        CL_SendStatusRequest(&entry.address);
        ui_rml_server_dirty = true;
        break;
    }

    if (ui_rml_server_ping_index >= ui_rml_server_entries.size()) {
        ui_rml_server_ping_index = 0;
        if (--ui_rml_server_ping_stage <= 0) {
            UI_Rml_FinishServerRefresh();
        } else {
            UI_Rml_CalculateServerPingInterval();
        }
    }
    ui_rml_server_next_ping = now + ui_rml_server_ping_interval;
}

static bool UI_Rml_ServerStatusEvent(const serverStatus_t *status)
{
    if (!status || ui_rml_active_route != "servers") {
        return false;
    }

    UI_Rml_ServerEntry *entry = UI_Rml_FindServerEntry(net_from);
    if (!entry) {
        if (!UI_Rml_AddServerEntry(&net_from, nullptr)) {
            return true;
        }
        entry = &ui_rml_server_entries.back();
        entry->timestamp = ui_rml_server_refresh_timestamp;
    }

    const unsigned now = Sys_Milliseconds();
    entry->ping = min(now - min(entry->timestamp, now), 999u);
    entry->status = UI_Rml_ServerStatus::Valid;
    entry->rtt = va("%u", entry->ping);
    entry->num_players = Q_clip(status->numPlayers, 0, MAX_STATUS_PLAYERS);

    const char *value = Info_ValueForKey(status->infostring, "hostname");
    entry->name = value && !COM_IsWhite(value) ? value : entry->source_name;
    value = Info_ValueForKey(status->infostring, "game");
    entry->mod = value && !COM_IsWhite(value) ? value : BASEGAME;
    value = Info_ValueForKey(status->infostring, "mapname");
    entry->map = value && !COM_IsWhite(value) ? value : "???";
    value = Info_ValueForKey(status->infostring, "maxclients");
    entry->max_players = value && COM_IsUint(value) ? Q_atoi(value) : 0;
    entry->players = entry->max_players > 0
                         ? va("%d/%d", entry->num_players, entry->max_players)
                         : va("%d/?", entry->num_players);
    entry->restricted =
        Q_atoi(Info_ValueForKey(status->infostring, "needpass")) >= 1 ||
        Q_atoi(Info_ValueForKey(status->infostring, "anticheat")) >= 2 ||
        !Q_stricmp(Info_ValueForKey(status->infostring, "NoFake"), "ENABLED");
    ui_rml_server_dirty = true;
    return true;
}

static bool UI_Rml_ServerErrorEvent(const netadr_t *from)
{
    if (!from || ui_rml_active_route != "servers") {
        return false;
    }
    UI_Rml_ServerEntry *entry = UI_Rml_FindServerEntry(*from);
    if (!entry || entry->status != UI_Rml_ServerStatus::Pending) {
        return true;
    }
    const unsigned now = Sys_Milliseconds();
    entry->ping = min(now - min(entry->timestamp, now), 999u);
    entry->status = UI_Rml_ServerStatus::Error;
    entry->mod = "???";
    entry->map = "???";
    entry->players = "down";
    entry->rtt = va("%u", entry->ping);
    ui_rml_server_dirty = true;
    return true;
}

static bool UI_Rml_ConnectSelectedServer(void)
{
    const UI_Rml_ServerEntry *entry = UI_Rml_SelectedServerEntry();
    if (!entry || entry->status == UI_Rml_ServerStatus::Error) {
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_ALERT);
        return true;
    }

    const Rml::String address = UI_Rml_ServerAddressString(entry->address);
    if (address.empty() || address.find_first_of("\n\r\";") != Rml::String::npos) {
        Com_WPrintf("RmlUi rejected an unsafe server address.\n");
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_ALERT);
        return true;
    }
    return UI_Rml_QueueCommand(va("connect \"%s\"", address.c_str()));
}

static void UI_Rml_SelectServerEntry(size_t index)
{
    if (index >= ui_rml_server_entries.size() || !ui_rml_document) {
        return;
    }

    const Rml::String address =
        UI_Rml_ServerAddressString(ui_rml_server_entries[index].address);
    const bool activate_selected = address == ui_rml_server_selected_address;
    ui_rml_server_selected_address = address;

    Rml::ElementList rows;
    ui_rml_document->QuerySelectorAll(rows, ".server-row");
    const Rml::String selected_id = va("server-row-%zu", index);
    for (Rml::Element *row : rows) {
        row->SetClass("is-selected", row->GetId() == selected_id);
    }
    UI_Rml_SetServerControlDisabled(
        ui_rml_document, "servers-connect",
        ui_rml_server_entries[index].status == UI_Rml_ServerStatus::Error);
    if (Rml::Element *status = ui_rml_document->GetElementById("servers-status")) {
        UI_Rml_SetElementInnerText(
            status,
            va("Selected %s (%s). Activate again or choose Connect.",
               ui_rml_server_entries[index].name.c_str(), address.c_str()));
    }

    if (activate_selected) {
        UI_Rml_ConnectSelectedServer();
    }
}

static bool UI_Rml_HandleServerBrowserAction(Rml::Element *element)
{
    if (!element || !ui_rml_document || ui_rml_active_route != "servers") {
        return false;
    }

    const Rml::String action =
        element->GetAttribute<Rml::String>("data-server-action", "");
    if (action == "refresh") {
        UI_Rml_StartServerRefresh(ui_rml_document);
        return true;
    }
    if (action == "toggle-source") {
        cvar_t *source = Cvar_Get("ui_server_source", "local", 0);
        const bool public_source =
            !Q_stricmp(source ? source->string : "local", "public");
        Cvar_SetByVar(source, public_source ? "local" : "public", FROM_MENU);
        ui_rml_server_explicit_arguments.clear();
        UI_Rml_StartServerRefresh(ui_rml_document);
        return true;
    }
    if (action == "connect") {
        return UI_Rml_ConnectSelectedServer();
    }
    if (action == "previous-page") {
        if (ui_rml_server_page > 0) {
            ui_rml_server_page--;
            UI_Rml_RenderServerBrowser(ui_rml_document, true);
        }
        return true;
    }
    if (action == "next-page") {
        const size_t page_count = ui_rml_server_entries.empty()
                                      ? 1
                                      : (ui_rml_server_entries.size() +
                                         UI_RML_SERVER_PAGE_SIZE - 1) /
                                            UI_RML_SERVER_PAGE_SIZE;
        if (ui_rml_server_page + 1 < page_count) {
            ui_rml_server_page++;
            UI_Rml_RenderServerBrowser(ui_rml_document, true);
        }
        return true;
    }
    if (action == "sort") {
        const int column = UI_Rml_ElementIntAttribute(
            element, "data-server-sort", -1);
        if (column >= 0 && column < UI_RML_SERVER_SORT_COUNT) {
            cvar_t *sort = Cvar_Get("ui_sortservers", "1", 0);
            const int requested = column + 1;
            const int encoded = sort ? sort->integer : 0;
            Cvar_SetInteger(sort,
                            abs(encoded) == requested ? -encoded : requested,
                            FROM_MENU);
            ui_rml_server_page = 0;
            UI_Rml_RenderServerBrowser(ui_rml_document, true);
        }
        return true;
    }
    if (action == "select") {
        const int index = UI_Rml_ElementIntAttribute(
            element, "data-server-index", -1);
        if (index >= 0 &&
            static_cast<size_t>(index) < ui_rml_server_entries.size()) {
            UI_Rml_SelectServerEntry(static_cast<size_t>(index));
        }
        return true;
    }
    return false;
}

// ---- Demo browser: legacy filesystem parity with native RmlUi presentation ----

static constexpr const char *UI_RML_DEMO_EXTENSIONS =
    ".dm2;.dm2.gz;.mvd2;.mvd2.gz";
static constexpr size_t UI_RML_DEMO_PAGE_SIZE = 8;
static constexpr int UI_RML_DEMO_MAX_ENTRIES = 4096;

enum class UI_Rml_DemoEntryType {
    Up,
    Directory,
    Demo,
};

enum UI_Rml_DemoSortColumn {
    UI_RML_DEMO_SORT_NAME = 0,
    UI_RML_DEMO_SORT_DATE,
    UI_RML_DEMO_SORT_SIZE,
    UI_RML_DEMO_SORT_MAP,
    UI_RML_DEMO_SORT_POV,
    UI_RML_DEMO_SORT_COUNT,
};

struct UI_Rml_DemoEntry {
    UI_Rml_DemoEntryType type = UI_Rml_DemoEntryType::Demo;
    Rml::String name;
    Rml::String date;
    Rml::String size_text;
    Rml::String map;
    Rml::String pov;
    int64_t size = 0;
    time_t mtime = 0;
};

struct UI_Rml_DemoMetadataCacheEntry {
    int64_t size = 0;
    time_t mtime = 0;
    demoInfo_t info = {};
};

static Rml::String ui_rml_demo_browse = "/demos";
static std::vector<UI_Rml_DemoEntry> ui_rml_demo_entries;
static std::unordered_map<std::string, UI_Rml_DemoMetadataCacheEntry>
    ui_rml_demo_metadata_cache;
static size_t ui_rml_demo_directory_count;
static size_t ui_rml_demo_page;
static uint64_t ui_rml_demo_total_bytes;
static int ui_rml_demo_total_files;
static bool ui_rml_demo_truncated;

static bool UI_Rml_DemoFilenameIsSafe(const Rml::String &name)
{
    if (name.empty()) {
        return false;
    }

    for (const char character : name) {
        if (character == '\n' || character == '\r' ||
            character == '"' || character == ';') {
            return false;
        }
    }

    return true;
}

static Rml::String UI_Rml_DemoPathForName(const Rml::String &name)
{
    Rml::String path = ui_rml_demo_browse;
    if (path.empty() || path.back() != '/') {
        path += '/';
    }
    path += name;
    return path;
}

static void UI_Rml_DemoAddDirectory(const char *name,
                                    UI_Rml_DemoEntryType type)
{
    if (!name || !name[0]) {
        return;
    }

    UI_Rml_DemoEntry entry;
    entry.type = type;
    entry.name = name;
    entry.date = "-";
    entry.size_text = "DIR";
    entry.map = "-";
    entry.pov = "-";
    ui_rml_demo_entries.push_back(std::move(entry));
}

static const Rml::String &UI_Rml_DemoStringSortValue(
    const UI_Rml_DemoEntry &entry,
    int column)
{
    switch (column) {
    case UI_RML_DEMO_SORT_MAP:
        return entry.map;
    case UI_RML_DEMO_SORT_POV:
        return entry.pov;
    default:
        return entry.name;
    }
}

static void UI_Rml_SortDemoEntries(void)
{
    int encoded_sort = Cvar_VariableInteger("ui_sortdemos");
    if (encoded_sort < -UI_RML_DEMO_SORT_COUNT) {
        encoded_sort = -UI_RML_DEMO_SORT_COUNT;
    } else if (encoded_sort > UI_RML_DEMO_SORT_COUNT) {
        encoded_sort = UI_RML_DEMO_SORT_COUNT;
    }

    if (!encoded_sort || ui_rml_demo_directory_count >= ui_rml_demo_entries.size()) {
        return;
    }

    const int direction = encoded_sort > 0 ? 1 : -1;
    const int column = abs(encoded_sort) - 1;
    auto begin = ui_rml_demo_entries.begin() + ui_rml_demo_directory_count;

    std::stable_sort(begin, ui_rml_demo_entries.end(),
                     [column, direction](const UI_Rml_DemoEntry &left,
                                         const UI_Rml_DemoEntry &right) {
        int comparison = 0;

        if (column == UI_RML_DEMO_SORT_DATE) {
            comparison = left.mtime < right.mtime ? -1 :
                         left.mtime > right.mtime ? 1 : 0;
        } else if (column == UI_RML_DEMO_SORT_SIZE) {
            comparison = left.size < right.size ? -1 :
                         left.size > right.size ? 1 : 0;
        } else {
            comparison = Q_stricmp(
                UI_Rml_DemoStringSortValue(left, column).c_str(),
                UI_Rml_DemoStringSortValue(right, column).c_str());
        }

        if (!comparison) {
            comparison = Q_stricmp(left.name.c_str(), right.name.c_str());
        }
        return direction > 0 ? comparison < 0 : comparison > 0;
    });
}

static void UI_Rml_BuildDemoEntries(void)
{
    Cvar_Get("ui_sortdemos", "1", 0);
    cvar_t *list_all = Cvar_Get("ui_listalldemos", "0", 0);
    const unsigned flags = list_all && list_all->integer
                               ? 0
                               : FS_TYPE_REAL | FS_PATH_GAME;
    int num_directories = 0;
    int num_demos = 0;
    void **directories = FS_ListFiles(ui_rml_demo_browse.c_str(), nullptr,
                                      flags | FS_SEARCH_DIRSONLY,
                                      &num_directories);
    void **demos = FS_ListFiles(ui_rml_demo_browse.c_str(),
                                UI_RML_DEMO_EXTENSIONS,
                                flags | FS_SEARCH_EXTRAINFO,
                                &num_demos);

    ui_rml_demo_entries.clear();
    ui_rml_demo_total_bytes = 0;
    ui_rml_demo_total_files = num_demos;
    ui_rml_demo_truncated = num_demos > UI_RML_DEMO_MAX_ENTRIES;

    if (ui_rml_demo_browse != "/") {
        UI_Rml_DemoAddDirectory("..", UI_Rml_DemoEntryType::Up);
    }

    if (directories) {
        for (int index = 0; index < num_directories; index++) {
            const char *name = static_cast<const char *>(directories[index]);
            if (name && name[0] && strcmp(name, ".") && strcmp(name, "..") &&
                UI_Rml_DemoFilenameIsSafe(name)) {
                UI_Rml_DemoAddDirectory(name, UI_Rml_DemoEntryType::Directory);
            }
        }
        FS_FreeList(directories);
    }

    ui_rml_demo_directory_count = ui_rml_demo_entries.size();

    time_t now = time(nullptr);
    struct tm *local_now = localtime(&now);
    const int current_year = local_now ? local_now->tm_year : -1;
    const int demo_limit = num_demos < UI_RML_DEMO_MAX_ENTRIES
                               ? num_demos
                               : UI_RML_DEMO_MAX_ENTRIES;

    for (int index = 0; demos && index < demo_limit; index++) {
        const file_info_t *file = static_cast<const file_info_t *>(demos[index]);
        if (!file || !UI_Rml_DemoFilenameIsSafe(file->name)) {
            continue;
        }

        UI_Rml_DemoEntry entry;
        entry.type = UI_Rml_DemoEntryType::Demo;
        entry.name = file->name;
        entry.size = file->size;
        entry.mtime = file->mtime;

        char formatted[64];
        struct tm *file_time = localtime(&entry.mtime);
        const char *date_format =
            file_time && file_time->tm_year == current_year
                ? "%b %d %H:%M"
                : "%b %d  %Y";
        if (!file_time || !strftime(formatted, sizeof(formatted),
                                    date_format, file_time)) {
            Q_strlcpy(formatted, "???", sizeof(formatted));
        }
        entry.date = formatted;

        Com_FormatSize(formatted, sizeof(formatted), entry.size);
        entry.size_text = formatted;

        const Rml::String path = UI_Rml_DemoPathForName(entry.name);
        UI_Rml_DemoMetadataCacheEntry *cached = nullptr;
        auto cached_it = ui_rml_demo_metadata_cache.find(path);
        if (cached_it != ui_rml_demo_metadata_cache.end() &&
            cached_it->second.size == entry.size &&
            cached_it->second.mtime == entry.mtime) {
            cached = &cached_it->second;
        } else {
            UI_Rml_DemoMetadataCacheEntry metadata;
            metadata.size = entry.size;
            metadata.mtime = entry.mtime;
            Q_strlcpy(metadata.info.map, "???", sizeof(metadata.info.map));
            Q_strlcpy(metadata.info.pov, "???", sizeof(metadata.info.pov));
            CL_GetDemoInfo(path.c_str(), &metadata.info);
            cached = &ui_rml_demo_metadata_cache.insert_or_assign(
                path, metadata).first->second;
        }

        entry.map = cached->info.map[0] ? cached->info.map : "???";
        entry.pov = cached->info.mvd
                        ? "MVD"
                        : (cached->info.pov[0] ? cached->info.pov : "???");
        ui_rml_demo_total_bytes += static_cast<uint64_t>(entry.size);
        ui_rml_demo_entries.push_back(std::move(entry));
    }

    if (demos) {
        FS_FreeList(demos);
    }

    UI_Rml_SortDemoEntries();

    const size_t page_count = ui_rml_demo_entries.empty()
                                  ? 1
                                  : (ui_rml_demo_entries.size() +
                                     UI_RML_DEMO_PAGE_SIZE - 1) /
                                        UI_RML_DEMO_PAGE_SIZE;
    if (ui_rml_demo_page >= page_count) {
        ui_rml_demo_page = page_count - 1;
    }
}

static void UI_Rml_SetDemoControlDisabled(Rml::ElementDocument *document,
                                          const char *id,
                                          bool disabled)
{
    if (Rml::Element *element = document ? document->GetElementById(id) : nullptr) {
        if (disabled) {
            element->SetAttribute("disabled", "");
        } else {
            element->RemoveAttribute("disabled");
        }
    }
}

static void UI_Rml_PopulateDemoBrowser(Rml::ElementDocument *document,
                                       bool attach_dynamic_listeners = false)
{
    Rml::Element *body = document
                             ? document->GetElementById("demos-table-body")
                             : nullptr;
    if (!body) {
        return;
    }

    UI_Rml_BuildDemoEntries();

    const size_t page_count = ui_rml_demo_entries.empty()
                                  ? 1
                                  : (ui_rml_demo_entries.size() +
                                     UI_RML_DEMO_PAGE_SIZE - 1) /
                                        UI_RML_DEMO_PAGE_SIZE;
    const size_t first = ui_rml_demo_page * UI_RML_DEMO_PAGE_SIZE;
    size_t last = first + UI_RML_DEMO_PAGE_SIZE;
    if (last > ui_rml_demo_entries.size()) {
        last = ui_rml_demo_entries.size();
    }

    Rml::String rows;
    if (first >= last) {
        rows = "<tr class=\"placeholder-row\"><td colspan=\"5\">"
               "No demos found in this directory.</td></tr>";
    } else {
        for (size_t index = first; index < last; index++) {
            const UI_Rml_DemoEntry &entry = ui_rml_demo_entries[index];
            const bool directory = entry.type != UI_Rml_DemoEntryType::Demo;
            Rml::String name = UI_Rml_EscapeTextForInnerRml(entry.name.c_str());
            if (entry.type == UI_Rml_DemoEntryType::Directory) {
                name += "/";
            }

            rows += "<tr class=\"demo-row";
            rows += directory ? " demo-directory-row\">" : "\">";
            rows += "<td><button class=\"data-table-row-action\" type=\"button\" "
                    "data-demo-action=\"activate\" data-demo-index=\"";
            rows += std::to_string(index);
            rows += "\" data-menu-sound=\"";
            rows += entry.type == UI_Rml_DemoEntryType::Up ? "out" : "open";
            rows += "\">";
            rows += name;
            rows += "</button></td><td>";
            rows += UI_Rml_EscapeTextForInnerRml(entry.date.c_str());
            rows += "</td><td>";
            rows += UI_Rml_EscapeTextForInnerRml(entry.size_text.c_str());
            rows += "</td><td>";
            rows += UI_Rml_EscapeTextForInnerRml(entry.map.c_str());
            rows += "</td><td>";
            rows += UI_Rml_EscapeTextForInnerRml(entry.pov.c_str());
            rows += "</td></tr>";
        }
    }
    body->SetInnerRML(rows);

    UI_Rml_SetDemoControlDisabled(document, "demos-up",
                                  ui_rml_demo_browse == "/");
    UI_Rml_SetDemoControlDisabled(document, "demos-previous",
                                  ui_rml_demo_page == 0);
    UI_Rml_SetDemoControlDisabled(document, "demos-next",
                                  ui_rml_demo_page + 1 >= page_count);

    if (Rml::Element *source = document->GetElementById("demos-source")) {
        UI_Rml_SetElementInnerText(
            source,
            Cvar_VariableInteger("ui_listalldemos")
                ? "Paths: All"
                : "Paths: Game");
    }
    if (Rml::Element *page_label = document->GetElementById("demos-page-label")) {
        UI_Rml_SetElementInnerText(
            page_label,
            va("Page %zu / %zu", ui_rml_demo_page + 1, page_count));
    }
    if (Rml::Element *summary = document->GetElementById("demos-summary")) {
        UI_Rml_SetElementInnerText(
            summary,
            va("Browse and play recorded demos in %s.",
               ui_rml_demo_browse.c_str()));
    }
    if (Rml::Element *status = document->GetElementById("demos-status")) {
        char bytes[64];
        Com_FormatSizeLong(bytes, sizeof(bytes), ui_rml_demo_total_bytes);
        UI_Rml_SetElementInnerText(
            status,
            ui_rml_demo_truncated
                ? va("Showing the first %d of %d demos, %s total. Activate a name to open it.",
                     UI_RML_DEMO_MAX_ENTRIES, ui_rml_demo_total_files, bytes)
                : va("%d demo%s, %s total. Activate a name to open it.",
                     ui_rml_demo_total_files,
                     ui_rml_demo_total_files == 1 ? "" : "s",
                     bytes));
        status->SetClass("is-ready", !ui_rml_demo_entries.empty());
    }

    static const char *sort_header_ids[UI_RML_DEMO_SORT_COUNT] = {
        "demos-sort-name", "demos-sort-date", "demos-sort-size",
        "demos-sort-map", "demos-sort-pov"
    };
    const int encoded_sort = Cvar_VariableInteger("ui_sortdemos");
    const int active_sort = encoded_sort ? abs(encoded_sort) - 1 : -1;
    for (int column = 0; column < UI_RML_DEMO_SORT_COUNT; column++) {
        if (Rml::Element *header = document->GetElementById(sort_header_ids[column])) {
            if (column == active_sort) {
                header->SetAttribute("data-sort-state",
                                     encoded_sort > 0
                                         ? "ascending"
                                         : "descending");
            } else {
                header->RemoveAttribute("data-sort-state");
            }
        }
    }

    if (attach_dynamic_listeners) {
        UI_Rml_AttachElementAudioListeners(body);
        if (Rml::Element *first_action = body->QuerySelector("button")) {
            first_action->Focus(true);
            first_action->ScrollIntoView(Rml::ScrollAlignment::Nearest);
        }
    }
}

static bool UI_Rml_DemoLeaveDirectory(void)
{
    if (ui_rml_demo_browse == "/") {
        return false;
    }

    const size_t slash = ui_rml_demo_browse.rfind('/');
    if (slash == Rml::String::npos || slash == 0) {
        ui_rml_demo_browse = "/";
    } else {
        ui_rml_demo_browse.resize(slash);
    }
    ui_rml_demo_page = 0;
    return true;
}

static bool UI_Rml_HandleDemoBrowserAction(Rml::Element *element)
{
    if (!element || !ui_rml_document || ui_rml_active_route != "demos") {
        return false;
    }

    const Rml::String action =
        element->GetAttribute<Rml::String>("data-demo-action", "");
    if (action == "up") {
        if (UI_Rml_DemoLeaveDirectory()) {
            UI_Rml_PopulateDemoBrowser(ui_rml_document, true);
        }
        return true;
    }

    if (action == "refresh") {
        UI_Rml_PopulateDemoBrowser(ui_rml_document, true);
        return true;
    }

    if (action == "toggle-source") {
        cvar_t *list_all = Cvar_Get("ui_listalldemos", "0", 0);
        Cvar_SetInteger(list_all, list_all && list_all->integer ? 0 : 1,
                        FROM_MENU);
        ui_rml_demo_page = 0;
        UI_Rml_PopulateDemoBrowser(ui_rml_document, true);
        return true;
    }

    if (action == "previous-page") {
        if (ui_rml_demo_page > 0) {
            ui_rml_demo_page--;
            UI_Rml_PopulateDemoBrowser(ui_rml_document, true);
        }
        return true;
    }

    if (action == "next-page") {
        const size_t page_count = ui_rml_demo_entries.empty()
                                      ? 1
                                      : (ui_rml_demo_entries.size() +
                                         UI_RML_DEMO_PAGE_SIZE - 1) /
                                            UI_RML_DEMO_PAGE_SIZE;
        if (ui_rml_demo_page + 1 < page_count) {
            ui_rml_demo_page++;
            UI_Rml_PopulateDemoBrowser(ui_rml_document, true);
        }
        return true;
    }

    if (action == "sort") {
        const int column = UI_Rml_ElementIntAttribute(
            element, "data-demo-sort", -1);
        if (column >= 0 && column < UI_RML_DEMO_SORT_COUNT) {
            cvar_t *sort = Cvar_Get("ui_sortdemos", "1", 0);
            const int encoded = sort ? sort->integer : 0;
            const int requested = column + 1;
            Cvar_SetInteger(sort,
                            abs(encoded) == requested ? -encoded : requested,
                            FROM_MENU);
            ui_rml_demo_page = 0;
            UI_Rml_PopulateDemoBrowser(ui_rml_document, true);
        }
        return true;
    }

    if (action != "activate") {
        return false;
    }

    const int index = UI_Rml_ElementIntAttribute(
        element, "data-demo-index", -1);
    if (index < 0 || static_cast<size_t>(index) >= ui_rml_demo_entries.size()) {
        return true;
    }

    const UI_Rml_DemoEntry entry = ui_rml_demo_entries[index];
    if (entry.type == UI_Rml_DemoEntryType::Up) {
        if (UI_Rml_DemoLeaveDirectory()) {
            UI_Rml_PopulateDemoBrowser(ui_rml_document, true);
        }
        return true;
    }

    if (!UI_Rml_DemoFilenameIsSafe(entry.name)) {
        Com_WPrintf("RmlUi rejected an unsafe demo browser entry.\n");
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_ALERT);
        return true;
    }

    const Rml::String path = UI_Rml_DemoPathForName(entry.name);
    if (path.size() >= MAX_OSPATH) {
        Com_WPrintf("RmlUi demo browser path is too long.\n");
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_ALERT);
        return true;
    }

    if (entry.type == UI_Rml_DemoEntryType::Directory) {
        ui_rml_demo_browse = path;
        ui_rml_demo_page = 0;
        UI_Rml_PopulateDemoBrowser(ui_rml_document, true);
        return true;
    }

    return UI_Rml_QueueCommand(va("demo \"%s\"", path.c_str()));
}

// Hydrates the authored save/load rows from the same server-owned save
// metadata used by the legacy menu. Load rows for missing or unreadable saves
// are disabled; save rows remain actionable so an empty slot can be created.
static void UI_Rml_PopulateSaveSlots(Rml::ElementDocument *document)
{
    if (!document) {
        return;
    }

    Rml::ElementList elements;
    document->QuerySelectorAll(elements, ".save-slot[data-slot]");

    for (Rml::Element *element : elements) {
        const Rml::String slot =
            element->GetAttribute<Rml::String>("data-slot", "");
        const Rml::String action =
            element->GetAttribute<Rml::String>("data-action-type", "");
        if (slot.empty() || (action != "loadgame" && action != "savegame")) {
            continue;
        }

        Rml::String label;
        if (slot == "save0") {
            label = "Autosave";
        } else if (slot.size() > 4 && slot.compare(0, 4, "save") == 0) {
            label = "Slot ";
            label += slot.substr(4);
        } else {
            label = slot;
        }

        char *save_info = SV_GetSaveInfo(slot.c_str());
        const bool occupied = save_info && save_info[0];
        Rml::String visible_text = label;
        visible_text += "  -  ";
        visible_text += occupied ? save_info : "Empty";
        UI_Rml_SetElementInnerText(element, visible_text.c_str());

        element->SetAttribute("data-save-state", occupied ? "ready" : "empty");
        if (action == "loadgame" && !occupied) {
            element->SetAttribute("disabled", "");
        } else {
            element->RemoveAttribute("disabled");
        }

        if (save_info) {
            Z_Free(save_info);
        }
    }
}

// ---- Player setup: model/skin enumeration, dogtags, and 3D preview ----

struct UI_Rml_PlayerModelInfo {
    Rml::String directory;
    std::vector<Rml::String> skins;
};

static std::vector<UI_Rml_PlayerModelInfo> ui_rml_player_models;
static bool ui_rml_player_models_loaded;
static bool ui_rml_player_preview_active;
static qhandle_t ui_rml_player_preview_model;
static qhandle_t ui_rml_player_preview_skin;
static std::vector<qhandle_t> ui_rml_player_preview_weapons;
static int ui_rml_player_preview_weapon_index = -1;
static int ui_rml_player_preview_frame;
static int ui_rml_player_preview_oldframe;
static unsigned ui_rml_player_preview_time;
static unsigned ui_rml_player_preview_oldtime;
static unsigned ui_rml_player_preview_realtime;
static int ui_rml_player_preview_stage;
static int ui_rml_player_preview_stage_loops;
static unsigned ui_rml_player_preview_hold_until;
static unsigned ui_rml_player_preview_muzzle_flash_until;

struct UI_Rml_PlayerPreviewStage {
    int first_frame;
    int last_frame;
    int frame_msec;
    int loops;
    int hold_msec;
    bool fire;
    bool switch_weapon;
};

static constexpr UI_Rml_PlayerPreviewStage ui_rml_player_preview_stages[] = {
    { FRAME_stand01, FRAME_stand40, 120, 2, 0, false, false },
    { FRAME_run1, FRAME_run6, 90, 4, 0, false, false },
    { FRAME_pain301, FRAME_pain304, 90, 1, 0, false, true },
    { FRAME_attack1, FRAME_attack8, 80, 2, 0, true, false },
    { FRAME_crstnd01, FRAME_crstnd19, 120, 1, 0, false, false },
    { FRAME_crattak1, FRAME_crattak9, 80, 1, 0, true, false },
    { FRAME_death101, FRAME_death106, 120, 1, 1200, false, false },
};

static bool UI_Rml_IsPlayerWeaponModel(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }

    return !Q_stricmp(name, "weapon.md2") ||
           !Q_strncasecmp(name, "w_", 2);
}

static void UI_Rml_AdvancePlayerPreviewWeapon(void)
{
    if (ui_rml_player_preview_weapons.empty()) {
        ui_rml_player_preview_weapon_index = -1;
        return;
    }

    ui_rml_player_preview_weapon_index =
        (ui_rml_player_preview_weapon_index + 1) %
        static_cast<int>(ui_rml_player_preview_weapons.size());
}

static void UI_Rml_SetPlayerPreviewStage(int stage)
{
    if (stage < 0 || stage >= q_countof(ui_rml_player_preview_stages)) {
        stage = 0;
    }

    ui_rml_player_preview_stage = stage;
    const UI_Rml_PlayerPreviewStage &preview_stage =
        ui_rml_player_preview_stages[stage];
    ui_rml_player_preview_stage_loops = max(1, preview_stage.loops);
    ui_rml_player_preview_hold_until = 0;
    ui_rml_player_preview_muzzle_flash_until = 0;
    ui_rml_player_preview_frame = preview_stage.first_frame;
    ui_rml_player_preview_oldframe = preview_stage.first_frame;
    ui_rml_player_preview_time = ui_rml_player_preview_realtime;
    ui_rml_player_preview_oldtime = ui_rml_player_preview_realtime;

    if (preview_stage.switch_weapon) {
        UI_Rml_AdvancePlayerPreviewWeapon();
    }
}

static void UI_Rml_BuildPlayerPreviewWeapons(const Rml::String &model)
{
    ui_rml_player_preview_weapons.clear();
    ui_rml_player_preview_weapon_index = -1;

    char directory[MAX_QPATH];
    Q_concat(directory, sizeof(directory), "players/", model.c_str());

    int num_files = 0;
    void **files = FS_ListFiles(directory, ".md2", 0, &num_files);
    if (!files) {
        return;
    }

    std::vector<Rml::String> names;
    for (int i = 0; i < num_files; ++i) {
        const char *name = static_cast<const char *>(files[i]);
        if (UI_Rml_IsPlayerWeaponModel(name)) {
            names.emplace_back(name);
        }
    }
    FS_FreeList(files);

    std::sort(names.begin(), names.end(),
              [](const Rml::String &a, const Rml::String &b) {
                  return Q_stricmp(a.c_str(), b.c_str()) < 0;
              });

    char path[MAX_QPATH];
    for (const Rml::String &name : names) {
        Q_concat(path, sizeof(path), directory, "/", name.c_str());
        if (qhandle_t handle = R_RegisterModel(path)) {
            ui_rml_player_preview_weapons.push_back(handle);
        }
    }

    if (!ui_rml_player_preview_weapons.empty()) {
        ui_rml_player_preview_weapon_index = 0;
    }
}

static void UI_Rml_LoadPlayerModels(void)
{
    if (ui_rml_player_models_loaded) {
        return;
    }
    ui_rml_player_models_loaded = true;

    int num_dirs = 0;
    void **dirs = FS_ListFiles("players", NULL, FS_SEARCH_DIRSONLY, &num_dirs);
    if (!dirs) {
        return;
    }

    for (int i = 0; i < num_dirs; i++) {
        const char *dir = static_cast<const char *>(dirs[i]);
        char scratch[MAX_QPATH];

        Q_concat(scratch, sizeof(scratch), "players/", dir, "/tris.md2");
        if (!FS_FileExists(scratch)) {
            continue;
        }

        Q_concat(scratch, sizeof(scratch), "players/", dir);
        int num_pcx = 0;
        void **pcx = FS_ListFiles(scratch, ".pcx", 0, &num_pcx);
        if (!pcx) {
            continue;
        }

        UI_Rml_PlayerModelInfo info;
        info.directory = dir;

        for (int k = 0; k < num_pcx; k++) {
            const char *name = static_cast<const char *>(pcx[k]);
            if (strstr(name, "_i.")) {
                continue;
            }

            char stem[MAX_QPATH];
            char icon[MAX_QPATH];
            COM_StripExtension(stem, name, sizeof(stem));
            Q_concat(icon, sizeof(icon), stem, "_i.pcx");

            for (int j = 0; j < num_pcx; j++) {
                if (!Q_stricmp(static_cast<const char *>(pcx[j]), icon)) {
                    info.skins.push_back(stem);
                    break;
                }
            }
        }

        FS_FreeList(pcx);

        if (!info.skins.empty()) {
            ui_rml_player_models.push_back(std::move(info));
        }
    }

    FS_FreeList(dirs);

    // male, then female, then the rest alphabetically (legacy ordering).
    std::sort(ui_rml_player_models.begin(), ui_rml_player_models.end(),
              [](const UI_Rml_PlayerModelInfo &a, const UI_Rml_PlayerModelInfo &b) {
                  if (!Q_stricmp(a.directory.c_str(), "male")) return true;
                  if (!Q_stricmp(b.directory.c_str(), "male")) return false;
                  if (!Q_stricmp(a.directory.c_str(), "female")) return true;
                  if (!Q_stricmp(b.directory.c_str(), "female")) return false;
                  return Q_stricmp(a.directory.c_str(), b.directory.c_str()) < 0;
              });
}

static const UI_Rml_PlayerModelInfo *UI_Rml_FindPlayerModel(const Rml::String &directory)
{
    for (const UI_Rml_PlayerModelInfo &info : ui_rml_player_models) {
        if (!Q_stricmp(info.directory.c_str(), directory.c_str())) {
            return &info;
        }
    }

    return nullptr;
}

static void UI_Rml_SetPlayerPreviewState(Rml::ElementDocument *document,
                                         const char *state)
{
    if (!document || !state) {
        return;
    }

    static const char *state_ids[] = {
        "players-preview-loading",
        "players-preview-empty",
        "players-preview-error",
    };

    for (const char *id : state_ids) {
        if (Rml::Element *element = document->GetElementById(id)) {
            const bool visible = strstr(id, state) != nullptr;
            if (visible) {
                element->RemoveProperty("display");
            } else {
                element->SetProperty("display", "none");
            }
        }
    }

    if (Rml::Element *preview = document->GetElementById("players-preview")) {
        preview->SetAttribute("data-player-preview-state", state);
    }
}

static bool UI_Rml_ReloadPlayerPreviewMedia(const Rml::String &model,
                                            const Rml::String &skin)
{
    char scratch[MAX_QPATH];

    ui_rml_player_preview_active = false;
    ui_rml_player_preview_model = 0;
    ui_rml_player_preview_skin = 0;
    ui_rml_player_preview_weapons.clear();
    ui_rml_player_preview_weapon_index = -1;

    if (model.empty() || skin.empty()) {
        return false;
    }

    Q_concat(scratch, sizeof(scratch), "players/", model.c_str(), "/tris.md2");
    ui_rml_player_preview_model = R_RegisterModel(scratch);
    if (!ui_rml_player_preview_model) {
        return false;
    }

    Q_concat(scratch, sizeof(scratch), "players/", model.c_str(), "/",
             skin.c_str(), ".pcx");
    ui_rml_player_preview_skin = R_RegisterSkin(scratch);
    if (!ui_rml_player_preview_skin) {
        return false;
    }

    UI_Rml_BuildPlayerPreviewWeapons(model);
    UI_Rml_SetPlayerPreviewStage(0);
    ui_rml_player_preview_active = true;
    return true;
}

static void UI_Rml_ApplyPlayerConfigSelection(bool model_changed)
{
    if (!ui_rml_document) {
        return;
    }

    auto *model_select = UI_Rml_SelectFromElement(
        ui_rml_document->GetElementById("players-model"));
    auto *skin_select = UI_Rml_SelectFromElement(
        ui_rml_document->GetElementById("players-skin"));
    if (!model_select || !skin_select) {
        return;
    }

    const Rml::String model = model_select->GetValue();
    const UI_Rml_PlayerModelInfo *info = UI_Rml_FindPlayerModel(model);
    if (!info) {
        return;
    }

    Rml::String skin = skin_select->GetValue();

    if (model_changed) {
        // Repopulate the skin list for the newly selected model, keeping
        // the current skin name when the model also provides it.
        bool skin_exists = false;

        skin_select->RemoveAll();
        for (const Rml::String &name : info->skins) {
            skin_select->Add(UI_Rml_EscapeTextForInnerRml(name.c_str()), name);
            if (!Q_stricmp(name.c_str(), skin.c_str())) {
                skin_exists = true;
            }
        }

        if (!skin_exists) {
            skin = info->skins[0];
        }

        ui_rml_applying_cvar_bindings = true;
        skin_select->SetValue(skin);
        ui_rml_applying_cvar_bindings = false;
    }

    if (skin.empty()) {
        return;
    }

    cvar_t *skin_var = Cvar_Get("skin", "male/grunt",
                                CVAR_USERINFO | CVAR_ARCHIVE);
    Cvar_SetByVar(skin_var, va("%s/%s", model.c_str(), skin.c_str()),
                  FROM_MENU);

    UI_Rml_SetPlayerPreviewState(
        ui_rml_document,
        UI_Rml_ReloadPlayerPreviewMedia(model, skin) ? "ready" : "error");
}

class UI_Rml_PlayerConfigEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &event) override
    {
        if (ui_rml_applying_cvar_bindings ||
            event.GetId() != Rml::EventId::Change) {
            return;
        }

        Rml::Element *element = event.GetTargetElement();
        const bool model_changed =
            element && element->GetId() == "players-model";

        UI_Rml_ApplyPlayerConfigSelection(model_changed);
    }
};

static UI_Rml_PlayerConfigEventListener ui_rml_player_config_listener;

static void UI_Rml_PopulatePlayerConfig(Rml::ElementDocument *document)
{
    auto *model_select = UI_Rml_SelectFromElement(
        document->GetElementById("players-model"));
    auto *skin_select = UI_Rml_SelectFromElement(
        document->GetElementById("players-skin"));
    if (!model_select || !skin_select) {
        return;
    }

    UI_Rml_LoadPlayerModels();

    if (ui_rml_player_models.empty()) {
        model_select->SetAttribute("disabled", "");
        skin_select->SetAttribute("disabled", "");
        UI_Rml_SetPlayerPreviewState(document, "empty");
        return;
    }

    model_select->RemoveAttribute("disabled");
    skin_select->RemoveAttribute("disabled");

    // Split the current userinfo skin ("male/grunt") into model and skin.
    char current_model[MAX_QPATH] = "male";
    char current_skin[MAX_QPATH] = "grunt";
    Cvar_VariableStringBuffer("skin", current_model, sizeof(current_model));
    if (char *slash = strpbrk(current_model, "/\\")) {
        *slash++ = 0;
        Q_strlcpy(current_skin, slash, sizeof(current_skin));
    }

    const UI_Rml_PlayerModelInfo *current_info =
        UI_Rml_FindPlayerModel(current_model);
    if (!current_info) {
        current_info = &ui_rml_player_models[0];
    }

    model_select->RemoveAll();
    for (const UI_Rml_PlayerModelInfo &info : ui_rml_player_models) {
        model_select->Add(UI_Rml_EscapeTextForInnerRml(info.directory.c_str()),
                          info.directory);
    }

    Rml::String skin = current_info->skins[0];
    skin_select->RemoveAll();
    for (const Rml::String &name : current_info->skins) {
        skin_select->Add(UI_Rml_EscapeTextForInnerRml(name.c_str()), name);
        if (!Q_stricmp(name.c_str(), current_skin)) {
            skin = name;
        }
    }

    ui_rml_applying_cvar_bindings = true;
    model_select->SetValue(current_info->directory);
    skin_select->SetValue(skin);
    ui_rml_applying_cvar_bindings = false;

    model_select->AddEventListener(Rml::EventId::Change,
                                   &ui_rml_player_config_listener);
    skin_select->AddEventListener(Rml::EventId::Change,
                                  &ui_rml_player_config_listener);

    UI_Rml_SetPlayerPreviewState(
        document,
        UI_Rml_ReloadPlayerPreviewMedia(current_info->directory, skin)
            ? "ready"
            : "error");
}

// Populates selects marked data-source-dir="<path>" with the file stems of
// that directory (used by the dogtag picker).
static void UI_Rml_PopulateDirectorySelects(Rml::ElementDocument *document)
{
    Rml::ElementList elements;
    document->QuerySelectorAll(elements, "select[data-source-dir]");

    for (Rml::Element *element : elements) {
        Rml::ElementFormControlSelect *select = UI_Rml_SelectFromElement(element);
        if (!select) {
            continue;
        }

        const Rml::String directory =
            element->GetAttribute<Rml::String>("data-source-dir", "");
        if (directory.empty()) {
            continue;
        }

        int num_files = 0;
        void **files = FS_ListFiles(directory.c_str(), NULL, 0, &num_files);
        if (!files || num_files <= 0) {
            if (files) {
                FS_FreeList(files);
            }
            continue;
        }

        std::vector<Rml::String> stems;
        for (int i = 0; i < num_files; i++) {
            char stem[MAX_QPATH];
            COM_StripExtension(stem, static_cast<const char *>(files[i]),
                               sizeof(stem));
            if (!stem[0]) {
                continue;
            }

            bool seen = false;
            for (const Rml::String &existing : stems) {
                if (!Q_stricmp(existing.c_str(), stem)) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                stems.push_back(stem);
            }
        }
        FS_FreeList(files);

        std::sort(stems.begin(), stems.end(),
                  [](const Rml::String &a, const Rml::String &b) {
                      return Q_stricmp(a.c_str(), b.c_str()) < 0;
                  });

        // Keep authored options (e.g. the 'default' entry) and skip
        // duplicates from the enumeration.
        for (const Rml::String &stem : stems) {
            bool authored = false;
            for (int i = 0; i < select->GetNumOptions(); i++) {
                Rml::Element *option = select->GetOption(i);
                if (option &&
                    !Q_stricmp(option->GetAttribute<Rml::String>("value", "").c_str(),
                               stem.c_str())) {
                    authored = true;
                    break;
                }
            }
            if (!authored) {
                select->Add(UI_Rml_EscapeTextForInnerRml(stem.c_str()), stem);
            }
        }
    }
}

static void UI_Rml_ResetPlayerPreview(void)
{
    ui_rml_player_preview_active = false;
    ui_rml_player_preview_model = 0;
    ui_rml_player_preview_skin = 0;
    ui_rml_player_preview_weapons.clear();
    ui_rml_player_preview_weapon_index = -1;
    ui_rml_player_preview_stage = 0;
    ui_rml_player_preview_stage_loops = 0;
    ui_rml_player_preview_hold_until = 0;
    ui_rml_player_preview_muzzle_flash_until = 0;
}

static void UI_Rml_AdvancePlayerPreview(unsigned realtime)
{
    ui_rml_player_preview_realtime = realtime;

    if (!ui_rml_player_preview_active ||
        (ui_rml_reduced_motion && ui_rml_reduced_motion->integer)) {
        return;
    }

    if (ui_rml_player_preview_hold_until) {
        ui_rml_player_preview_oldtime = realtime;
        ui_rml_player_preview_time = realtime;
        if (realtime >= ui_rml_player_preview_hold_until) {
            UI_Rml_SetPlayerPreviewStage(
                (ui_rml_player_preview_stage + 1) %
                q_countof(ui_rml_player_preview_stages));
        }
        return;
    }

    if (ui_rml_player_preview_time < realtime) {
        ui_rml_player_preview_oldtime = ui_rml_player_preview_time;
        const UI_Rml_PlayerPreviewStage &stage =
            ui_rml_player_preview_stages[ui_rml_player_preview_stage];
        ui_rml_player_preview_time += stage.frame_msec;
        if (ui_rml_player_preview_time < realtime) {
            ui_rml_player_preview_time = realtime;
        }

        ui_rml_player_preview_oldframe = ui_rml_player_preview_frame;
        ui_rml_player_preview_frame++;
        if (ui_rml_player_preview_frame > stage.last_frame) {
            ui_rml_player_preview_stage_loops--;
            if (ui_rml_player_preview_stage_loops > 0) {
                ui_rml_player_preview_frame = stage.first_frame;
            } else if (stage.hold_msec > 0) {
                ui_rml_player_preview_frame = stage.last_frame;
                ui_rml_player_preview_hold_until = realtime + stage.hold_msec;
                ui_rml_player_preview_oldtime = realtime;
                ui_rml_player_preview_time = realtime;
            } else {
                UI_Rml_SetPlayerPreviewStage(
                    (ui_rml_player_preview_stage + 1) %
                    q_countof(ui_rml_player_preview_stages));
                return;
            }
        }

        if (stage.fire &&
            ((ui_rml_player_preview_frame - stage.first_frame) & 1)) {
            ui_rml_player_preview_muzzle_flash_until = realtime + 60;
        }
    }
}

static void UI_Rml_RenderPlayerPreview(void)
{
    if (!ui_rml_player_preview_active || !ui_rml_document ||
        Q_stricmp(ui_rml_active_route.c_str(), "players")) {
        return;
    }

    Rml::Element *surface =
        ui_rml_document->GetElementById("players-preview-surface");
    if (!surface) {
        return;
    }

    const Rml::Vector2f offset =
        surface->GetAbsoluteOffset(Rml::BoxArea::Padding);
    const Rml::Vector2f size =
        surface->GetBox().GetSize(Rml::BoxArea::Padding);
    const float canvas_scale = UI_Rml_CanvasScale();

    const int x = Q_rint(offset.x * canvas_scale);
    const int y = Q_rint(offset.y * canvas_scale);
    const int width = Q_rint(size.x * canvas_scale);
    const int height = Q_rint(size.y * canvas_scale);

    if (width < 16 || height < 16) {
        return;
    }

    float backlerp = 0.0f;
    if (ui_rml_player_preview_time != ui_rml_player_preview_oldtime) {
        backlerp = 1.0f -
            static_cast<float>(ui_rml_player_preview_realtime -
                               ui_rml_player_preview_oldtime) /
            static_cast<float>(ui_rml_player_preview_time -
                               ui_rml_player_preview_oldtime);
        backlerp = Q_clipf(backlerp, 0.0f, 1.0f);
    }

    entity_t entities[2] = {};
    entities[0].model = ui_rml_player_preview_model;
    entities[0].skin = ui_rml_player_preview_skin;
    entities[0].flags = RF_FULLBRIGHT;
    entities[0].frame = ui_rml_player_preview_frame;
    entities[0].oldframe = ui_rml_player_preview_oldframe;
    entities[0].backlerp = backlerp;
    VectorSet(entities[0].origin, 80.0f, 0.0f, -6.0f);
    VectorCopy(entities[0].origin, entities[0].oldorigin);
    VectorSet(entities[0].scale, 1.0f, 1.0f, 1.0f);
    entities[0].angles[1] = 260.0f;
    if (!ui_rml_reduced_motion || !ui_rml_reduced_motion->integer) {
        entities[0].angles[1] =
            fmodf(260.0f + ui_rml_player_preview_realtime * 0.02f, 360.0f);
    }

    int num_entities = 1;
    if (ui_rml_player_preview_weapon_index >= 0 &&
        ui_rml_player_preview_weapon_index <
            static_cast<int>(ui_rml_player_preview_weapons.size())) {
        entities[1] = entities[0];
        entities[1].model =
            ui_rml_player_preview_weapons[ui_rml_player_preview_weapon_index];
        entities[1].skin = 0;
        entities[1].skinnum = 0;
        num_entities = 2;
    }

    dlight_t muzzle_light = {};
    int num_dlights = 0;
    if (num_entities > 1 &&
        ui_rml_player_preview_realtime <
            ui_rml_player_preview_muzzle_flash_until &&
        (!ui_rml_reduced_motion || !ui_rml_reduced_motion->integer)) {
        vec3_t forward, right, up;
        AngleVectors(entities[0].angles, forward, right, up);
        VectorCopy(entities[0].origin, muzzle_light.origin);
        VectorMA(muzzle_light.origin, 18.0f, forward, muzzle_light.origin);
        VectorMA(muzzle_light.origin, 8.0f, right, muzzle_light.origin);
        VectorMA(muzzle_light.origin, 8.0f, up, muzzle_light.origin);
        muzzle_light.radius = 120.0f;
        muzzle_light.intensity = 1.0f;
        VectorSet(muzzle_light.color, 1.0f, 0.8f, 0.4f);
        num_dlights = 1;
    }

    refdef_t refdef = {};
    refdef.x = x;
    refdef.y = y;
    refdef.width = width;
    refdef.height = height;
    // Keep the complete player/weapon silhouette inside the authored wide
    // preview panel, including side-on attack frames and larger weapon.md2
    // variants. The previous 40-degree crop regularly cut off the weapon.
    refdef.fov_x = 55.0f;
    refdef.fov_y = V_CalcFov(refdef.fov_x, refdef.width, refdef.height);
    refdef.time = ui_rml_player_preview_realtime * 0.001f;
    refdef.rdflags = RDF_NOWORLDMODEL;
    refdef.num_entities = num_entities;
    refdef.entities = entities;
    refdef.num_dlights = num_dlights;
    refdef.dlights = &muzzle_light;

    R_RenderFrame(&refdef);
    R_SetScale(UI_Rml_DrawScale());
}

static void UI_Rml_ApplyAccessibilityClasses(Rml::ElementDocument *document)
{
    if (!document) {
        return;
    }

    const bool high_visibility =
        ui_rml_high_visibility && ui_rml_high_visibility->integer != 0;
    const bool reduced_motion =
        ui_rml_reduced_motion && ui_rml_reduced_motion->integer != 0;
    const bool large_text =
        ui_rml_large_text && ui_rml_large_text->integer != 0;

    document->SetClass("ui-high-visibility", high_visibility);
    document->SetClass("ui-reduced-motion", reduced_motion);
    document->SetClass("ui-a11y-large-text", large_text);
    if (Rml::Element *body = UI_Rml_DocumentBody(document)) {
        body->SetClass("ui-high-visibility", high_visibility);
        body->SetClass("ui-reduced-motion", reduced_motion);
        body->SetClass("ui-a11y-large-text", large_text);
    }
}

static void UI_Rml_ApplyPreloadAccessibilityClasses(Rml::String &contents)
{
    Rml::String classes;
    if (ui_rml_high_visibility && ui_rml_high_visibility->integer != 0) {
        classes = "ui-high-visibility";
    }
    if (ui_rml_reduced_motion && ui_rml_reduced_motion->integer != 0) {
        if (!classes.empty()) {
            classes += " ";
        }
        classes += "ui-reduced-motion";
    }
    if (ui_rml_large_text && ui_rml_large_text->integer != 0) {
        if (!classes.empty()) {
            classes += " ";
        }
        classes += "ui-a11y-large-text";
    }
    if (classes.empty()) {
        return;
    }

    // Loading from memory lets accessibility state exist before RmlUi creates
    // entrance animations. Cancelling an already-created opacity animation
    // can otherwise leave compiled geometry sampled at an invisible frame.
    const size_t body = contents.find("<body");
    if (body == Rml::String::npos) {
        return;
    }
    contents.insert(body + strlen("<body"),
                    " class=\"" + classes + "\"");
}

static void UI_Rml_ApplyDocumentLocalization(Rml::ElementDocument *document,
                                             bool force)
{
    if (!document || !ui_rml_localization_language) {
        return;
    }

    if (!force && ui_rml_localization_modified_count ==
                      ui_rml_localization_language->modified_count) {
        return;
    }

    Rml::ElementList elements;
    document->QuerySelectorAll(elements, "[data-loc-key]");
    for (Rml::Element *element : elements) {
        const Rml::String key =
            element->GetAttribute<Rml::String>("data-loc-key", "");
        if (key.empty()) {
            continue;
        }

        Rml::String source_rml =
            element->GetAttribute<Rml::String>("data-loc-source-rml", "");
        if (source_rml.empty()) {
            source_rml = UI_Rml_TrimString(element->GetInnerRML());
            if (!source_rml.empty()) {
                element->SetAttribute("data-loc-source-rml", source_rml);
            }
        }

        char localization_key[MAX_STRING_CHARS];
        char localized[MAX_STRING_CHARS];
        Q_snprintf(localization_key, sizeof(localization_key), "$%s",
                   key.c_str());
        Loc_Localize(localization_key, false, NULL, 0,
                     localized, sizeof(localized));

        // Loc_Localize returns the key name when the selected language does
        // not yet define it. Preserve the authored English copy in that case
        // so newly annotated strings remain usable while translators can add
        // language entries incrementally.
        const bool missing_translation = !strcmp(localized, key.c_str());
        if (missing_translation && !source_rml.empty()) {
            if (element->GetInnerRML() != source_rml) {
                element->SetInnerRML(source_rml);
            }
        } else {
            UI_Rml_SetElementInnerText(element, localized);
        }

        if (element->HasAttribute("aria-label")) {
            element->SetAttribute(
                "aria-label",
                missing_translation && !source_rml.empty()
                    ? source_rml
                    : Rml::String(localized));
        }
    }

    ui_rml_localization_modified_count =
        ui_rml_localization_language->modified_count;
}

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

    if (Cvar_VariableInteger("ui_rml_debug")) {
        Com_Printf("RmlUi runtime file probe OK: route '%s' loaded %s (%zu bytes) through WORR filesystem.\n",
                   route_id ? route_id : "<null>",
                   document_path,
                   contents.size());
    }
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
    ui_rml_log_missing_data_models = NULL;
    ui_rml_high_visibility = NULL;
    ui_rml_reduced_motion = NULL;
    ui_rml_large_text = NULL;
    ui_rml_localization_language = NULL;
    ui_rml_localization_modified_count = -1;
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

    const ui_rml_renderer_family_t renderer_family = UI_Rml_RendererFamily();
    if (renderer_family != UI_RML_RENDERER_FAMILY_OPENGL &&
        renderer_family != UI_RML_RENDERER_FAMILY_VULKAN &&
        renderer_family != UI_RML_RENDERER_FAMILY_RTX_VKPT) {
        Com_Printf("RmlUi route '%s' resolved to '%s', but renderer family '%s' does not have a guarded native context path yet.\n",
                   route_id ? route_id : "<null>",
                   document_path ? document_path : "<null>",
                   UI_Rml_RendererFamilyString(renderer_family));
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

    const Rml::Vector2i initial_dimensions =
        UI_Rml_CompiledRuntimeCurrentDimensions();
    if (!UI_Rml_CompiledRuntimeEnsureContext(initial_dimensions.x,
                                            initial_dimensions.y)) {
        return false;
    }

    Rml::String document_contents;
    if (!Rml::GetFileInterface() ||
        !Rml::GetFileInterface()->LoadFile(document_path, document_contents)) {
        Com_Printf("RmlUi route '%s' failed to read document '%s'.\n",
                   route_id ? route_id : "<null>",
                   document_path);
        return false;
    }

    UI_Rml_ApplyPreloadAccessibilityClasses(document_contents);

    Rml::ElementDocument *document =
        ui_rml_context->LoadDocumentFromMemory(document_contents, document_path);
    if (!document) {
        Com_Printf("RmlUi route '%s' failed to load document '%s'.\n",
                   route_id ? route_id : "<null>",
                   document_path);
        return false;
    }

    UI_Rml_CompiledRuntimeCloseActiveDocument();

    ui_rml_document = document;
    ui_rml_document->AddEventListener(Rml::EventId::Click, &ui_rml_command_event_listener);
    ui_rml_active_route = route_id ? route_id : "";
    ui_rml_active_document = document_path;
    // Dynamic option population must run before cvar bindings so the
    // binding pass can select the live cvar value.
    UI_Rml_ExpandSourceLists(ui_rml_document);
    UI_Rml_PopulateMapDbSelects(ui_rml_document);
    UI_Rml_PopulateImageValueSelects(ui_rml_document);
    UI_Rml_PopulateDirectorySelects(ui_rml_document);
    UI_Rml_InitializeServerBrowser(ui_rml_document);
    UI_Rml_PopulateDemoBrowser(ui_rml_document);
    UI_Rml_PopulateSaveSlots(ui_rml_document);
    UI_Rml_PopulatePlayerConfig(ui_rml_document);
    UI_Rml_ApplyDocumentLocalization(ui_rml_document, true);
    UI_Rml_ApplyDocumentCvarBindings(ui_rml_document);
    UI_Rml_ApplyAccessibilityClasses(ui_rml_document);
    UI_Rml_RefreshKeybindDisplays(ui_rml_document);
    // Complete dynamic hydration while the document is still hidden. This
    // avoids invalidating already-visible compiled geometry (notably when
    // load-game rows gain disabled state) and guarantees the first presented
    // frame is the final authored layout.
    ui_rml_document->Show();
    ui_rml_context->Update();

    // Give keyboard/controller users a starting point: focus the first
    // enabled tabbable control once computed values are current, and before
    // the audio listeners attach so the initial focus stays silent.
    {
        Rml::Element *first_focus =
            ui_rml_document->FindNextTabElement(ui_rml_document, true);
        Rml::Element *search_start = first_focus;

        while (first_focus && first_focus->HasAttribute("disabled")) {
            first_focus =
                ui_rml_document->FindNextTabElement(first_focus, true);
            if (first_focus == search_start) {
                first_focus = nullptr;
                break;
            }
        }

        if (first_focus) {
            first_focus->Focus(true);
            first_focus->ScrollIntoView(Rml::ScrollAlignment::Nearest);
        }
    }

    UI_Rml_AttachElementCvarListeners(ui_rml_document);
    UI_Rml_AttachElementAudioListeners(ui_rml_document);
    UI_Rml_ApplyDocumentAudioHints(route_id, ui_rml_document);

    if (Cvar_VariableInteger("ui_rml_debug")) {
        Com_Printf("RmlUi route '%s' opened document '%s' in guarded context '%s'.\n",
                   ui_rml_active_route.c_str(),
                   ui_rml_active_document.c_str(),
                   "worr_ui");
    }
    return true;
}

static void UI_Rml_CompiledRuntimeCloseActiveDocument(void)
{
    ui_rml_keybind_capture_element = nullptr;
    ui_rml_keybind_capture_command.clear();
    ui_rml_keybind_capture_slot = 0;
    ui_rml_keybind_capture_started = 0;
    ui_rml_keybind_conflict_element = nullptr;
    ui_rml_keybind_conflict_command.clear();
    ui_rml_keybind_conflict_previous_command.clear();
    ui_rml_keybind_conflict_key = -1;
    UI_Rml_ResetPlayerPreview();

    if (ui_rml_active_route == "servers") {
        ui_rml_server_entries.clear();
        ui_rml_server_selected_address.clear();
        ui_rml_server_explicit_arguments.clear();
        ui_rml_server_refreshing = false;
        ui_rml_server_ping_stage = 0;
        ui_rml_server_dirty = false;
    }

    if (ui_rml_document) {
        Rml::ElementDocument *document = ui_rml_document;
        ui_rml_document = NULL;
        document->Close();
    }
}

static void UI_Rml_CompiledRuntimeCloseRoute(void)
{
    UI_Rml_CompiledRuntimeCloseActiveDocument();

    if (ui_rml_context) {
        ui_rml_context->Update();
    }

    // String textures accumulate for the lifetime of a menu session;
    // release them whenever the menu system fully closes.
    if (ui_rml_core_initialized) {
        Rml::ReleaseFontResources();
    }

    ui_rml_active_route.clear();
    ui_rml_active_document.clear();
}

static bool UI_Rml_CompiledRuntimeUpdate(int width, int height, unsigned realtime)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    UI_Rml_AdvancePlayerPreview(realtime);
    UI_Rml_AdvanceServerRefresh(realtime);

    if (ui_rml_keybind_capture_element &&
        Sys_Milliseconds() - ui_rml_keybind_capture_started >= 8000u) {
        UI_Rml_EndKeybindCapture();
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_CLOSE);
    }

    if (!UI_Rml_CompiledRuntimeEnsureContext(width, height)) {
        return false;
    }

    UI_Rml_RefreshDocumentCvarDisplays(ui_rml_document);
    UI_Rml_ApplyDocumentLocalization(ui_rml_document, false);
    if (ui_rml_server_dirty && ui_rml_active_route == "servers") {
        UI_Rml_RenderServerBrowser(ui_rml_document, true);
    }
    UI_Rml_ApplyAccessibilityClasses(ui_rml_document);
    (void)ui_rml_context->Update();
    return true;
}

static bool UI_Rml_CompiledRuntimeRender(void)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    R_SetClipRect(NULL);
    R_SetScale(UI_Rml_CompiledRuntimeRendererDrawScale());
    const bool rendered = ui_rml_context->Render();

    // RmlUi may finish on a clipped scroll region. Flush that geometry and
    // restore the renderer-wide clip state so the next gameplay frame is not
    // accidentally drawn through the menu's final scissor rectangle.
    R_SetClipRect(NULL);
    if (!rendered) {
        return false;
    }

    // The 3D player preview draws over its panel after the document.
    UI_Rml_RenderPlayerPreview();
    return true;
}

static bool UI_Rml_CompiledRuntimeKeyEvent(int key, bool down)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    // An active keybind capture consumes the next key before RmlUi focus
    // and navigation processing can react to it.
    if (UI_Rml_HandleKeybindCaptureKey(key, down)) {
        return true;
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

static Rml::String UI_Rml_ActiveDocumentCloseCommand(void)
{
    if (!ui_rml_document) {
        return "";
    }

    Rml::String value =
        ui_rml_document->GetAttribute<Rml::String>("data-close-command", "");

    if (value.empty()) {
        if (Rml::Element *body = UI_Rml_DocumentBody(ui_rml_document)) {
            value = body->GetAttribute<Rml::String>("data-close-command", "");
        }
    }

    if (value.empty()) {
        if (Rml::Element *holder =
                ui_rml_document->QuerySelector("[data-close-command]")) {
            value = holder->GetAttribute<Rml::String>("data-close-command", "");
        }
    }

    return value;
}

// Handles Escape/Mouse2 back requests: an active keybind capture or conflict
// consumes the key, and documents that declare data-close-command run their
// legacy close side effects (worr_vote_close etc.) instead of a bare back-pop.
static bool UI_Rml_CompiledRuntimeHandleBackKey(int key)
{
    if (!ui_rml_context || !ui_rml_document) {
        return false;
    }

    if (ui_rml_keybind_conflict_element) {
        UI_Rml_ClearKeybindConflict();
        UI_Rml_RefreshKeybindDisplays(ui_rml_document);
        UI_Rml_PlayMenuFeedbackSound(UI_FEEDBACK_CLOSE);
        return true;
    }

    if (ui_rml_keybind_capture_element) {
        return UI_Rml_HandleKeybindCaptureKey(key, true);
    }

    const Rml::String close_command = UI_Rml_ActiveDocumentCloseCommand();
    if (close_command.empty()) {
        return false;
    }

    if (UI_Rml_CommandStartsWithToken(close_command, "popmenu")) {
        const char *tail = UI_Rml_CommandTailAfterToken(close_command, "popmenu");

        return UI_Rml_InsertCommandSequence(
            "ui_rml_runtime_back",
            UI_Rml_RemoteSessionCommandWhenConnected(tail));
    }

    if (UI_Rml_CommandStartsWithToken(close_command, "forcemenuoff")) {
        const char *tail =
            UI_Rml_CommandTailAfterToken(close_command, "forcemenuoff");

        return UI_Rml_InsertCommandSequence(
            "ui_rml_runtime_close",
            UI_Rml_RemoteSessionCommandWhenConnected(tail));
    }

    if (UI_Rml_MatchHubCanCloseLocally(close_command)) {
        return UI_Rml_InsertCommandSequence("ui_rml_runtime_close",
                                            UI_Rml_RemoteSessionCommandWhenConnected(
                                                close_command.c_str()));
    }

    // Bare commands (e.g. download_cancel) own the close side effects.
    return UI_Rml_QueueCommand(close_command.c_str());
}

static bool UI_Rml_CompiledRuntimeStatusEvent(const serverStatus_t *status)
{
    return UI_Rml_ServerStatusEvent(status);
}

static bool UI_Rml_CompiledRuntimeErrorEvent(const netadr_t *from)
{
    return UI_Rml_ServerErrorEvent(from);
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
        UI_Rml_CompiledRuntimeHandleBackKey,
        UI_Rml_CompiledRuntimeStatusEvent,
        UI_Rml_CompiledRuntimeErrorEvent,
    };

    UI_Rml_SetRuntimeInterface(&runtime);
}

#endif
