from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_runtime_adapter as runtime_adapter  # noqa: E402


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_valid_repo(repo_root: Path, *, adapter_text: str | None = None) -> None:
    write_text(
        repo_root / "meson.build",
        """
client_src = [
  'src/client/ui_rml/ui_rml.cpp',
  'src/client/ui_rml/ui_rml_runtime.cpp',
]
renderer_src = [
  'src/renderer/rmlui_bridge.cpp',
]
renderer_cpp_args = [
  '-DHAVE_CONFIG_H',
  '-DQ2PROTO_CONFIG_H="common/q2proto_config.h"',
]
renderer_gl_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_GL']
renderer_vk_rtx_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_VKPT', '-DRENDERER_VULKAN', '-DRENDERER_VULKAN_RTX=1']
renderer_vk_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_VKPT', '-DRENDERER_VULKAN', '-DRENDERER_VULKAN_LEGACY=1']
renderer_gl_deps = renderer_deps
if rmlui_runtime
  renderer_gl_deps += rmlui_dep
  renderer_gl_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
endif
rmlui_cmake_options = {
  'RMLUI_FONT_ENGINE': 'none',
  'RMLUI_SAMPLES': false,
  'RMLUI_TESTS': false,
}
""",
    )
    write_text(
        repo_root / "src/client/ui_rml/ui_rml.h",
        """
#define UI_RML_HAS_RUNTIME 0
typedef enum {
    UI_RML_AVAILABILITY_RENDERER_UNAVAILABLE,
} ui_rml_availability_t;
typedef struct {
    bool (*CanOpenRoutes)(void);
    bool (*ProbeRoute)(const char *, const char *);
} ui_rml_runtime_interface_t;
typedef enum {
    UI_RML_RENDERER_FAMILY_NONE,
    UI_RML_RENDERER_FAMILY_OPENGL,
    UI_RML_RENDERER_FAMILY_VULKAN,
    UI_RML_RENDERER_FAMILY_RTX_VKPT,
} ui_rml_renderer_family_t;
typedef struct {
    ui_rml_renderer_family_t family;
    const char *(*RendererName)(void);
    bool (*CanRender)(void);
    void *(*NativeRenderInterface)(void);
} ui_rml_renderer_interface_t;
void UI_Rml_RegisterCompiledRuntime(void);
void UI_Rml_SetRuntimeInterface(const ui_rml_runtime_interface_t *runtime);
void UI_Rml_SetRendererInterface(const ui_rml_renderer_interface_t *renderer);
void UI_Rml_ClearRendererInterface(void);
bool UI_Rml_RendererIsAvailable(void);
""",
    )
    write_text(
        repo_root / "src/client/ui_rml/ui_rml.cpp",
        """
static ui_rml_runtime_interface_t ui_rml_runtime;
static ui_rml_renderer_interface_t ui_rml_renderer;
static bool ui_rml_renderer_registered;
void UI_Rml_Init(void)
{
#if UI_RML_HAS_RUNTIME
    UI_Rml_RegisterCompiledRuntime();
#endif
}
const ui_rml_renderer_interface_t *UI_Rml_RendererInterface(void)
{
    return ui_rml_renderer_registered ? &ui_rml_renderer : NULL;
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
    default:
        return "none";
    }
}
const char *UI_Rml_RendererName(void)
{
    return "test";
}
bool UI_Rml_RendererIsAvailable(void)
{
    return ui_rml_renderer_registered &&
        ui_rml_renderer.CanRender &&
        ui_rml_renderer.NativeRenderInterface &&
        ui_rml_renderer.CanRender() &&
        ui_rml_renderer.NativeRenderInterface() != NULL;
}
static bool UI_Rml_RuntimeCanOpenRoutes(void)
{
    if (!UI_Rml_RendererIsAvailable()) {
        return false;
    }
    if (ui_rml_runtime.CanOpenRoutes) {
        return ui_rml_runtime.CanOpenRoutes();
    }
    return true;
}
static void UI_Rml_StopRuntime(void)
{
}
const char *UI_Rml_AvailabilityString(void)
{
    return "renderer_unavailable";
}
static void UI_Rml_RuntimeProbeRoute_f(void)
{
}
static const char *ui_rml_runtime_probe = "ui_rml_runtime_probe";
""",
    )
    write_text(
        repo_root / "src/client/ui_rml/ui_rml_runtime.cpp",
        adapter_text
        if adapter_text is not None
        else """
#include "ui_rml.h"
#if UI_RML_HAS_RUNTIME
#undef DotProduct
#undef CrossProduct
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
static void UI_Rml_InstallCoreInterfaces(void)
{
    const ui_rml_renderer_interface_t *renderer = UI_Rml_RendererInterface();
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    if (renderer && renderer->NativeRenderInterface) {
        void *native_render_interface = renderer->NativeRenderInterface();
        if (native_render_interface) {
            Rml::SetRenderInterface(static_cast<Rml::RenderInterface *>(native_render_interface));
        }
    }
}
static void file_api(void)
{
    FS_OpenFile(path, &handle, FS_MODE_READ);
    FS_Read(buffer, size, handle);
    FS_Seek(handle, 0, SEEK_SET);
    FS_Tell(handle);
    FS_Length(handle);
    FS_CloseFile(handle);
}
static void system_api(void)
{
    Sys_Milliseconds();
    Com_WPrintf("warning");
    Com_Printf("info");
}
static bool UI_Rml_CompiledRuntimeCanOpenRoutes(void)
{
    return false;
}
static const char *UI_Rml_CompiledRuntimeName(void)
{
    Rml::GetVersion();
    return "RmlUi";
}
static bool UI_Rml_CompiledRuntimeInit(void)
{
    UI_Rml_InstallCoreInterfaces();
    return Rml::Initialise();
}
static bool UI_Rml_CompiledRuntimeProbeRoute(const char *, const char *document_path)
{
    Rml::String contents;
    return Rml::GetFileInterface()->LoadFile(document_path, contents);
}
static void UI_Rml_CompiledRuntimeShutdown(void)
{
    Rml::Shutdown();
}
static bool UI_Rml_CompiledRuntimeOpenRoute(const char *, const char *)
{
    return false;
}
void UI_Rml_RegisterCompiledRuntime(void)
{
    static const ui_rml_runtime_interface_t runtime = { UI_Rml_CompiledRuntimeCanOpenRoutes };
    UI_Rml_SetRuntimeInterface(&runtime);
}
#endif
""",
    )
    write_text(
        repo_root / "inc/renderer/renderer.h",
        """
typedef enum {
    R_RENDERER_RMLUI_FAMILY_NONE,
    R_RENDERER_RMLUI_FAMILY_OPENGL,
    R_RENDERER_RMLUI_FAMILY_VULKAN,
    R_RENDERER_RMLUI_FAMILY_RTX_VKPT,
} renderer_rmlui_family_t;
renderer_rmlui_family_t R_RmlUiRendererFamily(void);
const char *R_RmlUiRendererName(void);
bool R_RmlUiCanRender(void);
void *R_RmlUiNativeRenderInterface(void);
typedef struct renderer_export_s {
    renderer_rmlui_family_t (*RmlUiRendererFamily)(void);
    const char *(*RmlUiRendererName)(void);
    bool (*RmlUiCanRender)(void);
    void *(*RmlUiNativeRenderInterface)(void);
} renderer_export_t;
""",
    )
    write_text(
        repo_root / "src/renderer/renderer_api.c",
        """
#if USE_REF != REF_GL
static renderer_rmlui_family_t Renderer_RmlUiRendererFamily(void)
{
    return R_RENDERER_RMLUI_FAMILY_NONE;
}
static const char *Renderer_RmlUiRendererName(void)
{
    return "none";
}
static bool Renderer_RmlUiCanRender(void)
{
    return false;
}
static void *Renderer_RmlUiNativeRenderInterface(void)
{
    return NULL;
}
#endif
static const renderer_export_t renderer_exports = {
#if USE_REF == REF_GL
    .RmlUiRendererFamily    = R_RmlUiRendererFamily,
    .RmlUiRendererName      = R_RmlUiRendererName,
    .RmlUiCanRender         = R_RmlUiCanRender,
    .RmlUiNativeRenderInterface = R_RmlUiNativeRenderInterface,
#else
    .RmlUiRendererFamily    = Renderer_RmlUiRendererFamily,
    .RmlUiRendererName      = Renderer_RmlUiRendererName,
    .RmlUiCanRender         = Renderer_RmlUiCanRender,
    .RmlUiNativeRenderInterface = Renderer_RmlUiNativeRenderInterface,
#endif
};
""",
    )
    write_text(
        repo_root / "src/renderer/rmlui_bridge.cpp",
        """
#include "gl.h"
#if UI_RML_HAS_RUNTIME
#undef DotProduct
#undef CrossProduct
#include <RmlUi/Core/RenderInterface.h>
class R_RmlUiOpenGLRenderInterface final : public Rml::RenderInterface {
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return {}; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &) override { return {}; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return {}; }
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};
static R_RmlUiOpenGLRenderInterface r_rmlui_opengl_render_interface;
#endif
renderer_rmlui_family_t R_RmlUiRendererFamily(void)
{
#if UI_RML_HAS_RUNTIME
    return R_RENDERER_RMLUI_FAMILY_OPENGL;
#else
    return R_RENDERER_RMLUI_FAMILY_NONE;
#endif
}
const char *R_RmlUiRendererName(void)
{
    return "OpenGL RmlUi render-interface scaffold";
}
bool R_RmlUiCanRender(void)
{
    return false;
}
void *R_RmlUiNativeRenderInterface(void)
{
#if UI_RML_HAS_RUNTIME
    return &r_rmlui_opengl_render_interface;
#else
    return NULL;
#endif
}
""",
    )
    write_text(
        repo_root / "src/client/renderer.cpp",
        """
static ui_rml_renderer_family_t RmlUi_RendererFamilyFromRenderer(renderer_rmlui_family_t family)
{
    switch (family) {
    case R_RENDERER_RMLUI_FAMILY_OPENGL:
        return UI_RML_RENDERER_FAMILY_OPENGL;
    case R_RENDERER_RMLUI_FAMILY_VULKAN:
        return UI_RML_RENDERER_FAMILY_VULKAN;
    case R_RENDERER_RMLUI_FAMILY_RTX_VKPT:
        return UI_RML_RENDERER_FAMILY_RTX_VKPT;
    default:
        return UI_RML_RENDERER_FAMILY_NONE;
    }
}
static void RmlUi_RegisterRendererBridge(void)
{
    ui_rml_renderer_interface_t renderer = {};
    renderer.family = RmlUi_RendererFamilyFromRenderer(R_RmlUiRendererFamily());
    renderer.RendererName = R_RmlUiRendererName;
    renderer.CanRender = R_RmlUiCanRender;
    renderer.NativeRenderInterface = R_RmlUiNativeRenderInterface;
    UI_Rml_SetRendererInterface(&renderer);
}
void CL_InitRenderer(void)
{
    cls.ref_initialized = true;
    RmlUi_RegisterRendererBridge();
}
void CL_ShutdownRenderer(void)
{
    UI_Shutdown();
    UI_Rml_ClearRendererInterface();
}
""",
    )
    write_text(
        repo_root / "subprojects/rmlui.wrap",
        """
[wrap-file]
directory = RmlUi-6.2
method = cmake

[provide]
dependency_names = RmlUi, rmlui
""",
    )


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    result = runtime_adapter.main(
        [
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_valid_runtime_adapter_boundary_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)

    result, captured = run_checker(repo_root, capsys)

    assert result == 0
    assert "Malformed findings: 0" in captured.out
    assert "RmlUi Core symbols: Rml::GetVersion, Rml::Initialise, Rml::Shutdown" in captured.out
    assert "RmlUi interface symbols: Rml::SetSystemInterface, Rml::SetFileInterface, Rml::GetFileInterface" in captured.out
    assert "WORR filesystem symbols: FS_OpenFile, FS_CloseFile, FS_Read, FS_Seek, FS_Tell, FS_Length" in captured.out
    assert "Runtime file-probe hook: yes" in captured.out
    assert "Renderer bridge listed in meson.build: yes" in captured.out
    assert "Renderer C++ args configured: yes" in captured.out
    assert "OpenGL-scoped RmlUi renderer dependency: yes" in captured.out
    assert "Renderer contract declared: yes" in captured.out
    assert "RmlUi render interface symbols: Rml::SetRenderInterface, Rml::RenderInterface" in captured.out
    assert "Renderer API contract declared: yes" in captured.out
    assert "Renderer API OpenGL only: yes" in captured.out
    assert "Client renderer bridge registered: yes" in captured.out
    assert "Client renderer family mapping: yes" in captured.out
    assert "OpenGL renderer bridge scaffolded: yes" in captured.out
    assert "OpenGL renderer CanRender false: yes" in captured.out
    assert "Renderer route gate: yes" in captured.out
    assert "Native render interface required: yes" in captured.out
    assert "Vulkan not redirected to OpenGL: yes" in captured.out
    assert "Route-open guard: yes" in captured.out
    assert "Provide dependencies: RmlUi, rmlui" in captured.out
    assert "Result: RmlUi runtime adapter boundary check passed." in captured.out


def test_json_report_exposes_adapter_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)

    result, captured = run_checker(repo_root, capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["adapter"]["listed_in_meson"] is True
    assert payload["adapter"]["renderer_bridge_listed_in_meson"] is True
    assert payload["adapter"]["renderer_bridge_listed_once_in_meson"] is True
    assert payload["adapter"]["renderer_cpp_args_configured"] is True
    assert payload["adapter"]["renderer_gl_runtime_dependency"] is True
    assert payload["adapter"]["runtime_guard_present"] is True
    assert payload["adapter"]["rmlui_core_include_guarded"] is True
    assert payload["adapter"]["rmlui_system_file_includes_guarded"] is True
    assert payload["adapter"]["rmlui_render_include_guarded"] is True
    assert payload["adapter"]["rmlui_macro_collision_guards"] is True
    assert payload["adapter"]["runtime_probe_hook_present"] is True
    assert payload["adapter"]["core_interfaces_installed_before_initialise"] is True
    assert payload["adapter"]["rmlui_render_interface_symbols"] == [
        "Rml::SetRenderInterface",
        "Rml::RenderInterface",
    ]
    assert payload["adapter"]["renderer_contract_declared"] is True
    assert payload["adapter"]["renderer_native_families"] == [
        "UI_RML_RENDERER_FAMILY_OPENGL",
        "UI_RML_RENDERER_FAMILY_VULKAN",
        "UI_RML_RENDERER_FAMILY_RTX_VKPT",
    ]
    assert payload["adapter"]["renderer_contract_scaffolded"] is True
    assert payload["adapter"]["renderer_api_contract_declared"] is True
    assert payload["adapter"]["renderer_api_exports_declared"] is True
    assert payload["adapter"]["renderer_api_opengl_only"] is True
    assert payload["adapter"]["renderer_api_non_gl_none"] is True
    assert payload["adapter"]["client_renderer_bridge_registered"] is True
    assert payload["adapter"]["client_renderer_bridge_cleared"] is True
    assert payload["adapter"]["client_renderer_family_mapping"] is True
    assert payload["adapter"]["opengl_renderer_bridge_guarded"] is True
    assert payload["adapter"]["opengl_renderer_bridge_scaffolded"] is True
    assert payload["adapter"]["opengl_renderer_bridge_family"] is True
    assert payload["adapter"]["opengl_renderer_can_render_false"] is True
    assert payload["adapter"]["renderer_route_gate_present"] is True
    assert payload["adapter"]["renderer_native_interface_required"] is True
    assert payload["adapter"]["vulkan_renderer_not_redirected"] is True
    assert payload["counts"]["worr_file_symbols"] == 6
    assert payload["counts"]["rmlui_render_interface_symbols"] == 2
    assert payload["counts"]["renderer_native_families"] == 3
    assert payload["adapter"]["route_open_guard_present"] is True
    assert payload["adapter"]["cmake_font_engine_none"] is True
    assert payload["adapter"]["cmake_samples_disabled"] is True
    assert payload["adapter"]["cmake_tests_disabled"] is True
    assert payload["wrap"]["provide_dependencies"] == ["RmlUi", "rmlui"]
    assert payload["errors"] == []


def test_missing_adapter_meson_entry_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    write_text(
        repo_root / "meson.build",
        """
client_src = [
  'src/client/ui_rml/ui_rml.cpp',
]
rmlui_cmake_options = {
  'RMLUI_FONT_ENGINE': 'none',
  'RMLUI_SAMPLES': false,
  'RMLUI_TESTS': false,
}
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "adapter: source exists but is not listed in meson.build" in captured.out


def test_unguarded_rmlui_include_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(
        repo_root,
        adapter_text="""
#include "ui_rml.h"
#include <RmlUi/Core/Core.h>
#if UI_RML_HAS_RUNTIME
Rml::String version = Rml::GetVersion();
bool init = Rml::Initialise();
void shutdown(void) { Rml::Shutdown(); }
void UI_Rml_RegisterCompiledRuntime(void) { UI_Rml_SetRuntimeInterface(&runtime); }
#endif
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "RmlUi Core include is not behind UI_RML_HAS_RUNTIME" in captured.out


def test_missing_conservative_route_open_guard_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(
        repo_root,
        adapter_text="""
#include "ui_rml.h"
#if UI_RML_HAS_RUNTIME
#undef DotProduct
#undef CrossProduct
#include <RmlUi/Core/Core.h>
static bool UI_Rml_CompiledRuntimeCanOpenRoutes(void)
{
    return true;
}
void probe(void) { Rml::GetVersion(); Rml::Initialise(); Rml::Shutdown(); }
void UI_Rml_RegisterCompiledRuntime(void) { UI_Rml_SetRuntimeInterface(&runtime); }
#endif
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "missing conservative CanOpenRoutes false guard" in captured.out


def test_opengl_scaffold_must_not_claim_render_ready(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    bridge_path = repo_root / "src/renderer/rmlui_bridge.cpp"
    bridge_path.write_text(
        bridge_path.read_text(encoding="utf-8").replace(
            "bool R_RmlUiCanRender(void)\n{\n    return false;\n}",
            "bool R_RmlUiCanRender(void)\n{\n    return true;\n}",
        ),
        encoding="utf-8",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "OpenGL scaffold must keep CanRender false until it draws" in captured.out


def test_missing_core_interface_install_order_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(
        repo_root,
        adapter_text="""
#include "ui_rml.h"
#if UI_RML_HAS_RUNTIME
#undef DotProduct
#undef CrossProduct
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>
static bool UI_Rml_CompiledRuntimeCanOpenRoutes(void)
{
    return false;
}
static bool UI_Rml_CompiledRuntimeInit(void)
{
    bool ok = Rml::Initialise();
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    return ok;
}
static bool UI_Rml_CompiledRuntimeProbeRoute(const char *, const char *document_path)
{
    Rml::String contents;
    FS_OpenFile(path, &handle, FS_MODE_READ);
    FS_Read(buffer, size, handle);
    FS_Seek(handle, 0, SEEK_SET);
    FS_Tell(handle);
    FS_Length(handle);
    FS_CloseFile(handle);
    Sys_Milliseconds();
    Com_WPrintf("warning");
    Com_Printf("info");
    return Rml::GetFileInterface()->LoadFile(document_path, contents);
}
static const char *UI_Rml_CompiledRuntimeName(void) { Rml::GetVersion(); return "RmlUi"; }
static void UI_Rml_CompiledRuntimeShutdown(void) { Rml::Shutdown(); }
void UI_Rml_RegisterCompiledRuntime(void) { UI_Rml_SetRuntimeInterface(&runtime); }
#endif
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "system/file interfaces must be installed before Initialise" in captured.out


def test_missing_wrap_provide_dependency_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    write_text(
        repo_root / "subprojects/rmlui.wrap",
        """
[wrap-file]
directory = RmlUi-6.2
method = cmake
""",
    )

    result, captured = run_checker(repo_root, capsys)

    assert result == 1
    assert "wrap: [provide] dependency_names must include rmlui" in captured.out
