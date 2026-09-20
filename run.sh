#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BIN="build/nanobrowser"
if [ ! -x "$BIN" ]; then
    echo "Binary not found. Run ./setup.sh first." >&2
    exit 1
fi

# Environment overrides:
#   NANOBROWSER_RENDERER: "vulkan" or "opengl" (forces the graphics backend)
#   QT_LOGGING_RULES:     logging rules, e.g. QT_LOGGING_RULES='*' for full logs
# Both are passed through and override the app defaults.
exec "$BIN" "$@"