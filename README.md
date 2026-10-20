# NanoBrowser

A small Qt 6 / QML web browser for embedded Linux, built on Qt WebEngine. It was
written for the STM32MP257F (OpenSTLinux, Weston, 2x Cortex-A35) and is tuned for
that board, but it builds and runs anywhere Qt 6.8 and Qt WebEngine do.

The point of the project is to be small and fast: no tabs, no extensions, no
settings page, no menu — one window with an address bar, and a page that appears
as soon as the hardware allows.

## Demo

![NanoBrowser demo](docs/nanobrowser.gif)

## Features

- Minimal dark QML UI: back, forward, reload, address bar, load progress, and a
  fullscreen button (or F11). The window is drawn by the app itself, so it has a
  title bar and resize edges on a compositor that draws no decorations.
- **Open a page from the command line**: `nanobrowser --page URL`.
- **Timing you can measure**: `NANOBROWSER_TIMING=1` reports every milestone of a
  page load (see [Measuring](#measuring-a-page-load)).
- **100 MB HTTP cache** on disk, so a second visit to a site costs only what
  changed.
- **Hardware everything**: Qt Quick composites through the GPU, Chromium
  rasterises on the same GPU, and frames are handed over by zero copy.
- **WebGL 2.0** on the embedded GPU (measured: `Vivante GCNANOULTRA31_VIP2`).
- **Quiet console**: no warnings from Qt or Wayland on startup, on a normal page
  load, or on exit.
- **Clean exit**: closing the window always ends with status 0.

## Dependencies

Build dependencies:

- Qt 6.8 or newer (enforced in CMake via `find_package(Qt6 6.8 ...)`), with at
  least `QtBase`, `Qt Quick`, `Qt Quick Controls` and `Qt WebEngine`
  (`QtWebEngineQuick`). A desktop Qt installation pulls in the transitive
  `Qt6WebChannel` and `Qt6Positioning` that WebEngine requires.
- CMake 3.16+
- A C++17 compiler (GCC or Clang).

The Yocto/OpenSTLinux BSP provides Qt 6.8 LTS (layer `meta-qt6`). For development
on x86_64, install any Qt 6.8.x from [download.qt.io](https://download.qt.io),
e.g. with [aqtinstall](https://aqtinstall.readthedocs.io):

```sh
python3 -m venv ~/.venvs/aqt && ~/.venvs/aqt/bin/pip install aqtinstall
~/.venvs/aqt/bin/aqt install-qt linux desktop 6.8 linux_gcc_64 -O ~/Qt \
    -m qtwebengine qtwebchannel qtpositioning
```

## Quick start

- `./setup.sh` — one-time bootstrap: installs `aqtinstall`, downloads Qt 6.8.x to
  `~/Qt` if missing, and configures + builds into `build/`. Override with
  `QT_VERSION` / `QT_INSTALL_DIR`.
- `./run.sh` — launches `build/nanobrowser` and passes its arguments through.

## Building

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8/gcc_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

`NANOBROWSER_DEFAULT_RENDERER` selects the Qt Quick backend at build time:

| value | meaning |
| --- | --- |
| `auto` (default) | Vulkan when the GPU has a usable 1.2+ driver, OpenGL otherwise |
| `opengl` | OpenGL only — one GL context shared with Chromium, zero copy |
| `vulkan` | Vulkan only, falling back to OpenGL (with a banner) if unusable |

The board build uses `opengl` because it was measured faster there and because it
exits cleanly; see [Performance](#performance-on-the-stm32mp257f).

## Running

```sh
./build/nanobrowser --page example.com
```

```
Usage: nanobrowser [--page URL]

  --page URL    Open URL (or a bare host name) at startup
  --help        Show this text
  --version     Show the version
```

A bare host name gets `https://` prepended, exactly like the address bar does.
Without `--page` the browser opens DuckDuckGo.

### Environment variables

| variable | effect |
| --- | --- |
| `NANOBROWSER_RENDERER=opengl\|vulkan` | force the Qt Quick backend for this run |
| `NANOBROWSER_CHROMIUM_GPU=full\|raster\|off` | how far Chromium may use the GPU (default `full`: GPU rasterisation + zero copy) |
| `NANOBROWSER_EXTRA_FLAGS="..."` | append Chromium switches, e.g. to try a flag without a rebuild |
| `NANOBROWSER_TIMING=1` | print page load timings (see below) |
| `NANOBROWSER_EXIT_AFTER=n` | close after `n` seconds, through the same path as the X button |

### Choosing the graphics backend

A Vulkan instance is probed and only accepted if a usable hardware driver is
present: NVK/nouveau, llvmpipe and CPU-only devices are rejected. On Wayland the
app prefers the `wayland-egl` platform plugin, because some BSPs (the
OpenSTLinux image for the STM32MP257F is one) build the plain `wayland` plugin
without `createPlatformVulkanInstance()`, and then Qt cannot create a platform
Vulkan instance at all.

An OpenGL fallback that was not asked for shows a one-line banner in the window
naming the reason. A build that chose OpenGL on purpose shows nothing.

> Development machine note (Lenovo W540, Intel HD 4600 / Quadro K1100M with
> Nouveau): its Vulkan stack is incomplete (NVK crashes the WebEngine GPU
> process), so `auto` lands on OpenGL there.

## Performance on the STM32MP257F

Measured on the board (Weston 13, Qt 6.8.4, 1280x720 HDMI, wired network), with
`NANOBROWSER_TIMING=1`, three runs per row, numbers from a run where the machine
was idle. "First frame" is the moment the window has something on screen; "load"
is `loadEventEnd` plus the last resource; "fps" is what the page itself counted
through `requestAnimationFrame` while it was being scrolled for three seconds
after loading.

| page | first frame | load | TTFB (warm) | fps |
| --- | --- | --- | --- | --- |
| `example.com` | 1.3 s | 1.7 s | 0.33 s | 60 |
| `en.wikipedia.org/wiki/STM32` | 1.3 s | 5.5 s | 0.57 s | 23 |
| `duckduckgo.com` | 1.3 s | 3.7 s | 0.53 s | 14 |
| `youtube.com` | 1.3 s | 15.0 s | 0.56 s | 1 |

The same pages with the Vulkan backend (what this board used to run):

| page | first frame | load |
| --- | --- | --- |
| `example.com` | 3.1 s | 4.1 s |
| `en.wikipedia.org/wiki/STM32` | 3.3 s | 8.3 s |
| `duckduckgo.com` | 3.1 s | 6.7 s |
| `youtube.com` | 2.9 s | 17.5 s |

and the process died with a segmentation fault on exit, in the VeriSilicon
driver, while the OpenGL build exits with status 0.

With an empty cache (profile deleted before the run) the first visit pays the
round trip and the TLS handshake: `example.com` 2.2 s, `duckduckgo.com` 4.1 s,
`youtube.com` 15.3 s, with TTFB around 0.9 s instead of 0.3-0.6 s. The 100 MB
cache is what makes the second visit cheap.

YouTube is simply heavy on two Cortex-A35 cores: the page's own script keeps the
main thread busy for thirteen seconds after the first byte (DOMContentLoaded at
13.7 s), and it keeps producing work afterwards, which is the 1 fps. Nothing in
the browser can change that: the page is the load.

### Speedometer 2.1, and what the GPU is doing

[Speedometer 2.1](https://browserbench.org/Speedometer2.1/) is the page the
project suggests for comparing browsers: JavaScript, DOM and layout, no WebGL,
so it says how much work the GPU can take away even when nothing is 3D. It was
run in a 1000x780 window - the page asks for a viewport of at least 850x650,
the normal window is 1000x640 - and takes thirteen minutes per run on this
board.

| configuration | Speedometer 2.1 | WebGL 2.0 |
| --- | --- | --- |
| `NANOBROWSER_CHROMIUM_GPU=full` (the default) | 9.33 | hardware, `Vivante GCNANOULTRA31_VIP2` |
| `NANOBROWSER_CHROMIUM_GPU=off` | 9.20 | none |

The two scores differ by less than a run: the benchmark is limited by the two
Cortex-A35 cores, not by rasterisation, so doing that work on the CPU does not
make it slower either. What the GPU does decide is what a page can do at all.
With the default mode a WebGL context reports

```
WebGL 2.0 (OpenGL ES 3.0 Chromium), renderer "Vivante GCNANOULTRA31_VIP2"
```

and `chrome://gpu` lists Canvas, Compositing, Rasterization, Video Decode,
WebGL and WebGL2 as *Hardware accelerated* (Vulkan and WebGPU are disabled, as
they should be here). With `GPU=off` there is no WebGL at all: Chromium starts
with `--disable-gpu`, and `--enable-unsafe-swiftshader` does not bring a
software fallback back.

What was measured and rejected, so it does not get tried again:

| change | result |
| --- | --- |
| Vulkan Qt Quick backend | first frame 2.4x later, pages 1.2-2.4x slower, segfault on exit |
| `--disable-gpu-compositing` (Chromium raster only) | Wikipedia scrolling 18.5 fps instead of 29 |
| `--enable-low-end-device-mode` | 9 fps, first frame 26 s on YouTube |
| `--num-raster-threads=2` | 23 fps, no better than the default |
| `--renderer-process-limit=1` | 14 fps on Wikipedia |
| no zero copy | 18.5 fps instead of 29, and slower loads |
| translation, cast, component updater, background networking | neutral on load time, kept because they do no work for nothing |

### Measuring a page load

`NANOBROWSER_TIMING=1` prints one line per milestone, all measured from the
instant `main()` started:

```
NanoBrowser[timing]: qml ready at 962 ms
NanoBrowser[timing]: load started: https://en.wikipedia.org/wiki/STM32 at 1285 ms
NanoBrowser[timing]: first frame at 1441 ms
NanoBrowser[timing]: load finished: https://en.wikipedia.org/wiki/STM32 at 5681 ms
NanoBrowser[timing]: page ttfb 719 ms, dom 1729 ms, load 4404 ms, 1 kB in 17 resources
NanoBrowser[timing]: page 23.5 fps over 3.1 s while scrolling
```

The next three lines come from the page, not from the browser: time to first
byte, DOMContentLoaded and the load event as the page measured them itself, plus
how many bytes and resources arrived. The frame rate is counted by scrolling the
page through `requestAnimationFrame` for three seconds, two and a half seconds
after the load, so it includes Chromium's rasterisation and Qt's compositing —
what a finger on the screen feels. Anything a page logs at info level is echoed
in this mode too (`NanoBrowser[page]: ...`), which is how a WebGL probe reports
the renderer.

## How it works

- `src/main.cpp` parses `--page`, picks the scene graph backend
  (`pickGraphicsApi()`), makes sure the session has a usable Wayland socket, a
  UTF-8 character type and a sound server, sets the Chromium switches
  (`QTWEBENGINE_CHROMIUM_FLAGS`), and loads the QML module with
  `engine.loadFromModule("NanoBrowser", "Main")`. It also exposes the context
  properties the QML needs: the URL from the command line, the process start
  time, and the timing/exit switches.
- `qml/Main.qml` is the whole UI: client-side title bar (drag to move, edges to
  resize), the top bar (navigation, address bar, progress), and one
  `WebEngineView` in a persistent on-the-record `WebEngineProfile` with a 100 MB
  disk cache.
- Chromium runs with `--enable-unsafe-swiftshader --ignore-gpu-blocklist
  --enable-gpu-rasterization --enable-zero-copy` (under OpenGL) or
  `--disable-gpu` (under Vulkan, where Chromium's own Vulkan presenter aborts the
  process), plus `--disable-features=WebGPU,Translate,MediaRouter,OptimizationHints`
  and the background-work switches. The exact command line is printed at
  startup.
- Each window is also given `graphics.log` in `~/.local/share/NanoBrowser/`,
  which records the backend that was picked and why.

## The console stays quiet

Three things are deliberately silenced, and each has a reason:

- **Page console messages** would otherwise arrive through
  `javaScriptConsoleMessage` on every page load (Google and YouTube emit several
  per page). They are dropped, except at info level when
  `NANOBROWSER_TIMING=1` is set.
- **The VeriSilicon Vulkan ICD** is never asked for extensions it does not have,
  and Vulkan is only offered to Qt Quick when the probe says the driver can
  survive it.
- **The exit** does not let QtWebEngine tear Chromium down: that teardown is
  where libwayland reports `queue destroyed while proxies still attached`, on a
  page that was animating right up to the close. QML gives the page 300 ms to go
  idle and then ends the session (`Session::closeNow()`); the exit status is 0
  and nothing is printed. Freezing the page, hiding the window or destroying the
  view first were all tried and either warned or deadlocked.

## Cookies

Cookies are enabled. The browser uses a persistent on-the-record
`WebEngineProfile` (`storageName: "nanobrowser"`,
`persistentCookiesPolicy: ForcePersistentCookies`). On Linux the profile lives in:

```
~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/
```

(the `Cookies` database is there; the HTTP cache is under
`~/.cache/NanoBrowser/`). Set `offTheRecord: true` in `qml/Main.qml` for a
profile-less mode.

## Clearing cookies and history

Always close NanoBrowser first: WebEngine keeps the SQLite files in the profile
locked while it runs.

### Delete everything (full reset)

```sh
rm -rf ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser
```

Cookies, history, cache, session and local storage all live there. The directory
is recreated on the next launch.

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

- HTTP cache: `rm -rf ~/.cache/NanoBrowser/QtWebEngine/nanobrowser`
  (plus `GPUCache` in the profile folder)
- Site data / local storage:
  `rm -rf ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/"Local Storage" \
         ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/"Session Storage" \
         ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/WebStorage \
         ~/.local/share/NanoBrowser/QtWebEngine/nanobrowser/databases`

If the profile was ever run off-the-record, a leftover `databases-off-the-record`
folder may remain; it is safe to delete.

## Troubleshooting

- **`chrome://gpu` works, `chrome://version` and `about:gpu` do not.** Qt WebEngine
  only serves a few `chrome://` routes; anything else fails with
  `net::ERR_INVALID_URL`. What `chrome://gpu` reports matches what this browser
  does: WebGL 2.0 on the Vivante GPU, GPU rasterisation on, zero copy on.
- **Video plays in software.** Qt WebEngine cannot use this SoC's stateless V4L2
  decoder (nor is there a VA-API on the target), so an HTML5 `<video>` always runs
  on the CPU. Playing a YouTube video through the hardware decoder is what
  [YtGst](https://github.com/DanielMartensson/YtGst) is for.
- **`NANOBROWSER_RENDERER=vulkan` crashes on exit** on the STM32MP257F: the
  VeriSilicon driver does not survive the window teardown. That is why the board
  build defaults to OpenGL.
