#!/usr/bin/env python3
"""Identify known, legally supplied firmware and copy it without overwriting.

SOURCE is a directory of extracted files; DESTINATION is ux0/data/VitaCybiko.
No downloads, archive extraction, application imports or save modifications.
"""
import argparse
import hashlib
from pathlib import Path
import shutil


IMAGES = {
    (32768, "6fc08de6b2c67d884ec78f748e4a4bad27ee8045"): "classic-v1/roms/boot.bin",
    (540672, "19be4fed8d95112568adf93219afe9406d7baecf"): "classic-v1/roms/dataflash.bin",
    (32768, "135eaf9e3905e69582aabd9b06bc4de0a66780d5"): "classic-v2/roms/boot.bin",
    (262144, "eee329e8541e1e36c22acb1317378ce23ccd1e12"): "classic-v2/roms/flash.bin",
    (540672, "e414d6d2f876c7c811946bcdfcb6212999412381"): "classic-v2/roms/dataflash.bin",
    (32768, "28868d6174eb198a6cec6c3c70b6e494517229b9"): "xtreme/roms/boot.bin",
    (524288, "537a88e238746b3944b0cdfd4b0a9396460b2977"): "xtreme/roms/flash.bin",
}


def plan(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if not source.is_dir():
        raise ValueError("source must be an extracted directory")
    sizes = {size for size, _ in IMAGES}
    found = {}
    for path in sorted(source.rglob("*")):
        if path.is_symlink() or not path.is_file() or path.stat().st_size not in sizes:
            continue
        data = path.read_bytes()
        relative = IMAGES.get((len(data), hashlib.sha1(data).hexdigest()))
        if relative:
            target = destination / relative
            if target.is_symlink() or any(p.is_symlink() for p in target.parents):
                raise ValueError(f"refusing symlink destination: {target}")
            if target.exists() and target.read_bytes() != data:
                raise ValueError(f"existing file differs; preserve it: {target}")
            found.setdefault(relative, (path, target))
    return found


def stage(found):
    for source, target in found.values():
        if target.exists():
            if source.read_bytes() != target.read_bytes():
                raise ValueError(f"destination changed: {target}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation prevents overwriting any concurrent writer.
        with source.open("rb") as src, target.open("xb") as dst:
            shutil.copyfileobj(src, dst)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--copy", action="store_true", help="copy after validation; default is dry run")
    args = parser.parse_args()
    try:
        found = plan(args.source, args.destination)
        for name in sorted(IMAGES.values()):
            print(f"{'FOUND' if name in found else 'MISSING'} {name}")
        if args.copy:
            stage(found)
        print("Existing saves were not modified.")
        return 0 if found else 1
    except (OSError, ValueError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
