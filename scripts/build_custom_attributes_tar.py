#!/usr/bin/env python3
"""
Build a custom_attributes.tar from an existing valhalla_tiles.tar.

Binary format per .cab file (matches CustomAttributesTileHeader in
valhalla/baldr/custom_attributes_tile.h):
  [0:4]              uint32_t  edge_count
  [4:4+edge_count*4] float32[edge_count]  one value per directed edge

The resulting tar contains:
  <level>/<path>.cab  – one file per tile, same path structure as .gph
"""

import argparse
import io
import random
import struct
import sys
import tarfile


# Offset of the uint64 that holds nodecount_ / directededgecount_ inside
# GraphTileHeader (confirmed by static_assert in graphtileheader.h):
#   uint64  graphid_ + quality bits      @ 0   (8 bytes)
#   float[2] base_ll_                    @ 8   (8 bytes)
#   char[16] version_                    @ 16  (16 bytes)
#   uint64  dataset_id_                  @ 32  (8 bytes)
#   uint64  nodecount_:21 | directededgecount_:21 | ...  @ 40
_HEADER_COUNTS_OFFSET = 40
_NODECOUNT_BITS = 21
_DIRECTEDGE_BITS = 21


def _directed_edge_count(tile_bytes: bytes) -> int:
    """Extract directededgecount_ from raw GraphTileHeader bytes."""
    if len(tile_bytes) < _HEADER_COUNTS_OFFSET + 8:
        raise ValueError(
            f"Tile too small to contain GraphTileHeader counts word ({len(tile_bytes)} bytes)"
        )
    (counts_word,) = struct.unpack_from("<Q", tile_bytes, _HEADER_COUNTS_OFFSET)
    return (counts_word >> _NODECOUNT_BITS) & ((1 << _DIRECTEDGE_BITS) - 1)


def _tile_path_to_cab(path: str) -> str:
    """Replace .gph/.gph.gz suffix with .cab, normalising separators to '/'."""
    path = path.replace("\\", "/")
    for suffix in (".gph.gz", ".gph"):
        if path.endswith(suffix):
            return path[: -len(suffix)] + ".cab"
    raise ValueError(f"Unexpected tile suffix in path: {path!r}")


def _build_cab(edge_count: int, randomize: bool, default_value: float,
               random_max: float = 1.0) -> bytes:
    header = struct.pack("<I", edge_count)
    if randomize:
        values = [random.uniform(0.0, random_max) for _ in range(edge_count)]
    else:
        values = [default_value] * edge_count
    data = struct.pack(f"<{edge_count}f", *values)
    return header + data


def _add_bytes_to_tar(tf: tarfile.TarFile, name: str, data: bytes) -> None:
    info = tarfile.TarInfo(name=name)
    info.size = len(data)
    tf.addfile(info, io.BytesIO(data))


def build(valhalla_tar: str, output_tar: str, randomize: bool, default_value: float,
          random_max: float = 1.0) -> None:
    with tarfile.open(valhalla_tar, "r:*") as src_tf, tarfile.open(output_tar, "w:") as dst_tf:

        tiles_processed = 0
        tiles_skipped = 0

        for member in src_tf.getmembers():
            name = member.name.replace("\\", "/")
            if not (name.endswith(".gph") or name.endswith(".gph.gz")):
                continue

            cab_path = _tile_path_to_cab(name)

            f = src_tf.extractfile(member)
            if f is None:
                tiles_skipped += 1
                continue
            tile_bytes = f.read()

            try:
                edge_count = _directed_edge_count(tile_bytes)
            except ValueError as exc:
                print(f"  WARNING: skipping {name}: {exc}", file=sys.stderr)
                tiles_skipped += 1
                continue

            cab_bytes = _build_cab(edge_count, randomize, default_value, random_max)
            _add_bytes_to_tar(dst_tf, cab_path, cab_bytes)
            tiles_processed += 1

            if tiles_processed % 500 == 0:
                print(f"  {tiles_processed} tiles written ...", file=sys.stderr)

    print(
        f"Done: {tiles_processed} tiles written, {tiles_skipped} skipped -> {output_tar}",
        file=sys.stderr,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--tiles-tar",
        default="/data/valhalla_tiles.tar",
        help="Path to the source valhalla_tiles.tar (default: /data/valhalla_tiles.tar)",
    )
    parser.add_argument(
        "--output",
        default="/data/custom_attributes.tar",
        help="Path for the output custom_attributes.tar (default: /data/custom_attributes.tar)",
    )
    parser.add_argument(
        "--random",
        action="store_true",
        help="Assign random float values in [0.0, --random-max) to each edge instead of the default value",
    )
    parser.add_argument(
        "--random-max",
        type=float,
        default=1.0,
        help="Upper bound (exclusive) for random values when --random is set (default: 1.0)",
    )
    parser.add_argument(
        "--default-value",
        type=float,
        default=0.0,
        help="Default value for all edges when --random is not set (default: 0.0)",
    )
    args = parser.parse_args()

    print(f"Reading tiles from : {args.tiles_tar}", file=sys.stderr)
    print(f"Writing output to  : {args.output}", file=sys.stderr)
    if args.random:
        print(f"Edge values        : random [0.0, {args.random_max})", file=sys.stderr)
    else:
        print(f"Edge values        : {args.default_value}", file=sys.stderr)

    build(args.tiles_tar, args.output, args.random, args.default_value, args.random_max)


if __name__ == "__main__":
    main()
