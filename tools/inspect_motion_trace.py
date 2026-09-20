#!/usr/bin/env python3
"""Decode opt-in VCMT0001 CPU LCD traces; these are not GPU screenshots."""
import argparse
import csv
import hashlib
from pathlib import Path
import struct


def inspect(source, destination):
    data = source.read_bytes()
    if len(data) < 12 or data[:8] != b"VCMT0001":
        raise ValueError("not a VCMT0001 trace")
    count, = struct.unpack_from("<I", data, 8)
    size = 32 + 16000 * 2 + 144000
    if not 1 <= count <= 64 or len(data) != 12 + size * count:
        raise ValueError("invalid count or truncated/trailing trace data")
    # Reject malformed metadata before creating any output artifacts.
    for i in range(count):
        fields = struct.unpack_from("<IIIiiIII", data, 12 + size * i)
        if fields[2] > 256 or fields[5] & ~7 or fields[6:] != (480, 300):
            raise ValueError(f"invalid metadata at record {i}")
    destination.mkdir(parents=True, exist_ok=False)
    rows = []
    for i in range(count):
        offset = 12 + size * i
        frame, updates, phase, dx, dy, flags, width, height = struct.unpack_from("<IIIiiIII", data, offset)
        offset += 32
        before, after = data[offset:offset+16000], data[offset+16000:offset+32000]
        output = data[offset+32000:offset+32000+144000]
        def scaled(image):
            return b"".join(bytes(v for v in image[y*160:(y+1)*160] for _ in range(3)) * 3
                            for y in range(100))
        equals_before, equals_after = output == scaled(before), output == scaled(after)
        expected_after = phase == 256 or bool(flags & 2)
        rejected_ok = not flags & 4 or (equals_after if expected_after else equals_before)
        rows.append(dict(record=i, guest_frame=frame, received_source_updates=updates,
                         phase=phase, dx=dx, dy=dy, flags=flags,
                         native_before=equals_before, native_after=equals_after,
                         rejected_flow_native_exact=rejected_ok,
                         output_sha256=hashlib.sha256(output).hexdigest()))
        for name, pixels, w, h in (("before", before, 160, 100), ("after", after, 160, 100),
                                   ("output", output, 480, 300)):
            (destination / f"{i:03d}-{name}.pgm").write_bytes(f"P5\n{w} {h}\n255\n".encode() + pixels)
    if rows:
        with (destination / "records.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=rows[0])
            writer.writeheader()
            writer.writerows(rows)
    generated = sum(not r["native_before"] and not r["native_after"] for r in rows)
    failures = sum(not r["rejected_flow_native_exact"] for r in rows)
    print(f"records={count} non_native_outputs={generated} rejected_flow_mismatches={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()
    raise SystemExit(inspect(args.trace, args.output_directory))
