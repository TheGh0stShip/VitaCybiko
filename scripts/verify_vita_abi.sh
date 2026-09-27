#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if (($# == 0)); then
    printf 'usage: %s VITA_ELF [...]\n' "$0" >&2
    exit 2
fi

readelf_tool="${VITA_READELF:-arm-vita-eabi-readelf}"
command -v "${readelf_tool}" >/dev/null || {
    printf 'missing Vita readelf tool: %s\n' "${readelf_tool}" >&2
    exit 1
}

for elf_path in "$@"; do
    header="$(${readelf_tool} -h "${elf_path}")"
    attributes="$(${readelf_tool} -A "${elf_path}")"

    grep -Eq 'Class:[[:space:]]+ELF32$' <<<"${header}"
    grep -Eq "Data:[[:space:]]+2's complement, little endian$" <<<"${header}"
    grep -Eq 'Machine:[[:space:]]+ARM$' <<<"${header}"
    grep -Eq 'Flags:.*hard-float ABI' <<<"${header}"
    grep -Eq 'Tag_CPU_arch:[[:space:]]+v7$' <<<"${attributes}"
    grep -Eq 'Tag_CPU_arch_profile:[[:space:]]+Application$' <<<"${attributes}"
    grep -Eq 'Tag_ABI_VFP_args:[[:space:]]+VFP registers$' <<<"${attributes}"

    printf 'verified Vita ABI: %s (ELF32 little-endian ARMv7-A hard-float)\n' \
        "${elf_path}"
done
