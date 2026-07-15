#!/usr/bin/env python3
"""Generate the deterministic global-fog flare renderer-parity map."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import _tga, build_bsp


MAP_NAME = "worr_fr01_flare_fog.bsp"
FOG_WORLDSPAWN_PROPERTIES = (
    '"fog_color" "0.30 0.50 0.70"\n',
    '"fog_density" "0.50"\n',
    '"fog_sky_factor" "0.60"\n',
)
# misc_flare exercises the regular extended snapshot -> cgame -> RF_FLARE
# pipeline, including the native occlusion query. The location clears the
# shared fixture's inline BSP box, while LOCK_ANGLE keeps the visual fan stable
# in screen space. Explicit fade limits guarantee full alpha at the fixed view.
FLARE_ENTITIES = (
    '{\n'
    '"classname" "misc_flare"\n'
    '"origin" "256 -64 -22"\n'
    '"radius" "4"\n'
    '"fade_start_dist" "0"\n'
    '"fade_end_dist" "1"\n'
    '"image" "textures/parity/fr01_flare.tga"\n'
    '"spawnflags" "9"\n'
    '}\n',
)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(
            FOG_WORLDSPAWN_PROPERTIES, FLARE_ENTITIES
        ),
        asset_root / "textures/parity/fr01_flare.tga": _tga((255, 255, 255)),
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
        "schema": "worr.renderer-parity.flare-fog-fixture.v1",
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
        f"{len(outputs)} flare-fog fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
