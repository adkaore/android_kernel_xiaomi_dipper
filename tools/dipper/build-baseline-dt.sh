#!/usr/bin/env bash
# Compile existing Xiaomi SDM845 device trees; no Mi 8 hardware claims.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=${1:?Usage: build-baseline-dt.sh /absolute/external/output-directory}
[[ "$out" == /* ]] || { echo 'Output must be an absolute path.' >&2; exit 1; }
out=$(realpath -m "$out")
case "$out/" in "$root/"*) echo 'Keep build output outside the source tree.' >&2; exit 1;; esac
mkdir -p "$out"
jobs=${JOBS:-8}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive.' >&2; exit 1; }
make -C "$root" O="$out" ARCH=arm64 LLVM=1 defconfig
make -C "$root" O="$out" ARCH=arm64 LLVM=1 -j"$jobs" \
    qcom/sdm845-xiaomi-polaris.dtb \
    qcom/sdm845-xiaomi-beryllium-tianma.dtb \
    qcom/sdm845-xiaomi-beryllium-ebbg.dtb
python3 - "$root" "$out" <<'PY'
import hashlib,json,pathlib,subprocess,sys,datetime
root,out=map(pathlib.Path,sys.argv[1:])
def command(*args): return subprocess.check_output(args,cwd=root,text=True).strip()
paths=[out/'.config']+sorted((out/'arch/arm64/boot/dts/qcom').glob('sdm845-xiaomi-*.dtb'))
report={'kind':'stock-sdm845-dt-smoke-test','device':'NOT dipper',
        'full_kernel_built':False,'hardware_tested':False,
        'timestamp':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source_commit':command('git','rev-parse','HEAD'),
        'source_dirty':bool(command('git','status','--porcelain')),
        'clang':command('clang','--version').splitlines()[0],
        'make':command('make','--version').splitlines()[0],
        'artifacts':{str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
(out/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
PY
