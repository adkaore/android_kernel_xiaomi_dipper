#!/usr/bin/env bash
# Optional read-only inventory. Run only when the owner connects/authorizes ADB.
# No reboot, root, writes, extraction of calibration, serials or unique IDs.
set -euo pipefail
adb=${ADB:-adb}
"$adb" get-state >/dev/null
"$adb" shell 'for key in ro.product.device ro.build.version.release ro.crdroid.version ro.boot.hardware ro.boot.hwversion ro.boot.hwrev ro.boot.hwc; do printf "%s=" "$key"; getprop "$key"; done; printf "kernel="; uname -r; for prop in model compatible; do path=/proc/device-tree/$prop; if [ -r "$path" ]; then printf "%s=" "$prop"; tr "\000" "\n" < "$path"; fi; done; for f in /sys/bus/i2c/devices/*/name; do [ -r "$f" ] || continue; name=$(cat "$f"); case "$name" in *fts*|*mxt*|*atmel*|*FTS*|*Focal*) printf "touch_candidate=%s\n" "$name";; esac; done'
