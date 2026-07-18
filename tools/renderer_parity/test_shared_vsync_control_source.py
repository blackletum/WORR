#!/usr/bin/env python3
"""Lock shared Video vertical sync to native OpenGL and Vulkan presentation."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads(
    (ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8")
)
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(
    encoding="utf-8"
)
VSYNC_CONFIG = (
    ROOT / "assets/renderer_parity/fr01_vsync_runtime.cfg"
).read_text(encoding="utf-8")
VSYNC_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_vsync_runtime_manifest.json").read_text(
        encoding="utf-8"
    )
)


class SharedVSyncControlSourceTests(unittest.TestCase):
    def test_video_routes_bind_the_shared_vsync_cvar(self) -> None:
        self.assertIn('toggle "vertical sync" r_vsync', LEGACY_MENU)
        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        vsync = next(
            item for item in video_menu["items"] if item.get("label") == "vertical sync"
        )
        self.assertEqual(vsync["cvar"], "r_vsync")
        self.assertIn('data-cvar="r_vsync"', RML_VIDEO)

    def test_opengl_keeps_its_platform_swapinterval_spelling_as_an_alias(self) -> None:
        self.assertIn('r_vsync = Cvar_Get("r_vsync", gl_swapinterval->string, CVAR_ARCHIVE);', GL_MAIN)
        self.assertIn("gl_sync_vsync_defaults", GL_MAIN)
        self.assertIn("gl_swapinterval_changed", GL_MAIN)
        self.assertIn("Cvar_ClampInteger(self, 0, 1)", GL_MAIN)
        self.assertIn("vid->swap_interval(value);", GL_MAIN)
        self.assertIn("CVAR_ARCHIVE | CVAR_NOARCHIVE", GL_MAIN)

    def test_vulkan_rebuilds_its_native_swapchain_for_the_shared_control(self) -> None:
        self.assertIn("static void VK_RegisterVSyncCvars(void)", VK_MAIN)
        self.assertIn('Cvar_Get("r_vsync", vk_gl_swapinterval->string,', VK_MAIN)
        self.assertIn("static void VK_VSyncChanged(cvar_t *self)", VK_MAIN)
        self.assertIn("vk_state.swapchain_dirty = true;", VK_MAIN)
        self.assertIn("VK_UnregisterVSyncCvars();", VK_MAIN)
        self.assertIn(
            "VK_ChoosePresentMode(const VkPresentModeKHR *modes,\n"
            "                                              uint32_t count, bool vsync)",
            VK_MAIN,
        )
        self.assertIn("VK_PRESENT_MODE_IMMEDIATE_KHR", VK_MAIN)
        self.assertIn("VK_PRESENT_MODE_MAILBOX_KHR", VK_MAIN)
        self.assertIn("VK_PRESENT_MODE_FIFO_KHR", VK_MAIN)

    def test_headless_fixture_changes_vsync_after_the_vulkan_swapchain_exists(self) -> None:
        self.assertIn("set r_vsync 1", VSYNC_CONFIG)
        self.assertIn("map worr_fr01_world_texture_replace", VSYNC_CONFIG)
        self.assertIn("set r_vsync 0", VSYNC_CONFIG)
        scene = VSYNC_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "runtime_vsync_disable")
        self.assertEqual(scene["capture"], "fr01_vsync_runtime.tga")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 0)
        self.assertEqual(scene["probes"][0]["color"], [38, 38, 38])
        self.assertEqual(scene["probes"][1]["color"], [174, 174, 174])


if __name__ == "__main__":
    unittest.main()
