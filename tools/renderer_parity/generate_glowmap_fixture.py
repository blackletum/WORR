#!/usr/bin/env python3
"""Generate an authored wall-glowmap parity map and its exact PCX companion."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_glowmap.bsp"
GLOWMAP_NAME = "fr01_bm_bg_glow.pcx"
GLOWMAP_SIZE = 16


def _rle_rows(value: int, width: int, height: int) -> bytes:
    encoded = bytearray()
    for _ in range(height):
        remaining = width
        while remaining:
            count = min(remaining, 63)
            encoded.extend((0xC0 | count, value))
            remaining -= count
    return bytes(encoded)


def build_glowmap_pcx() -> bytes:
    """Return a conventional opaque indexed PCX whose wall alpha is one."""
    size = GLOWMAP_SIZE
    header = struct.pack(
        "<BBBBHHHHHH48sBBHH58s",
        10, 5, 1, 8,
        0, 0, size - 1, size - 1,
        size, size,
        bytes(48),
        0, 1, size, 1,
        bytes(58),
    )
    # Index one remains opaque in the shared Quake palette. Wall glow uses
    # only alpha, while the trailing PCX palette keeps the asset conventional
    # for tools and both native decoders.
    palette = bytearray(768)
    palette[3:6] = bytes((255, 255, 255))
    return header + _rle_rows(1, size, size) + bytes((12,)) + bytes(palette)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(),
        asset_root / "textures" / "parity" / GLOWMAP_NAME: build_glowmap_pcx(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", type=Path, default=Path("assets"))
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    outputs = generated_outputs(args.asset_root)
    mismatches = [
        str(path)
        for path, expected in outputs.items()
        if not path.is_file() or path.read_bytes() != expected
    ]
    if args.validate:
        if mismatches:
            raise SystemExit("generated fixture mismatch: " + ", ".join(mismatches))
    else:
        for path, data in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    report = {
        "schema": "worr.renderer-parity.glowmap-fixture.v1",
        "outputs": [
            {
                "path": str(path),
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            for path, data in outputs.items()
        ],
    }
    print(
        json.dumps(report, indent=2, sort_keys=True)
        if args.json
        else f"{'validated' if args.validate else 'generated'} "
        f"{len(outputs)} glowmap fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
