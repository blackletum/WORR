#!/usr/bin/env python3
"""Generate the dense ordinary-inline-BSP instancing parity fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generate_bmodel_first_frame_fixture import build_bsp


MAP_NAME = "worr_fr01_bmodel_instances.bsp"
INSTANCE_COLUMNS = 6
INSTANCE_ROWS = 6
INSTANCE_Y_SPACING = 140
INSTANCE_Z_SPACING = 120


def _instance_entity(y: int, z: int) -> str:
    # The source submodel is centered at Y=-144. Its regular fixture entity
    # offsets that by +144, so these translations form a front-facing grid
    # without changing the immutable source mesh.
    return (
        "{\n"
        '"classname" "func_wall"\n'
        '"model" "*1"\n'
        f'"origin" "0 {144 + y} {z}"\n'
        "}\n"
    )


INSTANCE_ENTITIES = tuple(
    _instance_entity(
        ((2 * column - (INSTANCE_COLUMNS - 1)) * INSTANCE_Y_SPACING) // 2,
        ((2 * row - (INSTANCE_ROWS - 1)) * INSTANCE_Z_SPACING) // 2,
    )
    for row in range(INSTANCE_ROWS)
    for column in range(INSTANCE_COLUMNS)
)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(extra_entities=INSTANCE_ENTITIES),
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
        "schema": "worr.renderer-parity.bmodel-instance-fixture.v1",
        "instances": len(INSTANCE_ENTITIES) + 1,
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
        f"{len(outputs)} bmodel-instancing fixture output(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
