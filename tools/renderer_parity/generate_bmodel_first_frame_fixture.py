#!/usr/bin/env python3
"""Generate the deterministic FR-01-T06/T07 renderable IBSP and textures.

The world face carries a dark authored lightmap while the capture config uses
``r_fullbright 1``. The existing first-frame bmodel gate therefore also proves
that both renderers bypass static world lighting under global fullbright.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


IBSP_IDENT = int.from_bytes(b"IBSP", "little")
BSP_VERSION = 38
HEADER_LUMPS = 19

LUMP_ENTITIES = 0
LUMP_PLANES = 1
LUMP_VERTICES = 2
LUMP_NODES = 4
LUMP_TEXINFO = 5
LUMP_FACES = 6
LUMP_LIGHTING = 7
LUMP_LEAFS = 8
LUMP_LEAFFACES = 9
LUMP_EDGES = 11
LUMP_SURFEDGES = 12
LUMP_MODELS = 13
LUMP_AREAS = 17

CONTENTS_SOLID = 1

MAP_NAME = "worr_fr01_bmodel_first_frame.bsp"
BACKGROUND_TEXTURE = "parity/fr01_bm_bg"
BMODEL_TEXTURE = "parity/fr01_bm_box"

BACKGROUND_RGB = (24, 40, 72)
BMODEL_RGB = (48, 220, 96)
BACKGROUND_LIGHTMAP_RGB = (32, 32, 32)
BACKGROUND_LIGHTMAP_WIDTH = 81
BACKGROUND_LIGHTMAP_HEIGHT = 61

WORLD_VERTICES = (
    (512.0, -640.0, -480.0),
    (512.0, -640.0, 480.0),
    (512.0, 640.0, 480.0),
    (512.0, 640.0, -480.0),
)

# The inline model is compiled left of center. Its entity origin translates it
# right by 144 units on the first server state. If Vulkan accidentally bakes
# the submodel into the static world mesh, the authored copy remains visible.
BMODEL_MINS = (224.0, -184.0, -64.0)
BMODEL_MAXS = (288.0, -104.0, 64.0)
BMODEL_ENTITY_ORIGIN = (0.0, 144.0, 0.0)


def _plane(normal: tuple[float, float, float], distance: float) -> bytes:
    return struct.pack("<4fi", *normal, distance, 0)


def _texinfo(name: str) -> bytes:
    encoded = name.encode("ascii")
    if len(encoded) >= 32:
        raise ValueError(f"texture name is too long: {name}")
    axes = (0.0, 1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0)
    return struct.pack(
        "<8fii32si", *axes, 0, 0, encoded.ljust(32, b"\0"), -1
    )


def _face(
    planenum: int,
    firstedge: int,
    texinfo: int,
    styles: tuple[int, int, int, int] = (255, 255, 255, 255),
    light_offset: int = -1,
) -> bytes:
    return struct.pack(
        "<Hhihh4Bi", planenum, 0, firstedge, 4, texinfo, *styles, light_offset
    )


def _leaf(
    contents: int,
    mins: tuple[int, int, int],
    maxs: tuple[int, int, int],
    first_face: int,
    face_count: int,
) -> bytes:
    return struct.pack(
        "<ihh3h3h4H",
        contents,
        -1,
        0,
        *mins,
        *maxs,
        first_face,
        face_count,
        0,
        0,
    )


def _model(
    mins: tuple[float, float, float],
    maxs: tuple[float, float, float],
    headnode: int,
    firstface: int,
    numfaces: int,
) -> bytes:
    return struct.pack(
        "<9f3i", *mins, *maxs, 0.0, 0.0, 0.0, headnode, firstface, numfaces
    )


def _tga(rgb: tuple[int, int, int], size: int = 16) -> bytes:
    header = struct.pack(
        "<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, size, size, 24, 0x20
    )
    pixel = bytes((rgb[2], rgb[1], rgb[0]))
    return header + pixel * (size * size)


def build_bsp(
    worldspawn_properties: tuple[str, ...] = (),
    extra_entities: tuple[str, ...] = (),
) -> bytes:
    extra_worldspawn_properties = "".join(worldspawn_properties)
    extra_entity_text = "".join(extra_entities)
    entities = (
        '{\n"classname" "worldspawn"\n'
        '"message" "WORR FR-01-T06 bmodel first-frame parity"\n'
        '"gravity" "0"\n'
        f'{extra_worldspawn_properties}'
        '}\n'
        '{\n"classname" "info_player_start"\n'
        '"origin" "0 0 -22"\n"angle" "0"\n}\n'
        '{\n"classname" "func_wall"\n"model" "*1"\n'
        f'"origin" "{BMODEL_ENTITY_ORIGIN[0]:g} '
        f'{BMODEL_ENTITY_ORIGIN[1]:g} {BMODEL_ENTITY_ORIGIN[2]:g}"\n}}\n'
        f'{extra_entity_text}\0'
    ).encode("ascii")

    xmin, ymin, zmin = BMODEL_MINS
    xmax, ymax, zmax = BMODEL_MAXS
    vertices = (*WORLD_VERTICES,
        (xmin, ymin, zmin), (xmin, ymin, zmax),
        (xmin, ymax, zmax), (xmin, ymax, zmin),
        (xmax, ymin, zmin), (xmax, ymin, zmax),
        (xmax, ymax, zmax), (xmax, ymax, zmin),
    )

    face_vertices = (
        (0, 1, 2, 3),       # world background, normal -X
        (4, 5, 6, 7),       # box -X
        (8, 11, 10, 9),     # box +X
        (4, 8, 9, 5),       # box -Y
        (7, 6, 10, 11),     # box +Y
        (4, 7, 11, 8),      # box -Z
        (5, 9, 10, 6),      # box +Z
    )

    edges = [(0, 0)]
    surfedges: list[int] = []
    face_firstedges: list[int] = []
    for winding in face_vertices:
        winding = tuple(reversed(winding))
        face_firstedges.append(len(surfedges))
        for index, vertex in enumerate(winding):
            next_vertex = winding[(index + 1) % len(winding)]
            edges.append((vertex, next_vertex))
            surfedges.append(len(edges) - 1)

    planes = (
        _plane((1.0, 0.0, 0.0), -1024.0),
        _plane((-1.0, 0.0, 0.0), -512.0),
        _plane((-1.0, 0.0, 0.0), -xmin),
        _plane((1.0, 0.0, 0.0), xmax),
        _plane((0.0, -1.0, 0.0), -ymin),
        _plane((0.0, 1.0, 0.0), ymax),
        _plane((0.0, 0.0, -1.0), -zmin),
        _plane((0.0, 0.0, 1.0), zmax),
    )

    lumps: list[bytes] = [b"" for _ in range(HEADER_LUMPS)]
    lumps[LUMP_ENTITIES] = entities
    lumps[LUMP_PLANES] = b"".join(planes)
    lumps[LUMP_VERTICES] = b"".join(struct.pack("<3f", *v) for v in vertices)
    lumps[LUMP_NODES] = struct.pack(
        "<iii3h3h2H",
        0,
        -2,
        -1,
        -1024,
        -1024,
        -1024,
        1024,
        1024,
        1024,
        0,
        1,
    )
    lumps[LUMP_TEXINFO] = _texinfo(BACKGROUND_TEXTURE) + _texinfo(BMODEL_TEXTURE)
    lumps[LUMP_FACES] = b"".join(
        _face(
            1 if index == 0 else index + 1,
            firstedge,
            0 if index == 0 else 1,
            (0, 255, 255, 255) if index == 0 else (255, 255, 255, 255),
            0 if index == 0 else -1,
        )
        for index, firstedge in enumerate(face_firstedges)
    )
    lightmap_pixel = bytes(BACKGROUND_LIGHTMAP_RGB)
    lumps[LUMP_LIGHTING] = lightmap_pixel * (
        BACKGROUND_LIGHTMAP_WIDTH * BACKGROUND_LIGHTMAP_HEIGHT
    )
    lumps[LUMP_LEAFS] = b"".join(
        (
            _leaf(CONTENTS_SOLID, (-1024, -1024, -1024), (-1024, 1024, 1024), 0, 0),
            _leaf(0, (-1024, -1024, -1024), (1024, 1024, 1024), 0, 1),
        )
    )
    lumps[LUMP_LEAFFACES] = struct.pack("<H", 0)
    lumps[LUMP_EDGES] = b"".join(struct.pack("<HH", *edge) for edge in edges)
    lumps[LUMP_SURFEDGES] = b"".join(struct.pack("<i", edge) for edge in surfedges)
    lumps[LUMP_MODELS] = b"".join(
        (
            _model((-1024.0, -1024.0, -1024.0), (1024.0, 1024.0, 1024.0), 0, 0, 1),
            _model(BMODEL_MINS, BMODEL_MAXS, -2, 1, 6),
        )
    )
    lumps[LUMP_AREAS] = struct.pack("<ii", 0, 0)

    header_size = 8 + HEADER_LUMPS * 8
    cursor = header_size
    body = bytearray()
    descriptors: list[tuple[int, int]] = []
    for lump in lumps:
        padding = (-cursor) & 3
        if padding:
            body.extend(b"\0" * padding)
            cursor += padding
        descriptors.append((cursor, len(lump)))
        body.extend(lump)
        cursor += len(lump)

    header = bytearray(struct.pack("<II", IBSP_IDENT, BSP_VERSION))
    for offset, length in descriptors:
        header.extend(struct.pack("<II", offset, length))
    return bytes(header + body)


def generated_outputs(asset_root: Path) -> dict[Path, bytes]:
    return {
        asset_root / "maps" / MAP_NAME: build_bsp(),
        asset_root / "textures" / f"{BACKGROUND_TEXTURE}.tga": _tga(BACKGROUND_RGB),
        asset_root / "textures" / f"{BMODEL_TEXTURE}.tga": _tga(BMODEL_RGB),
    }


def output_report(outputs: dict[Path, bytes]) -> dict[str, object]:
    return {
        "schema": "worr.renderer-parity.bmodel-first-frame-fixture.v1",
        "outputs": [
            {
                "path": str(path),
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            for path, data in outputs.items()
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-root", type=Path, default=Path("assets"))
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    outputs = generated_outputs(args.asset_root)
    if args.validate:
        mismatches = [
            str(path)
            for path, expected in outputs.items()
            if not path.is_file() or path.read_bytes() != expected
        ]
        if mismatches:
            raise SystemExit("generated fixture mismatch: " + ", ".join(mismatches))
    else:
        for path, data in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    report = output_report(outputs)
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        action = "validated" if args.validate else "generated"
        print(f"{action} {len(outputs)} FR-01-T06 fixture outputs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
