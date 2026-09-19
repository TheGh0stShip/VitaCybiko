# Xtreme CD CAP recovery

Source: the user's archived Xtreme CD, `apps/AddPack.cap`, SHA-256
`e8a470a323e26c50d312ac437a3920ab3cde85ce411ee488ea8adca6f8d2661d`.
The decoder is an independent Python implementation based on the format routines
in the same CD's `EZLoader_Setup.exe` → `Data.Cab` → `F2009_CyCAP.dll`.
The Windows software was inspected, not executed.

CAP v1 has a 19-byte header. Bytes 0–6 are `CAP` and little-endian version 1.
The little-endian words at offsets 7 and 11 are the table byte size XOR
`0xff89aa15` and decompressed payload byte size XOR `0xff4cc827`.
For this pack those sizes are 832 and 406,679; a BZip2 stream begins at offset
851. The table begins with an opaque check word followed by the entry count.

Each of the 17 table entries contains four little-endian 32-bit fields: type,
record size XOR `0x55555555`, content length XOR `0x05f5c1c1`, and filename
UTF-16 code-unit count XOR `0x23030323`. The filename follows, then the payload
offset XOR `0x015f70a8`. Filename code units are decoded by starting with
`0x8228`, XORing each stored unit into the previous decoded unit.

Types are metadata (1), file (2), and installation script (3). There is one
metadata record, one script, and fifteen file records. File records have a
12-byte prefix followed by content. Decode each content byte by starting with
`0x82` and XORing each stored byte into the previous decoded byte. Scripts use
the filename-style UTF-16 transform. The script is never run by the extractor.

Useful original routines: header fields at `0x10005772`, table parser at
`0x100058d4`, UTF-16 decoder at `0x100027cf`, byte decoder at `0x100027ec`.

The extraction checks header bounds, counts, record contiguity, stream end,
BZip2 integrity, and the structure/resource bounds of every recovered Cy
archive. It accounts for all table and payload bytes. The opaque table/record
check words are not yet interpreted. Tests pin the original source hash,
compare all recovered hashes, and reject truncated/corrupted compressed data.
This is verified for this CD, not a promise to support every CAP variant.

The original script installs `.app` files and `email_dl.dl` at the selected
disk root, and `sound.dl` in `Libraries/`. The port preserves these names in CFS.
Whether CyOS resolves/displays that directory correctly still needs guest
testing. The script's deletion of an older `/default/email.dl` and its version
downgrade prompts are not replayed; use a backed-up, suitably empty save when
installing this set.
