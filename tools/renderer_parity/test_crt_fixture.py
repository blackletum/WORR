#!/usr/bin/env python3
"""Regression checks for the deterministic CRT post-process parity scene."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_crt.cfg"
MANIFEST_PATH = ROOT / "assets/renderer_parity/fr01_crt_manifest.json"


class CrtFixtureTests(unittest.TestCase):
    def test_config_isolates_the_shared_crt_contract(self) -> None:
        config = CONFIG_PATH.read_text(encoding="utf-8")
        for line in (
            "set gl_bloom 0",
            "set vk_bloom 0",
            "set r_dof 0",
            "set gl_color_correction 0",
            "set vk_color_correction 0",
            "set r_crtmode 1",
            "set r_crt_brightboost 1.5",
            "set r_crt_hard_pix -8",
            "set r_crt_hard_scan -8",
            "set r_crt_mask_dark 0.5",
            "set r_crt_mask_light 1.5",
            "set r_crt_scale_in_linear_gamma 1",
            "set r_crt_shadow_mask 0",
        ):
            self.assertIn(line, config)
        masked_config = (
            ROOT / "assets/renderer_parity/fr01_crt_masked.cfg"
        ).read_text(encoding="utf-8")
        self.assertIn("set r_crtmode 1", masked_config)
        self.assertIn("set r_crt_shadow_mask 2", masked_config)

    def test_manifest_locks_the_crt_scanline_phase(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("FR-01-T12", manifest["task_id"])
        scene = manifest["scenes"][0]
        self.assertEqual("crt_fixed_view", scene["id"])
        self.assertEqual("renderer_parity/fr01_crt.cfg", scene["config"])
        self.assertEqual("fr01_crt.tga", scene["capture"])
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
                "name": "crt_dark_scanline_phase",
                "color": [14, 28, 55],
                "tolerance": 0,
                "min_pixels_per_backend": 25000,
                "max_backend_count_delta_percent": 0,
                "min_backend_intersection_over_union": 1,
            },
            scene["probes"][0],
        )

        masked_scene = manifest["scenes"][1]
        self.assertEqual("crt_shadow_mask_fixed_view", masked_scene["id"])
        self.assertEqual("renderer_parity/fr01_crt_masked.cfg", masked_scene["config"])
        self.assertEqual("fr01_crt_masked.tga", masked_scene["capture"])
        self.assertEqual([100, 100, 250, 200], masked_scene["crop"])
        self.assertEqual(
            {
                "pixel_threshold": 0,
                "max_mean_absolute_rgb": [0, 0, 0],
                "max_pixels_over_threshold_percent": 0,
            },
            masked_scene["metrics"],
        )
        self.assertEqual(
            {
                "name": "crt_shadow_mask_green_phase",
                "color": [24, 74, 78],
                "tolerance": 0,
                "min_pixels_per_backend": 8400,
                "max_backend_count_delta_percent": 0,
                "min_backend_intersection_over_union": 1,
            },
            masked_scene["probes"][0],
        )


if __name__ == "__main__":
    unittest.main()
