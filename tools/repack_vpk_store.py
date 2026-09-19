#!/usr/bin/env python3
"""Rewrite a VPK/ZIP with stored entries.

vita-pack-vpk creates a valid ZIP archive, but some physical VitaShell install
failures around LiveArea promotion are avoided by shipping VPK entries stored
instead of deflated. This script preserves names, timestamps and attributes
while rewriting the archive in place.
"""
from pathlib import Path
import argparse
import os
import tempfile
import zipfile


def repack(path: Path) -> None:
    path = path.resolve()
    with zipfile.ZipFile(path, "r") as source:
        bad = source.testzip()
        if bad is not None:
            raise ValueError(f"{path}: corrupt member before repack: {bad}")
        fd, tmp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
        os.close(fd)
        tmp = Path(tmp_name)
        try:
            with zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_STORED) as out:
                for info in source.infolist():
                    data = source.read(info.filename)
                    stored = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                    stored.comment = info.comment
                    stored.create_system = info.create_system
                    stored.external_attr = info.external_attr
                    stored.extra = info.extra
                    out.writestr(stored, data, compress_type=zipfile.ZIP_STORED)
        except Exception:
            tmp.unlink(missing_ok=True)
            raise
    with zipfile.ZipFile(tmp, "r") as check:
        bad = check.testzip()
        if bad is not None:
            tmp.unlink(missing_ok=True)
            raise ValueError(f"{path}: corrupt member after repack: {bad}")
    tmp.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vpk", type=Path)
    args = parser.parse_args()
    repack(args.vpk)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
