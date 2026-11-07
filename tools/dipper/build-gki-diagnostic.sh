#!/usr/bin/env bash
# Non-hermetic diagnostic Kbuild, not a certified Kleaf/GKI release build.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tc=${TOOLCHAINS_DIR:?Set TOOLCHAINS_DIR to the extracted Google toolchains}
out=${1:?Pass an absolute output directory outside the source tree}
[[ "$out" == /* ]] || exit 1
out=$(realpath -m "$out")
case "$out/" in "$root/"*) echo 'Output must be outside source tree' >&2; exit 1;; esac
export PATH="$tc/clang-r596125/bin:$tc/1.91.1.p3/bin:$tc/clang-tools/bin:$PATH"
export LIBCLANG_PATH="$tc/clang-r596125/lib"
export RUST_LIB_SRC="$tc/1.91.1.p3/lib/rustlib/src/rust/library"
export KBUILD_BUILD_USER=adkaore KBUILD_BUILD_HOST=dipper-build
export KBUILD_BUILD_TIMESTAMP='Fri Nov 6 00:00:00 UTC 2026'
make -C "$root" O="$out" ARCH=arm64 LLVM=1 rustavailable
make -C "$root" O="$out" ARCH=arm64 LLVM=1 gki_defconfig
for config in RUST CFI DEBUG_INFO_BTF; do
    grep -qx "CONFIG_${config}=y" "$out/.config"
done
make -C "$root" O="$out" ARCH=arm64 LLVM=1 -j"${JOBS:-8}" Image modules
