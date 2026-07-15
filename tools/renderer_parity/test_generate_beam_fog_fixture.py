#!/usr/bin/env python3
"""Regression checks for the deterministic beam-fog fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import generate_beam_fog_fixture as fixture


class BeamFogFixtureTests(unittest.TestCase):
    def test_authored_beam_fog_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        expected = fixture.generated_outputs(asset_root)
        for path, data in expected.items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_carries_stable_global_fog_and_native_beam_inputs(self) -> None:
        self.assertIn('"fog_density" "0.50"', "".join(fixture.FOG_WORLDSPAWN_PROPERTIES))
        entity_text = "".join(fixture.BEAM_ENTITIES)
        self.assertIn('"classname" "target_laser"', entity_text)
        self.assertIn('"spawnflags" "65"', entity_text)
        self.assertIn('"rgba" "224 224 224 224"', entity_text)

    def test_generator_writes_only_the_beam_fog_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            outputs = fixture.generated_outputs(asset_root)
            self.assertEqual({asset_root / "maps" / fixture.MAP_NAME}, set(outputs))


if __name__ == "__main__":
    unittest.main()
