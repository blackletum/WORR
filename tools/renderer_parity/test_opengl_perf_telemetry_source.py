#!/usr/bin/env python3
"""Headless structural checks for the OpenGL performance telemetry contract."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")


class OpenGLPerformanceTelemetrySourceTests(unittest.TestCase):
    def test_machine_readable_stats_include_comparable_metrics(self) -> None:
        self.assertIn('Cmd_AddCommand("gl_stats", GL_Stats_f);', GL_MAIN)
        self.assertIn('Cmd_RemoveCommand("gl_stats");', GL_MAIN)
        self.assertIn('"GL_STATS frame=%u draws=%d vertices=%llu indices=0 uploads=%llu "', GL_MAIN)
        for metric in ("cpu_ms=%.3f", "gpu_ms=%.3f", "gpu_world_ms=%.3f",
                       "gpu_effects_ms=%.3f", "gpu_post_ms=%.3f", "gpu_valid=%d"):
            self.assertIn(metric, GL_MAIN)


if __name__ == "__main__":
    unittest.main()
