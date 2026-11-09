#!/usr/bin/env bash
# ARM64 QEMU kernel API tests. Never uses ADB or physical PMICs.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tc=${TOOLCHAINS_DIR:?Set TOOLCHAINS_DIR to the extracted Google tools}
out=${1:?Pass an absolute external KUnit output directory}
[[ "$out" == /* ]] || { echo 'Absolute output path required' >&2; exit 1; }
out=$(realpath -m "$out")
case "$out/" in "$root/"*) echo 'Output must be outside the source tree' >&2; exit 1;; esac
jobs=${JOBS:-8}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive' >&2; exit 1; }
export PATH="$tc/clang-r596125/bin:$PATH"
export KBUILD_BUILD_USER=adkaore KBUILD_BUILD_HOST=dipper-kunit
export KBUILD_BUILD_TIMESTAMP='Fri Nov 6 00:00:00 UTC 2026'
clang --version | grep -q r596125
command -v qemu-system-aarch64 >/dev/null
cd "$root"
mkdir -p "$out"
# Do not leave a previous successful result visible after a failed rerun.
rm -f "$out/dipper-kunit-report.json" "$out/typec-results.json"
revision=$(git rev-parse HEAD)
status=$(git status --porcelain)
python3 -u tools/testing/kunit/kunit.py run --arch=arm64 --make_options=LLVM=1 \
    --kunitconfig=tools/dipper/kunit/typec.config --build_dir="$out" --jobs="$jobs" \
    --timeout=90 \
    --kernel_args='earlycon=pl011,0x09000000 panic=-1' \
    --json="$out/typec-results.json" 'qcom-pmi8998-input-handoff*'
[[ "$revision" == "$(git rev-parse HEAD)" && "$status" == "$(git status --porcelain)" ]] || {
    echo 'Source revision/status changed during the run; refusing a checked report' >&2; exit 1;
}
python3 tools/dipper/kunit/check-results.py "$root" "$out" "$revision" "$status"
