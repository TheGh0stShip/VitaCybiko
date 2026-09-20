#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "${repo_root}"

remote="${1:-origin}"
branch="${2:-$(git branch --show-current)}"

if [[ -z "${branch}" ]]; then
    printf 'Unable to determine the current branch. Pass it explicitly: %s <remote> <branch>\n' "$0" >&2
    exit 2
fi

if ! command -v gh >/dev/null 2>&1; then
    printf 'GitHub CLI (gh) is required so the pushed commit can be verified remotely.\n' >&2
    exit 2
fi

sha="$(git rev-parse HEAD)"

printf 'Running local release gate for %s before pushing...\n' "${sha}"
"${repo_root}/scripts/release_gate.sh"

printf '\nPushing %s to %s/%s...\n' "${sha}" "${remote}" "${branch}"
git push "${remote}" "HEAD:${branch}"

printf '\nWaiting for GitHub Actions run for %s on %s...\n' "${sha}" "${branch}"

run_id=""
for _ in {1..24}; do
    run_id="$(
        gh run list \
            --branch "${branch}" \
            --commit "${sha}" \
            --limit 1 \
            --json databaseId \
            --jq '.[0].databaseId // empty'
    )"
    if [[ -n "${run_id}" ]]; then
        break
    fi
    sleep 5
done

if [[ -z "${run_id}" ]]; then
    printf 'No GitHub Actions run appeared for %s on %s.\n' "${sha}" "${branch}" >&2
    exit 1
fi

gh run watch "${run_id}" --exit-status

conclusion="$(
    gh run view "${run_id}" \
        --json conclusion \
        --jq '.conclusion // empty'
)"

if [[ "${conclusion}" != "success" ]]; then
    printf 'GitHub Actions did not pass for %s. Run id: %s, conclusion: %s\n' "${sha}" "${run_id}" "${conclusion:-unknown}" >&2
    exit 1
fi

url="$(
    gh run view "${run_id}" \
        --json url \
        --jq '.url'
)"

printf '\nVerified: %s passed locally and GitHub Actions succeeded.\n%s\n' "${sha}" "${url}"
