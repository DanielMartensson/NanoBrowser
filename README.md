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
- Hardware-accelerated playback of YouTube videos: the video is streamed from
  YouTube and decoded by the VPU (`v4l2slh264dec`), audio plays through PipeWire
  (see "Hardware video decoding on the STM32MP257F" below).
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
- On Wayland the app prefers the `wayland-egl` platform plugin over the default
  `wayland` one when both are installed. Some BSPs (the OpenSTLinux image for the
  STM32MP257F is one) build the `wayland` plugin without
  `createPlatformVulkanInstance()`, so Qt cannot create a platform Vulkan
  instance and Qt Quick silently falls back to OpenGL; the EGL variant can. Set
  `QT_QPA_PLATFORM` to override the choice.

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
  probes Vulkan (rejecting NVK/nouveau, llvmpipe and CPU-only devices), picks a
  Vulkan-capable platform plugin (Wayland: `preferVulkanCapablePlatform()`),
  initializes Qt WebEngine with the correct ordering for Qt 6.8, and loads the
  QML module with `engine.loadFromModule("NanoBrowser", "Main")`.
- `src/main.cpp` also exposes `HardwarePlayer` to QML (`hardwarePlayer`), which
  resolves a YouTube page with `yt-dlp` and plays the H.264/AAC streams on the
  VPU with GStreamer.
- `qml/Main.qml` lays out the top bar (navigation buttons, address bar, progress),
  a `WebEngineView` with `webGLEnabled`, the hardware-playback card and a
  fullscreen auto-hide header.
- Chromium starts with `--enable-unsafe-swiftshader --ignore-gpu-blocklist
  --enable-gpu-rasterization --enable-zero-copy --disable-features=WebGPU`
  (`QTWEBENGINE_CHROMIUM_FLAGS`).

## Hardware video decoding on the STM32MP257F

The VPU of the STM32MP25 is a *stateless* V4L2 decoder (Hantro, `/dev/video5`)
that decodes H.264 and VP8. Two consequences for a browser on this SoC:

- There is no VA-API on the target, and Chromium does not implement the
  stateless V4L2 video API, so Qt WebEngine can never decode video in hardware
  here: an HTML5 `<video>` element always runs on the CPU, and the dual
  Cortex-A35 cannot decode 720p H.264 in real time. Of the codecs YouTube
  offers, only H.264 is decodable by this VPU (no VP9, no HEVC).
- The route to the VPU is therefore GStreamer, which is what the play button
  (or F8) on a YouTube page uses:

  ```
  yt-dlp -e -g -f 'bv*[vcodec^=avc1][height<=1080]+ba[ext=m4a]/...'
  souphttpsrc location=<video> ! qtdemux ! h264parse ! v4l2slh264dec ! queue ! waylandsink
  souphttpsrc location=<audio> ! qtdemux ! queue ! aacparse ! avdec_aac ! audioconvert ! audioresample ! pulsesink
  ```

  Nothing is downloaded: `yt-dlp` only resolves the direct DASH URLs, which
  GStreamer then streams from YouTube's servers. Video and audio are decoded in a
  single pipeline so that they share the pipeline clock (`GstPulseSinkClock`,
  i.e. PipeWire) and stay in sync. `waylandsink` gets its own Wayland surface,
  which Weston composites together with the (Vulkan-rendered) browser window;
  `qml6glsink` is deliberately not used, because it renders through Qt's OpenGL
  context, which does not exist when Qt Quick composites on the Vulkan RHI. The
  page's own player is paused and muted before playback starts, so that there is
  only one audio source and the CPU does not decode the video a second time.

Requirements on the target: `yt-dlp`, the GStreamer command line tools, the
`v4l2slh264dec` and `waylandsink` elements and `/dev/video5`. If any of them is
missing, the hardware-playback buttons stay disabled and the browser behaves as
before.

Troubleshooting: the output of the GStreamer pipeline is written to
`gstreamer.log` next to `graphics.log`
(`~/.local/share/NanoBrowser/gstreamer.log`).

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