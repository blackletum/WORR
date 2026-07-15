#!/usr/bin/env python3
"""Generate the deterministic global-fog laser-beam renderer-parity map."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_beam_fog.bsp"
FOG_WORLDSPAWN_PROPERTIES = (
    '"fog_color" "0.30 0.50 0.70"\n',
    '"fog_density" "0.50"\n',
    '"fog_sky_factor" "0.60"\n',
)
# target_laser starts after the map has settled, then emits a native RF_BEAM
# across the fogged world plane. START_ON | FAT gives it a stable geometry;
# all four RGBA bytes use palette index 224 so the renderer's beam palette
# selection cannot introduce a capture-phase color change.
BEAM_ENTITIES = (
    '{\n'
    '"classname" "target_laser"\n'
    '"origin" "64 -64 -22"\n'
    '"angle" "0"\n'
    '"spawnflags" "65"\n'
    '"rgba" "224 224 224 224"\n'
    '}\n',
)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            FOG_WORLDSPAWN_PROPERTIES, BEAM_ENTITIES
        ),
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
        "schema": "worr.renderer-parity.beam-fog-fixture.v1",
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
        else f"{'validated' if args.validate else 'generated'} {len(outputs)} beam-fog fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
