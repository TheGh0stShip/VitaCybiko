#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 path/to/VitaCybiko.vpk" >&2
  exit 2
fi

vpk=$1
host=${VITA_FTP_HOST:-10.0.0.202}
port=${VITA_FTP_PORT:-1337}
remote_dir=${VITA_FTP_DIR:-/ux0:data/VitaCybiko}
remote_name=${VITA_FTP_NAME:-VitaCybiko-latest.vpk}
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

[[ -f "$vpk" ]] || { echo "missing VPK: $vpk" >&2; exit 1; }
base="ftp://${host}:${port}"

curl --fail --silent --show-error --connect-timeout 5 --max-time 120 \
  --ftp-create-dirs -T "$vpk" "$base$remote_dir/$remote_name"
curl --fail --silent --show-error --connect-timeout 5 --max-time 120 \
  "$base$remote_dir/$remote_name" -o "$tmp"

local_hash=$(sha256sum "$vpk" | awk '{print $1}')
remote_hash=$(sha256sum "$tmp" | awk '{print $1}')
if [[ "$local_hash" != "$remote_hash" ]]; then
  echo "VPK read-back hash mismatch" >&2
  echo "local : $local_hash" >&2
  echo "remote: $remote_hash" >&2
  exit 1
fi
printf 'deployed %s\nsha256 %s\nremote %s\n' "$remote_name" "$local_hash" "$base$remote_dir/$remote_name"
