#!/usr/bin/env python3
"""Inventory local Cybiko apps or stage a size-checked Vita installation set.

No downloads or firmware are included. A catalog records provenance and hashes,
not compatibility or release dates. Stage output must be a new directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect_app(path):
    size = path.stat().st_size
    if not 6 <= size <= 16 * 1024 * 1024:
        raise ValueError(f"{path.name}: invalid app size ({size})")
    data = path.read_bytes()
    if data[:2] != b"Cy":
        raise ValueError(f"{path.name}: missing Cy archive header")
    count, header_size = struct.unpack_from(">HH", data, 2)
    header_end = 6 + header_size
    table_end = 6 + 10 * count
    if count == 0 or table_end > header_end or header_end > size:
        raise ValueError(f"{path.name}: invalid archive table")
    for index in range(count):
        name, offset, length = struct.unpack_from(">HII", data, 6 + 10 * index)
        if not table_end <= name < header_end or data.find(b"\0", name, header_end) < 0:
            raise ValueError(f"{path.name}: invalid resource name")
        if offset < header_end or offset + length > size:
            raise ValueError(f"{path.name}: resource outside archive")
    blocks = 1 + max(0, size - 178 + 249) // 250
    return {
        "name": path.name,
        "bytes": size,
        "cfs_blocks": blocks,
        "sha256": hashlib.sha256(data).hexdigest(),
        "resources": count,
        "fits_empty_cfs": blocks <= 2000,
        "runtime_status": "untested",
    }, data


def inventory(source):
    entries = []
    for path in sorted(source.iterdir(), key=lambda p: p.name):
        if path.is_file() and path.suffix.lower() == ".app":
            try:
                entry, _ = inspect_app(path)
            except ValueError as error:
                entry = {"name": path.name, "error": str(error)}
            entries.append(entry)
    return entries


def stage(source, destination, names, reserve_blocks=128):
    if not names or len(names) > 64 or len(set(names)) != len(names):
        raise ValueError("select between 1 and 64 unique app filenames")
    if not 0 <= reserve_blocks < 2000:
        raise ValueError("reserve_blocks must be between 0 and 1999")
    files = []
    for name in sorted(names):
        if Path(name).name != name or len(name.encode("utf-8")) > 58:
            raise ValueError(f"invalid CFS filename: {name}")
        if not name.lower().endswith(".app"):
            raise ValueError(f"not an .app: {name}")
        entry, data = inspect_app(source / name)
        files.append((entry, data))
    used = sum(entry["cfs_blocks"] for entry, _ in files)
    if used > 2000 - reserve_blocks:
        raise ValueError(f"selection uses {used} CFS blocks; budget is {2000 - reserve_blocks}")
    # Validate the entire selection before creating any output. Never merge into
    # an existing installation, which could silently exceed the checked budget.
    destination.mkdir(parents=True, exist_ok=False)
    apps = destination / "apps"
    apps.mkdir()
    for entry, data in files:
        (apps / entry["name"]).write_bytes(data)
    manifest = {
        "source": str(source.resolve()),
        "description": "User-selected optional apps; not a verified launch bundle",
        "cfs_blocks_used": used,
        "cfs_blocks_reserved": reserve_blocks,
        "apps": [entry for entry, _ in files],
    }
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--stage", type=Path, help="new output directory containing apps/ and manifest.json")
    parser.add_argument("--app", action="append", default=[], help="exact filename; repeat to select more apps")
    parser.add_argument("--reserve-blocks", type=int, default=128)
    args = parser.parse_args()
    try:
        if args.stage:
            result = stage(args.source, args.stage, args.app, args.reserve_blocks)
        else:
            if args.app:
                raise ValueError("--app requires --stage")
            result = {"source": str(args.source.resolve()), "apps": inventory(args.source)}
        print(json.dumps(result, indent=2))
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
