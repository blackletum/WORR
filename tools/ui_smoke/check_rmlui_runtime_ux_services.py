#!/usr/bin/env python3
"""Validate shared RmlUi navigation, localization, accessibility, and scaling services."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_INPUTS = {
    "adapter": Path("src/client/ui_rml/ui_rml_runtime.cpp"),
    "scaffold": Path("src/client/ui_rml/ui_rml.cpp"),
    "keys": Path("src/client/keys.cpp"),
    "accessibility_document": Path("assets/ui/rml/settings/accessibility.rml"),
    "accessibility_theme": Path("assets/ui/rml/common/theme/accessibility.rcss"),
    "base_theme": Path("assets/ui/rml/common/theme/base.rcss"),
    "startserver_document": Path("assets/ui/rml/singleplayer/startserver.rml"),
    "players_document": Path("assets/ui/rml/utility/players.rml"),
    "rmlui_bridge": Path("src/renderer/rmlui_bridge.cpp"),
    "vulkan_entity_source": Path("src/rend_vk/vk_entity.c"),
    "vulkan_main_source": Path("src/rend_vk/vk_main.c"),
    "rtx_debug_source": Path("src/rend_rtx/vkpt/debug.c"),
    "rtx_draw_source": Path("src/rend_rtx/vkpt/draw.c"),
    "rtx_main_source": Path("src/rend_rtx/vkpt/main.c"),
    "rtx_shader_constants": Path("src/rend_rtx/vkpt/shader/constants.h"),
    "rtx_shader_path_tracer": Path("src/rend_rtx/vkpt/shader/path_tracer_rgen.h"),
    "rtx_texture_source": Path("src/rend_rtx/vkpt/textures.c"),
}


@dataclass
class RuntimeUxServicesReport:
    repo_root: Path
    paths: dict[str, Path]
    files: dict[str, bool] = field(default_factory=dict)
    facts: dict[str, bool] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_path(path: Path, repo_root: Path) -> Path:
    return path.resolve() if path.is_absolute() else (repo_root / path).resolve()


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.relative_to(repo_root).as_posix()
    except ValueError:
        return str(path)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def validate_runtime_ux_services(
    repo_root: Path,
    paths: dict[str, Path] = DEFAULT_INPUTS,
) -> RuntimeUxServicesReport:
    repo_root = repo_root.resolve()
    resolved = {name: resolve_path(path, repo_root) for name, path in paths.items()}
    texts = {name: read_text(path) for name, path in resolved.items()}
    report = RuntimeUxServicesReport(
        repo_root=repo_root,
        paths=resolved,
        files={name: path.is_file() for name, path in resolved.items()},
    )

    for name, exists in report.files.items():
        if not exists:
            report.errors.append(f"missing {name} input")

    adapter = texts["adapter"]
    scaffold = texts["scaffold"]
    keys = texts["keys"]
    document = texts["accessibility_document"]
    accessibility = texts["accessibility_theme"]
    base = texts["base_theme"]
    startserver = texts["startserver_document"]
    players = texts["players_document"]
    rmlui_bridge = texts["rmlui_bridge"]
    vulkan_entity = texts["vulkan_entity_source"]
    vulkan_main = texts["vulkan_main_source"]
    rtx_debug = texts["rtx_debug_source"]
    rtx_draw = texts["rtx_draw_source"]
    rtx_main = texts["rtx_main_source"]
    rtx_constants = texts["rtx_shader_constants"]
    rtx_path_tracer = texts["rtx_shader_path_tracer"]
    rtx_textures = texts["rtx_texture_source"]

    checks = {
        "keyboard_navigation_active": (
            all(token in adapter for token in (
                "ProcessKeyDown", "ProcessKeyUp", "FindNextTabElement",
                "Focus(true)", "ScrollIntoView", "Rml::Input::KI_TAB",
                "Rml::Input::KI_UP", "Rml::Input::KI_DOWN",
                "Rml::Input::KI_LEFT", "Rml::Input::KI_RIGHT",
            ))
            and "nav: auto" in base
            and "tab-index: auto" in base
        ),
        "gamepad_navigation_active": all(token in keys for token in (
            "Key_MapGamepadToUi", "K_DPAD_UP", "K_DPAD_DOWN",
            "K_DPAD_LEFT", "K_DPAD_RIGHT", "K_A_BUTTON", "K_B_BUTTON",
        )),
        "escape_back_active": all(token in scaffold for token in (
            "K_ESCAPE", "K_MOUSE2", "HandleBackKey", "UI_Rml_RuntimeBackRoute_f",
        )),
        "localization_hooks_consumed": all(token in adapter for token in (
            'QuerySelectorAll(elements, "[data-loc-key]")',
            "Loc_Localize", 'Cvar_Get("loc_language"',
            "ui_rml_localization_language->modified_count",
            "UI_Rml_ApplyDocumentLocalization(ui_rml_document, true)",
            "UI_Rml_ApplyDocumentLocalization(ui_rml_document, false)",
            'GetAttribute<Rml::String>("data-loc-source-rml"',
            "missing_translation", "SetInnerRML(source_rml)",
        )),
        "large_text_preference_active": all(token in adapter for token in (
            'Cvar_Get("ui_rml_large_text"',
            'SetClass("ui-a11y-large-text"',
            'classes += "ui-a11y-large-text"',
        )) and all(token in document for token in (
            'data-cvar="ui_rml_large_text"',
            'id="accessibility-menu-large-text"',
        )) and all(token in accessibility for token in (
            ".ui-a11y-large-text", "min-height: 48px", "font-size: 21px",
            ".ui-a11y-large-text button.worr-tab", "flex: 1 1 112px",
            "white-space: normal", "overflow: visible",
        )),
        "high_visibility_preference_active": (
            'Cvar_Get("ui_rml_high_visibility"' in adapter
            and 'SetClass("ui-high-visibility"' in adapter
            and 'data-cvar="ui_rml_high_visibility"' in document
            and ".ui-high-visibility" in accessibility
        ),
        "reduced_motion_preference_active": (
            'Cvar_Get("ui_rml_reduced_motion"' in adapter
            and 'SetClass("ui-reduced-motion"' in adapter
            and 'data-cvar="ui_rml_reduced_motion"' in document
            and "animation: none" in accessibility
            and "transition: none" in accessibility
        ),
        "responsive_canvas_scaling_active": all(token in adapter + "\n" + scaffold for token in (
            "UI_Rml_CanvasScale", "UI_RML_REFERENCE_WIDTH",
            "UI_RML_REFERENCE_HEIGHT", "SetDimensions", "R_SetScale",
            "UI_Rml_MouseFromFramebuffer",
        )),
        "dynamic_image_fallback_active": all(token in adapter for token in (
            "UI_Rml_ImageSourceExists", "FS_FileExists",
            'GetAttribute<Rml::String>("data-src-fallback"',
        )) and 'data-src-fallback="../common/skins/metal/cardart-host.png"' in startserver
        and players.count("data-src-fallback=") >= 2,
        "rtx_player_preview_debug_state_initialized": (
            "VkResult vkpt_debugdraw_create(void)" in rtx_debug
            and "R_ClearDebugLines();" in rtx_debug
        ),
        "vulkan_player_preview_native_overlay": all(token in vulkan_entity for token in (
            "frame_view_rect", "frame_uses_view_rect", "RDF_NOWORLDMODEL",
            "VK_Entity_IsNoWorldSubview", "vkCmdSetViewport", "vkCmdSetScissor",
        )) and all(token in vulkan_main for token in (
            "overlay_render_pass", "VK_ATTACHMENT_LOAD_OP_LOAD",
            "VK_Entity_IsNoWorldSubview", "VK_Entity_Record",
        )),
        "vulkan_menu_soft_focus_native": all(token in vulkan_main for token in (
            "VK_SoftFocus_Record", "vk_soft_focus_strength",
            "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
            "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL", "VK_FILTER_LINEAR",
            "ui_overlay_info.renderPass = ctx->overlay_render_pass",
        )),
        "rtx_player_preview_native_overlay": (
            all(token in rtx_main for token in (
                "MATERIAL_FLAG_FULLBRIGHT", "vkpt_final_blit_rect",
                "preview_scissor", "RDF_NOWORLDMODEL",
                "fd->y * 2 + fd->height",
            ))
            and all(token in rtx_draw for token in (
                "vkpt_final_blit_rect", "render_pass_info.renderArea = *scissor",
                "vkCmdSetScissor",
            ))
            and "#define MATERIAL_FLAG_FULLBRIGHT" in rtx_constants
            and "triangle.material_id & MATERIAL_FLAG_FULLBRIGHT" in rtx_path_tracer
        ),
        "rtx_rmlui_repeat_textures_native": (
            "q_img->flags & IF_REPEAT" in rtx_textures
            and "sampler = qvk.tex_sampler;" in rtx_textures
            and rtx_textures.index("q_img->flags & IF_REPEAT")
                < rtx_textures.index("q_img->type == IT_SPRITE")
        ),
        "rmlui_source_textures_srgb": (
            "R_RegisterImage(" in rmlui_bridge
            and "RENDERER_VULKAN_RTX" in rmlui_bridge
            and "image_flags | IF_SRGB" in rmlui_bridge
        ),
    }

    messages = {
        "keyboard_navigation_active": "keyboard focus, spatial navigation, and scroll-into-view services are incomplete",
        "gamepad_navigation_active": "gamepad D-pad/confirm/back mappings are incomplete",
        "escape_back_active": "Escape/Mouse2 back routing is incomplete",
        "localization_hooks_consumed": "data-loc-key hooks are not consumed and refreshed by the live localization service",
        "large_text_preference_active": "large menu text is not wired through cvar, runtime class, settings, and RCSS",
        "high_visibility_preference_active": "high-visibility menu preference is incomplete",
        "reduced_motion_preference_active": "reduced-motion menu preference is incomplete",
        "responsive_canvas_scaling_active": "responsive canvas, renderer scale, or mouse conversion is incomplete",
        "dynamic_image_fallback_active": "dynamic menu images do not preserve authored fallbacks when runtime assets are absent",
        "rtx_player_preview_debug_state_initialized": "RTX debug-line state is not initialized before the player preview's first no-world frame",
        "vulkan_player_preview_native_overlay": "Vulkan does not preserve and overlay the no-world player-preview subview natively",
        "vulkan_menu_soft_focus_native": "Vulkan does not apply menu soft focus before its native UI overlay",
        "rtx_player_preview_native_overlay": "RTX does not preserve fullbright player materials and composite the no-world preview rectangle natively",
        "rtx_rmlui_repeat_textures_native": "RTX does not honor repeat sampling for native RmlUi decorator textures",
        "rmlui_source_textures_srgb": "authored RmlUi source textures are not registered with their sRGB color-space contract",
    }
    report.facts.update(checks)
    for name, passed in checks.items():
        if not passed:
            report.errors.append(messages[name])
    return report


def json_payload(report: RuntimeUxServicesReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "paths": {name: display_path(path, report.repo_root) for name, path in report.paths.items()},
        "files": report.files,
        "facts": report.facts,
        "counts": {
            "services": len(report.facts),
            "passed": sum(report.facts.values()),
            "failed": sum(not value for value in report.facts.values()),
            "errors": len(report.errors),
        },
        "errors": report.errors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args(argv)
    report = validate_runtime_ux_services(args.repo_root)
    if args.format == "json":
        print(json.dumps(json_payload(report), indent=2, sort_keys=True))
    else:
        print("RmlUi runtime UX services:")
        for name, passed in report.facts.items():
            print(f"  {name}: {'yes' if passed else 'no'}")
        for error in report.errors:
            print(f"  - {error}")
        print(f"Result: RmlUi runtime UX services check {'passed' if report.ok() else 'failed'}.")
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
