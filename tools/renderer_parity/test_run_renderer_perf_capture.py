#!/usr/bin/env python3
"""Tests for the hidden paired renderer telemetry capture runner."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("run_renderer_perf_capture.py")
SPEC = importlib.util.spec_from_file_location("renderer_perf_capture", SCRIPT)
assert SPEC and SPEC.loader
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


class RendererPerfCaptureTests(unittest.TestCase):
    def test_config_hash_covers_transitive_exec_includes_and_capture_profile(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            install = Path(temp)
            config_dir = install / "basew" / "renderer_parity"
            config_dir.mkdir(parents=True)
            primary = config_dir / "primary.cfg"
            included = config_dir / "common.cfg"
            primary.write_text("exec renderer_parity/common.cfg\n", encoding="utf-8")
            included.write_text("set r_fullbright 1\n", encoding="utf-8")
            first = CAPTURE.config_tree_sha256(install, "renderer_parity/primary.cfg")
            included.write_text("set r_fullbright 0\n", encoding="utf-8")
            second = CAPTURE.config_tree_sha256(install, "renderer_parity/primary.cfg")
            self.assertNotEqual(first, second)

    def test_command_uses_the_explicit_hidden_native_surface_for_each_renderer(self) -> None:
        command = CAPTURE.build_command(
            Path("C:/stage/worr_x86_64.exe"), Path("C:/stage"),
            Path("C:/run/home"), "vulkan",
            "renderer_parity/fr01_renderer_perf_bmodel.cfg",
        )
        self.assertIn("win_headless", command)
        self.assertEqual(command[command.index("win_headless") + 1], "1")
        self.assertEqual(command[command.index("in_enable") + 1], "0")
        self.assertEqual(command[command.index("in_grab") + 1], "0")
        self.assertIn("r_fullscreen", command)
        self.assertEqual(command[command.index("r_fullscreen") + 1], "0")
        self.assertIn("r_renderer", command)
        self.assertEqual(command[command.index("r_renderer") + 1], "vulkan")
        self.assertIn("CREATE_NO_WINDOW", SCRIPT.read_text(encoding="utf-8"))
        self.assertIn("stdin=subprocess.DEVNULL", SCRIPT.read_text(encoding="utf-8"))

    def test_rmlui_mode_is_hashed_and_requires_real_vulkan_ui_uploads(self) -> None:
        command = CAPTURE.build_command(
            Path("C:/stage/worr_x86_64.exe"), Path("C:/stage"),
            Path("C:/run/home"), "vulkan",
            "renderer_parity/fr01_renderer_perf_rmlui.cfg", True,
        )
        self.assertEqual(command[command.index("ui_rml_enable") + 1], "1")
        self.assertNotEqual(
            CAPTURE.capture_profile(False), CAPTURE.capture_profile(True),
        )
        self.assertIn("RMLUI_CAPTURE_MARKER", SCRIPT.read_text(encoding="utf-8"))
        self.assertIn(r"ui_uploads=[1-9][0-9]*", SCRIPT.read_text(encoding="utf-8"))

    def test_adapter_parser_requires_the_active_native_adapter_from_each_log(self) -> None:
        self.assertEqual(
            CAPTURE.renderer_adapter("Vulkan device: Intel Iris Xe\n", "vulkan"),
            "Intel Iris Xe",
        )
        self.assertEqual(
            CAPTURE.renderer_adapter("GL_RENDERER: Intel Iris Xe\n", "opengl"),
            "Intel Iris Xe",
        )
        self.assertIsNone(CAPTURE.renderer_adapter("no adapter\n", "vulkan"))


if __name__ == "__main__":
    unittest.main()
