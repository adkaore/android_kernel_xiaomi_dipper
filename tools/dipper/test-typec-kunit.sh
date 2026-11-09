#!/usr/bin/env bash
# Compatibility entry point: the shared run now also tests the charger side.
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/test-pmi8998-kunit.sh" "$@"
