#!/usr/bin/env python3
"""Regression checks for the deterministic flare-fog fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import generate_flare_fog_fixture as fixture


class FlareFogFixtureTests(unittest.TestCase):
    def test_authored_flare_fog_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_uses_the_real_fogged_misc_flare_path(self) -> None:
        self.assertIn('"fog_density" "0.50"', "".join(fixture.FOG_WORLDSPAWN_PROPERTIES))
        entity_text = "".join(fixture.FLARE_ENTITIES)
        self.assertIn('"classname" "misc_flare"', entity_text)
        self.assertIn('"origin" "256 -64 -22"', entity_text)
        self.assertIn('"radius" "4"', entity_text)
        self.assertIn('"fade_end_dist" "1"', entity_text)
        self.assertIn('"image" "textures/parity/fr01_flare.tga"', entity_text)
        self.assertIn('"spawnflags" "9"', entity_text)

    def test_generator_writes_only_the_flare_fog_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {
                    asset_root / "maps" / fixture.MAP_NAME,
                    asset_root / "textures/parity/fr01_flare.tga",
                },
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
