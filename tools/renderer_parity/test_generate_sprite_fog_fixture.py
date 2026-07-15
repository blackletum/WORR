#!/usr/bin/env python3
"""Regression checks for the deterministic sprite-fog fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import generate_sprite_fog_fixture as fixture


class SpriteFogFixtureTests(unittest.TestCase):
    def test_authored_sprite_fog_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_uses_a_real_sprite_game_entity(self) -> None:
        entity_text = "".join(fixture.SPRITE_ENTITIES)
        self.assertIn('"classname" "misc_model"', entity_text)
        self.assertIn('"model" "sprites/s_bfg1.sp2"', entity_text)
        self.assertIn('"scale" "4"', entity_text)

    def test_generator_writes_only_the_sprite_fog_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
