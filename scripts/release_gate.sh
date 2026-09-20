#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

jobs="${VITACYBIKO_JOBS:-2}"

run_step() {
    local title="$1"
    shift
    printf '\n==> %s\n' "${title}"
    "$@"
}

run_step "Configure release host build" \
    cmake -S . -B build-ci-release-host -DCMAKE_BUILD_TYPE=Release

run_step "Build release host targets" \
    cmake --build build-ci-release-host -j"${jobs}" --target test_h8s_cpu cybiko-smoke

run_step "Run focused H8S CPU tests" \
    ./build-ci-release-host/test_h8s_cpu

run_step "Run full release host ctest suite" \
    ctest --test-dir build-ci-release-host --output-on-failure -j"${jobs}"

if [[ ! -d build-asan-core ]]; then
    run_step "Configure ASan/UBSan core build" \
        cmake -S . -B build-asan-core \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
fi

run_step "Build ASan/UBSan core suite" \
    cmake --build build-asan-core -j"${jobs}"

run_step "Run ASan/UBSan/leak core ctest suite" \
    env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    ctest --test-dir build-asan-core --output-on-failure -j"${jobs}"

run_step "Run Python tests" \
    python3 -m unittest discover -s tests -p 'test_*.py'

if [[ -f vitasdk.cmake || -n "${VITASDK:-}" || -d build-vita ]]; then
    if [[ ! -d build-vita ]]; then
        run_step "Configure Vita build" \
            cmake -S . -B build-vita -DCMAKE_BUILD_TYPE=Release
    fi
    run_step "Build Vita package" \
        cmake --build build-vita -j"${jobs}"
else
    printf '\n==> Skip Vita package build: no build-vita directory or VitaSDK environment detected\n'
fi

printf '\nRelease gate passed. Safe to commit/push, then watch GitHub Actions to completion before distributing a VPK.\n'
