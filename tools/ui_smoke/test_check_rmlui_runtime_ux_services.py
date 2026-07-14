from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_runtime_ux_services as ux_services  # noqa: E402


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_valid_repo(repo_root: Path) -> None:
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["adapter"],
        """
Cvar_Get("loc_language", "auto", CVAR_ARCHIVE);
Cvar_Get("ui_rml_large_text", "0", CVAR_ARCHIVE);
Cvar_Get("ui_rml_high_visibility", "0", CVAR_ARCHIVE);
Cvar_Get("ui_rml_reduced_motion", "0", CVAR_ARCHIVE);
document->SetClass("ui-a11y-large-text", large_text);
document->SetClass("ui-high-visibility", high_visibility);
document->SetClass("ui-reduced-motion", reduced_motion);
classes += "ui-a11y-large-text";
document->QuerySelectorAll(elements, "[data-loc-key]");
Loc_Localize(key, false, NULL, 0, out, size);
ui_rml_localization_language->modified_count;
UI_Rml_ApplyDocumentLocalization(ui_rml_document, true);
UI_Rml_ApplyDocumentLocalization(ui_rml_document, false);
GetAttribute<Rml::String>("data-loc-source-rml"); missing_translation; SetInnerRML(source_rml);
ProcessKeyDown(); ProcessKeyUp(); FindNextTabElement(); Focus(true); ScrollIntoView();
Rml::Input::KI_TAB; Rml::Input::KI_UP; Rml::Input::KI_DOWN;
Rml::Input::KI_LEFT; Rml::Input::KI_RIGHT;
SetDimensions(); R_SetScale(); UI_Rml_CanvasScale(); UI_Rml_MouseFromFramebuffer();
UI_Rml_ImageSourceExists(); FS_FileExists(); GetAttribute<Rml::String>("data-src-fallback");
""",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["scaffold"],
        """
#define UI_RML_REFERENCE_WIDTH 960
#define UI_RML_REFERENCE_HEIGHT 720
K_ESCAPE; K_MOUSE2; HandleBackKey; UI_Rml_RuntimeBackRoute_f;
UI_Rml_CanvasScale(); R_SetScale(); UI_Rml_MouseFromFramebuffer();
""",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["keys"],
        "Key_MapGamepadToUi K_DPAD_UP K_DPAD_DOWN K_DPAD_LEFT K_DPAD_RIGHT K_A_BUTTON K_B_BUTTON",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["accessibility_document"],
        '<input id="accessibility-menu-large-text" data-cvar="ui_rml_large_text" />\n'
        '<input data-cvar="ui_rml_high_visibility" />\n'
        '<input data-cvar="ui_rml_reduced_motion" />',
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["accessibility_theme"],
        ".ui-a11y-large-text { font-size: 21px; min-height: 48px; }\n"
        ".ui-a11y-large-text button.worr-tab { flex: 1 1 112px; white-space: normal; overflow: visible; }\n"
        ".ui-high-visibility {}\n.ui-reduced-motion { animation: none; transition: none; }",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["base_theme"],
        "button { nav: auto; tab-index: auto; }",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["startserver_document"],
        '<img data-src-fallback="../common/skins/metal/cardart-host.png" />',
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["players_document"],
        '<img data-src-fallback="/players/male/grunt_i.pcx" />\n'
        '<img data-src-fallback="/tags/default.pcx" />',
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["rmlui_bridge"],
        "RENDERER_VULKAN_RTX image_flags | IF_SRGB "
        "R_RegisterImage(source, IT_SPRITE, image_flags)",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["rtx_debug_source"],
        "VkResult vkpt_debugdraw_create(void) { R_ClearDebugLines(); }",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["vulkan_entity_source"],
        "frame_view_rect frame_uses_view_rect RDF_NOWORLDMODEL "
        "VK_Entity_IsNoWorldSubview vkCmdSetViewport vkCmdSetScissor",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["vulkan_main_source"],
        "overlay_render_pass VK_ATTACHMENT_LOAD_OP_LOAD "
        "VK_Entity_IsNoWorldSubview VK_Entity_Record",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["rtx_draw_source"],
        "vkpt_final_blit_rect render_pass_info.renderArea = *scissor vkCmdSetScissor",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["rtx_main_source"],
        "MATERIAL_FLAG_FULLBRIGHT vkpt_final_blit_rect preview_scissor "
        "RDF_NOWORLDMODEL fd->y * 2 + fd->height",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["rtx_shader_constants"],
        "#define MATERIAL_FLAG_FULLBRIGHT 0x04000000",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["rtx_shader_path_tracer"],
        "triangle.material_id & MATERIAL_FLAG_FULLBRIGHT",
    )
    write_text(
        repo_root / ux_services.DEFAULT_INPUTS["rtx_texture_source"],
        "q_img->flags & IF_REPEAT sampler = qvk.tex_sampler; "
        "q_img->type == IT_SPRITE",
    )


def test_valid_runtime_ux_services_pass(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    result = ux_services.main(["--repo-root", str(repo_root), "--format", "json"])
    payload = json.loads(capsys.readouterr().out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["counts"] == {"errors": 0, "failed": 0, "passed": 14, "services": 14}


def test_missing_live_localization_fails(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    adapter = repo_root / ux_services.DEFAULT_INPUTS["adapter"]
    adapter.write_text(adapter.read_text(encoding="utf-8").replace("Loc_Localize", "Missing_Localize"), encoding="utf-8")
    result = ux_services.main(["--repo-root", str(repo_root)])
    assert result == 1
    assert "data-loc-key hooks are not consumed" in capsys.readouterr().out


def test_missing_large_text_setting_fails(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    document = repo_root / ux_services.DEFAULT_INPUTS["accessibility_document"]
    document.write_text(document.read_text(encoding="utf-8").replace("ui_rml_large_text", "ui_rml_missing_text"), encoding="utf-8")
    result = ux_services.main(["--repo-root", str(repo_root)])
    assert result == 1
    assert "large menu text is not wired" in capsys.readouterr().out


def test_missing_rtx_preview_debug_initialization_fails(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    source = repo_root / ux_services.DEFAULT_INPUTS["rtx_debug_source"]
    source.write_text(
        source.read_text(encoding="utf-8").replace("R_ClearDebugLines();", ""),
        encoding="utf-8",
    )
    result = ux_services.main(["--repo-root", str(repo_root)])
    assert result == 1
    assert "RTX debug-line state is not initialized" in capsys.readouterr().out


def test_missing_native_preview_overlay_fails(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    repo_root = tmp_path / "repo"
    write_valid_repo(repo_root)
    source = repo_root / ux_services.DEFAULT_INPUTS["vulkan_main_source"]
    source.write_text(
        source.read_text(encoding="utf-8").replace("VK_ATTACHMENT_LOAD_OP_LOAD", "VK_ATTACHMENT_LOAD_OP_CLEAR"),
        encoding="utf-8",
    )
    result = ux_services.main(["--repo-root", str(repo_root)])
    assert result == 1
    assert "Vulkan does not preserve and overlay" in capsys.readouterr().out
