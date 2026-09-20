#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BIN="build/nanobrowser"
if [ ! -x "$BIN" ]; then
    echo "Binary not found. Run ./setup.sh first." >&2
    exit 1
fi

# NANOBROWSER_RENDERER is passed through unchanged (e.g. "vulkan" or "opengl").
exec "$BIN" "$@"