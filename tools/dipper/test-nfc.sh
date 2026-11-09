#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:?Pass an absolute external test output directory}
[[ "$out" == /* ]] || exit 1
out=$(realpath -m "$out")
case "$out/" in "$root/"*) echo 'Keep test binaries outside the source tree' >&2; exit 1;; esac
mkdir -p "$out"
"${HOSTCC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/tools/dipper/tests/test_nfc.c" -o "$out/test-nfc"
"$out/test-nfc"
