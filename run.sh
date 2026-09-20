#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BIN="build/nanobrowser"
if [ ! -x "$BIN" ]; then
    echo "Binary not found. Run ./setup.sh first." >&2
    exit 1
fi

# Chromium/WebEngine child processes can lose HOME in their sandbox, which
# breaks fontconfig's default config lookup ("Cannot load default config file").
# Point them at the system config explicitly when available.
if [ -f /etc/fonts/fonts.conf ]; then
    export FONTCONFIG_FILE="${FONTCONFIG_FILE:-/etc/fonts/fonts.conf}"
    export FONTCONFIG_PATH="${FONTCONFIG_PATH:-/etc/fonts}"
fi

# Web pages often spam the console with Permissions-Policy notices in Qt
# WebEngine. Silence them by default while keeping critical messages.
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-*.warning=false}"

# NANOBROWSER_RENDERER is passed through unchanged (e.g. "vulkan" or "opengl").
exec "$BIN" "$@"