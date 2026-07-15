#!/usr/bin/env python3
"""Regression coverage for the generated authored-height-fog map."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "renderer_parity"))

import generate_height_fog_fixture as fixture


class HeightFogFixtureTests(unittest.TestCase):
    def test_authored_worldspawn_height_fog_map_is_current(self) -> None:
        outputs = fixture.generated_outputs(ROOT / "assets")
        self.assertEqual(len(outputs), 1)
        path, expected = next(iter(outputs.items()))
        self.assertEqual(path.name, fixture.MAP_NAME)
        self.assertEqual(path.read_bytes(), expected)

    def test_fixture_carries_stable_height_fog_inputs(self) -> None:
        payload = next(iter(fixture.generated_outputs(ROOT / "assets").values()))
        for field in fixture.HEIGHT_FOG_WORLDSPAWN_PROPERTIES:
            self.assertIn(field.encode("ascii").rstrip(b"\n"), payload)


if __name__ == "__main__":
    unittest.main()
