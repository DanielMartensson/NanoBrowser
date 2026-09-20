#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

QT_VERSION="${QT_VERSION:-6.8.3}"                  # any Qt 6.8.x (must match the minimum Qt 6.8)
QT_INSTALL_DIR="${QT_INSTALL_DIR:-$HOME/Qt}"       # parent dir containing $QT_VERSION/gcc_64
QT_PREFIX="$QT_INSTALL_DIR/$QT_VERSION/gcc_64"
AQT_VENV="${AQT_VENV:-$HOME/.venvs/aqt}"

echo "==> Checking aqtinstall..."
if [ ! -x "$AQT_VENV/bin/aqt" ]; then
    echo "    Installing aqtinstall into $AQT_VENV"
    python3 -m venv "$AQT_VENV"
    "$AQT_VENV/bin/pip" install --quiet aqtinstall
fi

echo "==> Checking Qt $QT_VERSION..."
if [ ! -x "$QT_PREFIX/bin/qmake" ]; then
    echo "    Installing Qt $QT_VERSION to $QT_INSTALL_DIR (this downloads a few GB)"
    "$AQT_VENV/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
        -O "$QT_INSTALL_DIR" -m qtwebengine qtwebchannel qtpositioning
else
    echo "    Qt already present"
fi

echo "==> Configuring and building..."
cmake -S . -B build -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo
echo "Done. Run the browser with: ./run.sh"