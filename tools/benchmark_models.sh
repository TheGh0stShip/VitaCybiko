#!/usr/bin/env bash
set -euo pipefail

# Run identical 600-frame smoke workloads for all profiles. Firmware paths are
# supplied by the operator; no ROMs or saves are stored in the repository.
if [[ $# -ne 8 ]]; then
  echo "usage: $0 smoke classic_v1_boot classic_v1_flash classic_v2_boot classic_v2_flash classic_v2_dataflash xtreme_boot xtreme_flash" >&2
  exit 2
fi
smoke=$1; v1boot=$2; v1flash=$3; v2boot=$4; v2flash=$5; v2data=$6; xtboot=$7; xtflash=$8
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
run() { /usr/bin/time -f '%e' -o "$tmp/time" "$@" >"$tmp/out"; printf '%s\t' "$1"; cat "$tmp/time"; rg 'firmware-smoke: PASS|model=' "$tmp/out"; }
run "$smoke" --classic-v1 "$v1boot" - "$v1flash" 600
run "$smoke" --classic-v2 "$v2boot" "$v2flash" "$v2data" 600
run "$smoke" "$xtboot" "$xtflash" 600
