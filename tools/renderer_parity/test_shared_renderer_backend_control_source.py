#!/usr/bin/env python3
"""Lock the Video renderer selector to the native renderer lifecycle."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CLIENT_RENDERER = (ROOT / "src/client/renderer.cpp").read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads(
    (ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8")
)
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(encoding="utf-8")


class SharedRendererBackendControlSourceTests(unittest.TestCase):
    expected_choices = [
        ("OpenGL", "opengl"),
        ("Vulkan", "vulkan"),
        ("Vulkan RTX", "rtx"),
    ]

    def test_all_video_routes_select_the_real_renderer_cvar(self) -> None:
        self.assertIn(
            'pairs "rendering backend" r_renderer OpenGL opengl Vulkan vulkan '
            '"Vulkan RTX" rtx',
            LEGACY_MENU,
        )
        self.assertNotIn('rendering backend" gl_shaders', LEGACY_MENU)

        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        backend = next(
            item
            for item in video_menu["items"]
            if item.get("label") == "rendering backend"
        )
        self.assertEqual(backend["cvar"], "r_renderer")
        self.assertEqual(
            [(pair["label"], pair["value"]) for pair in backend["pairs"]],
            self.expected_choices,
        )

        self.assertIn('data-cvar="r_renderer"', RML_VIDEO)
        for label, value in self.expected_choices:
            self.assertIn(f'<option value="{value}">{label}</option>', RML_VIDEO)
        self.assertNotIn('data-cvar="gl_shaders"', RML_VIDEO)

    def test_native_renderer_values_are_persistent_and_restart_the_renderer(self) -> None:
        self.assertIn('Cvar_Get("r_renderer", "opengl", CVAR_ARCHIVE | CVAR_RENDERER)', CLIENT_RENDERER)
        self.assertIn('Prompt_AddMatch(ctx, "opengl")', CLIENT_RENDERER)
        self.assertIn('Prompt_AddMatch(ctx, "vulkan")', CLIENT_RENDERER)
        self.assertIn('Prompt_AddMatch(ctx, "rtx")', CLIENT_RENDERER)
        self.assertIn('if (cvar_modified & CVAR_RENDERER)', CLIENT_RENDERER)
        self.assertIn('CL_RestartRenderer(true);', CLIENT_RENDERER)

    def test_vulkan_and_rtx_aliases_resolve_to_native_renderer_names(self) -> None:
        self.assertIn('if (!Q_strcasecmp(name, "vk"))', CLIENT_RENDERER)
        self.assertIn('return "vulkan";', CLIENT_RENDERER)
        self.assertIn('if (!Q_strcasecmp(name, "vkpt"))', CLIENT_RENDERER)
        self.assertIn('return "rtx";', CLIENT_RENDERER)


if __name__ == "__main__":
    unittest.main()
