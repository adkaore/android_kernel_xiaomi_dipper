#!/usr/bin/env bash
# Non-hermetic development build, not a certified/flashable Android release.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tc=${TOOLCHAINS_DIR:?Set TOOLCHAINS_DIR to the extracted Google tools}
out=${1:?Pass an absolute output directory outside this source tree}
[[ "$out" == /* ]] || { echo 'Absolute output path required' >&2; exit 1; }
out=$(realpath -m "$out")
case "$out/" in "$root/"*) echo 'Output must be outside the source tree' >&2; exit 1;; esac
jobs=${JOBS:-8}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive' >&2; exit 1; }
export PATH="$tc/clang-r596125/bin:$tc/1.91.1.p3/bin:$tc/clang-tools/bin:$PATH"
export LIBCLANG_PATH="$tc/clang-r596125/lib"
export RUST_LIB_SRC="$tc/1.91.1.p3/lib/rustlib/src/rust/library"
export KBUILD_BUILD_USER=adkaore KBUILD_BUILD_HOST=dipper-build
export KBUILD_BUILD_TIMESTAMP='Fri Nov 6 00:00:00 UTC 2026'
clang --version | grep -q r596125
mkdir -p "$out"
cd "$root"
make O="$out" ARCH=arm64 LLVM=1 rustavailable
scripts/kconfig/merge_config.sh -m -O "$out" \
    arch/arm64/configs/gki_defconfig arch/arm64/configs/dipper/core.config
make O="$out" ARCH=arm64 LLVM=1 olddefconfig
python3 tools/dipper/check-config.py arch/arm64/configs/dipper/core.config "$out/.config"
make O="$out" ARCH=arm64 LLVM=1 -j"$jobs" \
    Image modules qcom/sdm845-xiaomi-dipper-mp-v2.1.dtb
python3 - "$root" "$out" <<'PY'
import datetime,hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:])
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as source:
        for data in iter(lambda:source.read(1048576),b''):
            h.update(data)
    return h.hexdigest()
files=['.config','arch/arm64/boot/Image',
       'arch/arm64/boot/dts/qcom/sdm845-xiaomi-dipper-mp-v2.1.dtb']
modules=sorted(str(p.relative_to(out)) for p in out.rglob('*.ko'))
report={'kind':'dipper-core-development-build','hardware_tested':False,
        'flashable_boot_image':False,'hermetic_kleaf_build':False,
        'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        'source_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()),
        'sha256':{name:digest(out/name) for name in files},'modules':modules}
(out/'dipper-build-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(f"Build passed: Image, DTB, {len(modules)} modules")
PY
