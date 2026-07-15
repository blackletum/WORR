#!/usr/bin/env python3

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from PIL import Image

import run_vk_debug_smoke as smoke


class VulkanDebugSmokeTests(unittest.TestCase):
    def test_log_requires_queue_stats_and_caps(self) -> None:
        valid = (
            "VK_DEBUG_TEST status=queued active_lines=42 max_lines=8192\n"
            "VK_STATS frame=7 draws=5 vertices=90 indices=12 uploads=2048 "
            "entities=1 dlights=0 particles=0 queries=0 debug_lines=42 "
            "capacity_hits=0 cpu_ms=1.000 missing_mask=0x00\n"
            "VK_CAPS debug_lines=1 screenshot=1 stencil=1 soft_focus=1\n"
        )
        self.assertEqual(smoke.evaluate_log(valid), [])
        self.assertTrue(smoke.evaluate_log(valid + "VUID-test\n"))

    def test_image_difference_counts_changed_pixels(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            baseline = root / "baseline.png"
            overlay = root / "overlay.png"
            Image.new("RGB", (4, 4), (0, 0, 0)).save(baseline)
            image = Image.new("RGB", (4, 4), (0, 0, 0))
            image.putpixel((1, 2), (64, 0, 0))
            image.save(overlay)
            self.assertEqual(smoke.image_difference(baseline, overlay), (1, 64))


if __name__ == "__main__":
    unittest.main()
