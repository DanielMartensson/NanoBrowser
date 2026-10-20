#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BIN="build/nanobrowser"
if [ ! -x "$BIN" ]; then
    echo "Binary not found. Run ./setup.sh first." >&2
    exit 1
fi

# Arguments are passed to the browser, e.g.:
#   ./run.sh --page example.com
#
# Environment overrides:
#   NANOBROWSER_RENDERER:    "vulkan" or "opengl" (forces the graphics backend)
#   NANOBROWSER_CHROMIUM_GPU: full, raster or off (default full)
#   NANOBROWSER_EXTRA_FLAGS: more Chromium switches
#   NANOBROWSER_TIMING:      "1" to print page load timings
#   NANOBROWSER_EXIT_AFTER:  seconds until the window closes itself
#   QT_LOGGING_RULES:        logging rules, e.g. QT_LOGGING_RULES='*' for full logs
exec "$BIN" "$@"