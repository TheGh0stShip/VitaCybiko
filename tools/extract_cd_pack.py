#!/usr/bin/env python3
"""Recover the Xtreme CD's CAP v1 files without running its Windows installer.

Format derived from CyCAP.dll supplied with the same CD. Only file entries are
exported; installer scripts are never executed. Library destinations follow the
AddPack install script: email_dl.dl at root, sound.dl under Libraries/.
"""
import argparse
import bz2
import hashlib
import json
from pathlib import Path
import struct
import zipfile

from prepare_apps import inspect_app

CD_MEMBER = "cybiko/xtreme cd - most files work with classic too/apps/AddPack.cap"
MAX_SIZE = 16 * 1024 * 1024


def decode_words(data):
    key = 0x8228
    plain = bytearray()
    for (value,) in struct.iter_unpack("<H", data):
        key ^= value
        plain.extend(struct.pack("<H", key))
    return plain.decode("utf-16-le")


def decode_bytes(data):
    key = 0x82
    plain = bytearray()
    for value in data:
        key ^= value
        plain.append(key)
    return bytes(plain)


def decode_pack(data):
    if len(data) < 27 or len(data) > MAX_SIZE or data[:7] != b"CAP\x01\0\0\0":
        raise ValueError("unsupported CAP header")
    toc_size = struct.unpack_from("<I", data, 7)[0] ^ 0xFF89AA15
    payload_size = struct.unpack_from("<I", data, 11)[0] ^ 0xFF4CC827
    stream = 19 + toc_size
    if not 8 <= toc_size <= len(data) - 19 or payload_size > MAX_SIZE:
        raise ValueError("CAP lengths out of bounds")
    decoder = bz2.BZ2Decompressor()
    payload = decoder.decompress(data[stream:], max_length=payload_size + 1)
    if len(payload) != payload_size or not decoder.eof or decoder.unused_data:
        raise ValueError("CAP payload size/stream mismatch")
    count = struct.unpack_from("<I", data, 23)[0]
    if not 1 <= count <= 256:
        raise ValueError("invalid CAP entry count")
    position = 27
    expected_offset = 0
    files = {}
    for _ in range(count):
        if position + 16 > stream:
            raise ValueError("truncated CAP table")
        kind, record_size, length, chars = struct.unpack_from("<IIII", data, position)
        record_size ^= 0x55555555
        length ^= 0x05F5C1C1
        chars ^= 0x23030323
        position += 16
        if chars > 255 or position + chars * 2 + 4 > stream:
            raise ValueError("invalid CAP filename length")
        name = decode_words(data[position:position + chars * 2])
        position += chars * 2
        offset = struct.unpack_from("<I", data, position)[0] ^ 0x015F70A8
        position += 4
        if offset != expected_offset or offset + record_size > len(payload):
            raise ValueError("CAP records overlap or exceed payload")
        expected_offset += record_size
        if kind == 1:  # Metadata; no executable content exported.
            continue
        if kind == 3:  # Installation script: validate its bounds, never execute.
            if record_size != 12 + 2 * length:
                raise ValueError("invalid CAP script length")
            continue
        if kind != 2 or record_size != 12 + length:
            raise ValueError("unsupported CAP entry")
        if not name or Path(name).name != name or any(c in name for c in "\\\0:"):
            raise ValueError("unsafe CAP filename")
        if name in files:
            raise ValueError("duplicate CAP filename")
        files[name] = decode_bytes(payload[offset + 12:offset + record_size])
    if position != stream or expected_offset != len(payload):
        raise ValueError("unaccounted CAP data")
    return files


def extract(archive, destination):
    with zipfile.ZipFile(archive) as source:
        if source.getinfo(CD_MEMBER).file_size > MAX_SIZE:
            raise ValueError("CD pack is too large")
        cap = source.read(CD_MEMBER)
    files = decode_pack(cap)
    # These are the only library routing rules established by this CD script.
    if set(name for name in files if not name.endswith(".app")) != {"email_dl.dl", "sound.dl"}:
        raise ValueError("unexpected CD dependencies; review its installation rules")
    entries = []
    used = 0
    for name, data in files.items():
        target = f"Libraries/{name}" if name == "sound.dl" else name
        if len(target.encode()) > 58:
            raise ValueError("filename exceeds CFS limit")
        blocks = 1 + max(0, len(data) - 178 + 249) // 250
        used += blocks
        entries.append({"name": target, "bytes": len(data), "cfs_blocks": blocks,
                        "sha256": hashlib.sha256(data).hexdigest(), "runtime_status": "untested"})
    if used > 1872 or len(files) > 64:
        raise ValueError("CD selection exceeds reserved CFS budget")
    # Validate every Cy archive before writing the install set.
    import tempfile
    with tempfile.TemporaryDirectory(prefix="vitacybiko-cap-validate-") as temporary:
        for name, data in files.items():
            path = Path(temporary) / name
            path.write_bytes(data)
            inspect_app(path)
    destination.mkdir(parents=True, exist_ok=False)
    for entry in entries:
        path = destination / "apps" / entry["name"]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(files[Path(entry["name"]).name])
    manifest = {"source_archive": str(archive.resolve()), "source_member": CD_MEMBER,
                "source_sha256": hashlib.sha256(cap).hexdigest(),
                "description": "Exact additional pack from supplied Xtreme CD; launch date unverified",
                "cfs_blocks_used": used, "files": entries}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        result = extract(args.archive, args.destination)
    except (ValueError, OSError, EOFError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
