# NanoBrowser

A lightweight Qt6/QML web browser for embedded Linux, targeted at the STM32MP257F
(OpenSTLinux / scarthgap BSP). Uses the native graphics API where available
(Vulkan first, OpenGL as fallback).

## Demo

![NanoBrowser demo](docs/nanobrowser.gif)

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

The console stays quiet by default: page JavaScript console messages (Google/YouTube
traffic emits several per page load) are swallowed in `qml/Main.qml` through the
`WebEngineView.javaScriptConsoleMessage` handler. To see the full page console
output again, remove that handler or run with `QT_LOGGING_RULES='*' ./run.sh`.

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

## Clearing cookies and history

Chrome/WebEngine keeps the SQLite files in the profile folder locked while the
browser runs, so always close NanoBrowser first (quit the window or press
`Ctrl+C` in the terminal), then delete what you need.

### Delete everything (full reset)

The whole browsing profile lives in one directory, so removing it clears
cookies, history, cache, session and local storage at once:

```sh
rm -rf ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser
```

The directory is recreated automatically on the next launch.

### Delete only the cookies

```sh
rm -f ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/Cookies \
      ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/Cookies-journal
```

### Delete only the history

```sh
rm -f ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/History \
      ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/History-journal \
      ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/"Visited Links"
```

### Clear other site data

- HTTP cache: `rm -rf ~/.cache/NanoBrowser/QtWebEngine/nanobrowser` (plus
  `GPUCache` in the profile folder)
- Site data / local storage:
  `rm -rf ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/"Local Storage" \
         ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/"Session Storage" \
         ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/WebStorage \
         ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/databases`

If the profile was ever run off-the-record, a leftover `databases-off-the-record`
folder may remain in the profile directory; it is safe to delete.