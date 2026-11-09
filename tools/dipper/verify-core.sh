#!/usr/bin/env bash
# Offline checks after the pinned toolchains and schema venv are provisioned.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
venv=${DT_SCHEMA_VENV:?Set DT_SCHEMA_VENV to a provisioned dtschema Python venv}
out=${1:?Pass an absolute external build output directory}
"$root/tools/dipper/build-core.sh" "$out"
out=$(realpath "$out")
export PATH="$venv/bin:$TOOLCHAINS_DIR/clang-r596125/bin:$TOOLCHAINS_DIR/1.91.1.p3/bin:$TOOLCHAINS_DIR/clang-tools/bin:$PATH"
export LIBCLANG_PATH="$TOOLCHAINS_DIR/clang-r596125/lib"
export RUST_LIB_SRC="$TOOLCHAINS_DIR/1.91.1.p3/lib/rustlib/src/rust/library"
cd "$root"
bindings=(arm/qcom.yaml display/panel/samsung,ea8074.yaml media/i2c/sony,imx363.yaml
    input/touchscreen/st,stmfts-ftm5.yaml input/qcom,spmi-haptics.yaml
    nvmem/qcom,pmi8998-rc-trim.yaml mfd/qcom,spmi-pmic.yaml
    power/supply/qcom,pmi8998-fg.yaml power/supply/qcom,pmi8998-charger.yaml
    usb/qcom,pmi8998-typec.yaml
    regulator/qcom,usb-vbus-regulator.yaml)
make O="$out" ARCH=arm64 LLVM=1 \
    DT_SCHEMA_FILES="$(IFS=:; echo "${bindings[*]}")" dt_binding_check
python3 tools/dipper/check-config.py arch/arm64/configs/dipper/core.config "$out/.config"
schemas="$out/Documentation/devicetree/bindings/processed-schema.json"
dtb_dir="$out/arch/arm64/boot/dts/qcom"
targets=$(python3 tools/dipper/list-board-targets.py --helpers --prefix "$dtb_dir/")
mapfile -t dtbs <<< "$targets"
dt-validate -s "$schemas" --json-output "$out/dipper-schema-result.json" "${dtbs[@]}"
# dt-validate may exit zero while reporting errors: inspect the JSON explicitly.
"$venv/bin/python" - "$out/dipper-schema-result.json" <<'PY'
import json,sys
with open(sys.argv[1]) as source:
    diagnostics=json.load(source)
if diagnostics:
    raise SystemExit(f'DTB validation produced {len(diagnostics)} diagnostics')
print('DTB schema diagnostics: 0')
PY
DIPPER_DTB_DIR="$dtb_dir" PYTHONDONTWRITEBYTECODE=1 "$venv/bin/python" \
    -m unittest discover -s tools/dipper/tests -v
"$root/tools/dipper/test-ftm5.sh" "$out/host-tests"
"$root/tools/dipper/test-haptics.sh" "$out/host-tests"
"$root/tools/dipper/test-fg.sh" "$out/host-tests"
"$root/tools/dipper/test-typec.sh" "$out/host-tests"
"$root/tools/dipper/test-smb2.sh" "$out/host-tests"
git diff --check
printf '\nPASS: build, all board/composed DTB schemas, tests and decoder sanitizers.\n'
printf 'No device boot, power, IRQ or camera hardware validation is implied.\n'
