# NanoBrowser

A lightweight Qt6/QML web browser for embedded Linux, targeted at the STM32MP257F
(OpenSTLinux / scarthgap BSP). Uses the native graphics API where available
(Vulkan first, OpenGL as fallback).

## Features

- Minimal dark-themed QML UI: back/forward buttons, address bar, load progress and
  fullscreen support (button or F11).
- Vulkan compositing (Qt RHI) when the GPU exposes a working Vulkan driver,
  automatic OpenGL fallback otherwise.
- WebGL enabled in the embedded Chromium (Qt WebEngine).
- Persistent cookies: the browser runs an on-the-record profile and stores cookies
  on disk (see "Cookies" below).
- Window title reflects the current page, e.g. `NanoBrowser - DuckDuckGo`.

## Dependencies

Build dependencies:

- Qt 6.8 or newer (the minimum is enforced in CMake via
  `find_package(Qt6 6.8 ...)`), with at least: `QtBase`, `Qt Quick`, `Qt Quick
  Controls`, `Qt WebEngine` (`QtWebEngineQuick`). A standard desktop Qt
  installation pulls in the transitive `Qt6WebChannel` and `Qt6Positioning` that
  WebEngine requires.
- CMake 3.16+
- A C++17-capable compiler (GCC or Clang).

Qt on the target board is provided by the Yocto/OpenSTLinux BSP (ST ships the
Qt 6.8 LTS line in the scarthgap layers, layer `meta-qt6`). For development on
x86_64 Linux, install any Qt 6.8.x (or newer) from
[download.qt.io](https://download.qt.io) — e.g. with
[aqtinstall](https://aqtinstall.readthedocs.io):

```sh
python3 -m venv ~/.venvs/aqt && ~/.venvs/aqt/bin/pip install aqtinstall
~/.venvs/aqt/bin/aqt install-qt linux desktop 6.8 linux_gcc_64 -O ~/Qt \
    -m qtwebengine qtwebchannel qtpositioning
```

Use the latest available patch release of your chosen Qt 6.8.x.

## Quick start

Two helper scripts (Linux):

- `./setup.sh` — one-time bootstrap: installs `aqtinstall`, downloads Qt 6.8.x
  to `~/Qt` if missing, and configures + builds into `build/`. Override the
  version or install dir with `QT_VERSION` / `QT_INSTALL_DIR`.
- `./run.sh` — launches the built browser (`build/nanobrowser`), passing through
  `NANOBROWSER_RENDERER` if set.

## Building

Configure with the path to your Qt 6.8+ installation, then build:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8/gcc_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Adjust `CMAKE_PREFIX_PATH` to your actual Qt installation. It is stored in the
build cache, so later rebuilds only need `cmake --build build`.

## Running

```sh
./build/nanobrowser
```

Graphics backend selection:

- A Vulkan instance is probed first. It is only accepted if a usable hardware
  driver is present — known-unstable or software implementations are skipped:
  NVK/nouveau, llvmpipe, and CPU-only devices. Otherwise OpenGL (RHI) is used.
- Force either backend explicitly with the `NANOBROWSER_RENDERER` environment
  variable: `opengl` or `vulkan` (Vulkan with a forced value skips the check).

The console stays quiet by default: the app sets `QT_LOGGING_RULES="*.warning=false"`
before Qt WebEngine starts, which suppresses spammy JavaScript console messages
from web pages (Google/YouTube traffic emits several per page load). Critical
messages still show. Override with your own rules for the full log, e.g.
`QT_LOGGING_RULES='*' ./run.sh`.

> Note for the development machine (Lenovo W540, Intel HD 4600 / NVIDIA Quadro
> K1100M with the open-source Nouveau driver): its Vulkan stack is incomplete
> (NVK crashes the WebEngine GPU process), so the app automatically falls back
> to OpenGL here.

## How it works

- `src/main.cpp` selects the scenegraph backend (`pickGraphicsApi()`), which
  probes Vulkan (rejecting NVK/nouveau, llvmpipe and CPU-only devices), then
  initializes Qt WebEngine with the correct ordering for Qt 6.8, and loads the
  QML module with `engine.loadFromModule("NanoBrowser", "Main")`.
- `qml/Main.qml` lays out the top bar (navigation buttons, address bar, progress),
  a `WebEngineView` with `webGLEnabled`, and a fullscreen auto-hide header.
- Chromium starts with `--enable-unsafe-swiftshader --ignore-gpu-blocklist`
  (`QTWEBENGINE_CHROMIUM_FLAGS`).

## Cookies

Cookies are enabled. The browser uses a persistent on-the-record `WebEngineProfile`
(`storageName: "nanobrowser"`, `persistentCookiesPolicy: ForcePersistentCookies`).
On Linux they are stored under:

```
~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/
```

(see the `Cookies` SQLite database). Set `offTheRecord: true` if you ever want an
incognito/profile-less mode instead.

## Clearing browsing data

Close the browser first, then remove the profile directory. The whole browsing
profile lives in one folder, so deleting it clears everything:

```sh
rm -rf ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser
```

That removes history, cookies, cached files, session and local storage (site
data). The folder is recreated automatically on the next launch.

To clear only individual parts, delete just the relevant files in that folder:

- Cookies only: `rm -f Cookies Cookies-journal`
- History only: `rm -f History History-journal "Visited Links"`
- Cache only: `rm -rf Cache GPUCache`
- Site data / local storage only: `rm -rf "Local Storage" Session Storage WebStorage databases`