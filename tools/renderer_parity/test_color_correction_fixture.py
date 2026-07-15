#!/usr/bin/env python3
"""Regression checks for the deterministic colour-correction parity scene."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_color_correction.cfg"
MANIFEST_PATH = ROOT / "assets/renderer_parity/fr01_color_correction_manifest.json"


class ColorCorrectionFixtureTests(unittest.TestCase):
    def test_config_isolates_a_matching_non_identity_combined_transform(self) -> None:
        config = CONFIG_PATH.read_text(encoding="utf-8")
        for line in (
            "set gl_bloom 0",
            "set vk_bloom 0",
            "set r_dof 0",
            "set r_crtmode 0",
            'set gl_color_lut ""',
            'set vk_color_lut ""',
            "set gl_color_split_strength 0",
            "set vk_color_split_strength 0",
            "set gl_color_correction 1",
            "set vk_color_correction 1",
            "set gl_color_brightness 0.10",
            "set vk_color_brightness 0.10",
            "set gl_color_contrast 1.20",
            "set vk_color_contrast 1.20",
            "set gl_color_saturation 0.70",
            "set vk_color_saturation 0.70",
            "set gl_color_tint cyan",
            "set vk_color_tint cyan",
        ):
            self.assertIn(line, config)

    def test_manifest_targets_the_fixed_colour_correction_capture(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("FR-01-T12", manifest["task_id"])
        scene = manifest["scenes"][0]
        self.assertEqual("combined_color_correction_fixed_view", scene["id"])
        self.assertEqual("renderer_parity/fr01_color_correction.cfg", scene["config"])
        self.assertEqual("fr01_color_correction.tga", scene["capture"])
        self.assertEqual([100, 100, 250, 200], scene["crop"])
        self.assertEqual(
            {
                "pixel_threshold": 0,
                "max_mean_absolute_rgb": [0, 0, 0],
                "max_pixels_over_threshold_percent": 0,
            },
            scene["metrics"],
        )
        self.assertEqual(
            {
                "name": "combined_colour_transform_output",
                "color": [0, 48, 74],
                "tolerance": 0,
                "min_pixels_per_backend": 50000,
                "max_backend_count_delta_percent": 0,
                "min_backend_intersection_over_union": 1,
            },
            scene["probes"][0],
        )


if __name__ == "__main__":
    unittest.main()
