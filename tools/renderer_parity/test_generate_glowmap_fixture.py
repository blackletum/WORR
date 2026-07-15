#!/usr/bin/env python3
"""Regression checks for the generated wall-glowmap fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import generate_glowmap_fixture as fixture


class GlowmapFixtureTests(unittest.TestCase):
    def test_authored_outputs_are_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_companion_is_an_opaque_indexed_pcx(self) -> None:
        pcx = fixture.build_glowmap_pcx()
        self.assertEqual(128 + 2 * fixture.GLOWMAP_SIZE + 769, len(pcx))
        self.assertEqual((10, 5, 1, 8), tuple(pcx[:4]))
        self.assertEqual(1, pcx[65])
        self.assertEqual(fixture.GLOWMAP_SIZE, int.from_bytes(pcx[66:68], "little"))
        self.assertEqual(bytes((0xD0, 1)), pcx[128:130])
        self.assertEqual(12, pcx[-769])
        self.assertEqual(bytes((255, 255, 255)), pcx[-765:-762])

    def test_generator_writes_only_its_map_and_glow_companion(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {
                    asset_root / "maps" / fixture.MAP_NAME,
                    asset_root / "textures" / "parity" / fixture.GLOWMAP_NAME,
                },
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
