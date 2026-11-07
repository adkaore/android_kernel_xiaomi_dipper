#!/usr/bin/env bash
set -euo pipefail
missing=0
for tool in git python3 make clang ld.lld gcc flex bison bc openssl pkg-config; do
    if command -v "$tool" >/dev/null; then
        printf 'OK      %s\n' "$tool"
    else
        printf 'MISSING %s\n' "$tool"
        missing=1
    fi
done
printf '\nStorage (WSL and host):\n'
df -h .
if [[ -d /mnt/c ]]; then df -h /mnt/c; fi
printf '\nMemory:\n'
free -h
printf '\nThis is a host inventory, not proof of ACK build readiness.\n'
exit "$missing"
