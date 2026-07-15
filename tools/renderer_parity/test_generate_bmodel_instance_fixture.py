#!/usr/bin/env python3
"""Regression checks for the ordinary inline-BSP instancing fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import generate_bmodel_instance_fixture as fixture


class BmodelInstanceFixtureTests(unittest.TestCase):
    def test_authored_instance_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_is_a_dense_grid_of_ordinary_inline_models(self) -> None:
        self.assertEqual(36, len(fixture.INSTANCE_ENTITIES))
        entity_text = "".join(fixture.INSTANCE_ENTITIES)
        self.assertEqual(36, entity_text.count('"classname" "func_wall"'))
        self.assertEqual(36, entity_text.count('"model" "*1"'))
        self.assertIn('"origin" "0 -206 -300"', entity_text)
        self.assertIn('"origin" "0 494 300"', entity_text)

    def test_generator_writes_only_the_instance_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
