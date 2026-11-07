#!/usr/bin/env bash
# Prepare an update branch; never overwrite the integrated port or push anything.
set -euo pipefail
root=$(git rev-parse --show-toplevel)
cd "$root"
if [[ -n $(git status --porcelain) ]]; then
    echo 'Working tree must be clean.' >&2; exit 1
fi
port=dipper/android17-6.18
base=base/android17-6.18
upstream=upstream/android17-6.18
branch=${1:-work/update-$(date -u +%Y%m%d-%H%M%S)}
git check-ref-format --branch "$branch" >/dev/null
case "$branch" in work/*) ;; *) echo 'Use a work/* branch name.' >&2; exit 1;; esac
if git show-ref --verify --quiet "refs/heads/$branch"; then
    echo 'Update branch already exists.' >&2; exit 1
fi
git rev-parse --verify "$port^{commit}" >/dev/null
git rev-parse --verify "$base^{commit}" >/dev/null
git fetch --no-tags upstream
if ! git merge-base --is-ancestor "$base" "$upstream"; then
    if [[ $(git rev-parse --is-shallow-repository) == true ]]; then
        git fetch --no-tags --deepen=128 upstream
    fi
    if ! git merge-base --is-ancestor "$base" "$upstream"; then
        echo 'Cannot verify upstream ancestry. Inspect history; deepen upstream if needed.' >&2
        exit 1
    fi
fi
if git merge-base --is-ancestor "$upstream" "$port"; then
    echo 'Port already contains the fetched Google revision.'; exit 0
fi
git switch -c "$branch" "$port"
git merge --no-ff --no-edit "$upstream"
echo 'Update merged only into the work branch. Build/test before integrating.'
echo 'If conflicts occurred, resolve them here or run git merge --abort.'
