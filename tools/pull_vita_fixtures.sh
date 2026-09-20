#!/usr/bin/env bash
set -euo pipefail

# Pull operator-owned VitaCybiko firmware fixtures from a VitaShell FTP server
# into the ignored local scratch tree. This is for local verification only:
# no firmware, apps, saves, or user data should be committed to the repository.

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 ftp_base_url [output_dir]" >&2
  echo "example: $0 ftp://10.0.0.202:1337/ux0:/data/VitaCybiko" >&2
  exit 2
fi

base=${1%/}
out=${2:-vita_runtime_pull/current}

download() {
  local remote=$1
  local local_path=$2
  mkdir -p "$(dirname "$local_path")"
  curl --fail --connect-timeout 8 --max-time 60 -sS \
    -o "$local_path" \
    "$base/$remote"
}

download classic-v1/roms/boot.bin      "$out/classic-v1/roms/boot.bin"
download classic-v1/roms/dataflash.bin "$out/classic-v1/roms/dataflash.bin"
download classic-v2/roms/boot.bin      "$out/classic-v2/roms/boot.bin"
download classic-v2/roms/flash.bin     "$out/classic-v2/roms/flash.bin"
download classic-v2/roms/dataflash.bin "$out/classic-v2/roms/dataflash.bin"
download xtreme/roms/boot.bin          "$out/xtreme/roms/boot.bin"
download xtreme/roms/flash.bin         "$out/xtreme/roms/flash.bin"

python3 - "$out" <<'PY'
import hashlib
import pathlib
import sys
import zlib

root = pathlib.Path(sys.argv[1])
for path in sorted(root.rglob("*.bin")):
    data = path.read_bytes()
    print(f"{zlib.crc32(data) & 0xffffffff:08x} "
          f"{hashlib.sha1(data).hexdigest()} "
          f"{len(data):8d} {path}")
PY
