#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
hook_dir="${repo_root}/.git/hooks"
mkdir -p "${hook_dir}"
cp "${repo_root}/scripts/pre-push" "${hook_dir}/pre-push"
chmod +x "${hook_dir}/pre-push"
printf 'Installed VitaCybiko pre-push release gate at %s\n' "${hook_dir}/pre-push"
