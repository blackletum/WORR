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
#include "system/system.h"

#ifdef DotProduct
#undef DotProduct
#endif

#ifdef CrossProduct
#undef CrossProduct
#endif

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/URL.h>

static bool ui_rml_core_initialized;

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

static UI_Rml_SystemInterface ui_rml_system_interface;
static UI_Rml_CoreFileInterface ui_rml_file_interface;

static void UI_Rml_InstallCoreInterfaces(void)
{
    const ui_rml_renderer_interface_t *renderer = UI_Rml_RendererInterface();

    Rml::SetSystemInterface(&ui_rml_system_interface);
    Rml::SetFileInterface(&ui_rml_file_interface);

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
    (void)UI_Rml_RendererIsAvailable();
    return false;
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
    Com_Printf("RmlUi core initialized through %s; native renderer bridge is pending (renderer='%s', family='%s').\n",
               UI_Rml_CompiledRuntimeName(),
               UI_Rml_RendererName(),
               UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
    return true;
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
    } else {
        Com_Printf("RmlUi route '%s' resolved to '%s' with renderer='%s' family='%s', but document context rendering is pending.\n",
                   route_id ? route_id : "<null>",
                   document_path ? document_path : "<null>",
                   UI_Rml_RendererName(),
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
    }

    return false;
}

void UI_Rml_RegisterCompiledRuntime(void)
{
    static const ui_rml_runtime_interface_t runtime = {
        UI_Rml_CompiledRuntimeInit,
        UI_Rml_CompiledRuntimeShutdown,
        UI_Rml_CompiledRuntimeOpenRoute,
        UI_Rml_CompiledRuntimeProbeRoute,
        UI_Rml_CompiledRuntimeName,
        UI_Rml_CompiledRuntimeCanOpenRoutes,
    };

    UI_Rml_SetRuntimeInterface(&runtime);
}

#endif
