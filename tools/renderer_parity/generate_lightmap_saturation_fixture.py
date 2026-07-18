#!/usr/bin/env python3
"""Generate coloured static-world and inline-BSP lightmaps for saturation parity."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_lightmap_saturation.bsp"
LIGHTMAP_RGB = (48, 80, 144)
BMODEL_LIGHTMAP_RGB = (64, 112, 192)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            world_lightmap_rgb=LIGHTMAP_RGB,
            bmodel_lightmap_rgb=BMODEL_LIGHTMAP_RGB,
            # The BSP loader reserves offset zero as the unlit sentinel.
            # Prefix one unused RGB triplet so the world face receives a
            # positive authored-lightmap offset as well.
            light_data_prefix_bytes=3,
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", type=Path, default=Path("assets"))
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    outputs = generated_outputs(args.asset_root)
    mismatches = [str(path) for path, data in outputs.items()
                  if not path.is_file() or path.read_bytes() != data]
    if args.validate:
        if mismatches:
            raise SystemExit("generated fixture mismatch: " + ", ".join(mismatches))
    else:
        for path, data in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    report = {"schema": "worr.renderer-parity.lightmap-saturation-fixture.v1",
              "outputs": [{"path": str(path), "bytes": len(data),
                            "sha256": hashlib.sha256(data).hexdigest()}
                           for path, data in outputs.items()]}
    print(json.dumps(report, indent=2, sort_keys=True) if args.json else
          f"{'validated' if args.validate else 'generated'} {len(outputs)} lightmap saturation fixture output(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
