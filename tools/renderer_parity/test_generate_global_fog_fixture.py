#!/usr/bin/env python3
"""Regression coverage for the generated authored-global-fog map."""

from __future__ import annotations

import unittest
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "renderer_parity"))

import generate_global_fog_fixture as fixture


class GlobalFogFixtureTests(unittest.TestCase):
    def test_authored_worldspawn_fog_map_is_current(self) -> None:
        outputs = fixture.generated_outputs(ROOT / "assets")
        self.assertEqual(len(outputs), 1)
        path, expected = next(iter(outputs.items()))
        self.assertEqual(path.name, fixture.MAP_NAME)
        self.assertEqual(path.read_bytes(), expected)

    def test_fixture_carries_stable_global_and_sky_fog_inputs(self) -> None:
        payload = next(iter(fixture.generated_outputs(ROOT / "assets").values()))
        self.assertIn(b'"fog_color" "0.30 0.50 0.70"', payload)
        self.assertIn(b'"fog_density" "0.50"', payload)
        self.assertIn(b'"fog_sky_factor" "0.60"', payload)


if __name__ == "__main__":
    unittest.main()
