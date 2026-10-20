#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QVulkanInstance>
#include <QtWebEngineQuick>
#include <QUrl>
#include <QDebug>
#include <QStandardPaths>
#include <QVarLengthArray>
#include <QVersionNumber>
#include <QtGlobal>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <langinfo.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

// NOTE: The target's Qt 6.8 (OpenSTLinux) is a trimmed/renamed Qt build.
// This code must therefore avoid the standard Qt 6 APIs that are missing
// there: QString::leftOfLastPathSeparator()/leftPart(),
// QIODevice::AppendOnly, QFile::openStatus()/QFile::Ok, and
// QQmlEngine::addContextProperty(). Context properties are set through
// QQmlEngine::rootContext()->setContextProperty() instead.

static QtMessageHandler g_previousMessageHandler = nullptr;

// Reached from QML when the window is closed (X, Alt+F4, NANOBROWSER_EXIT_AFTER):
// the page has been given a moment to idle, and then the process goes. QtWebEngine
// tears Chromium down from QCoreApplication::aboutToQuit, and that teardown is
// exactly where libwayland reports the window queue it destroys with buffer
// releases still attached. The session is over by then - the page is idle, the
// cache has been written - so step out before that teardown runs.
class Session : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    Q_INVOKABLE void closeNow()
    {
        std::fflush(nullptr);
        std::_Exit(0);
    }
};

static QString g_graphicsApi = QStringLiteral("OpenGL");
static QString g_graphicsReason = {};
// True only when Vulkan was wanted and could not be used. The banner in the
// window is for that case; a deliberate OpenGL build has nothing to announce.
static bool g_graphicsFallback = false;

// The moment main() started, in milliseconds since the epoch. QML reports its
// milestones against this, so "the page is up" can be measured from the outside
// without a stopwatch in the launcher.
static qint64 g_processStartMs = 0;

static void printUsage(const char *program)
{
    std::printf(
        "Usage: %s [--page URL]\n"
        "\n"
        "  --page URL    Open URL (or a bare host name) at startup\n"
        "  --help        Show this text\n"
        "  --version     Show the version\n"
        "\n"
        "Environment:\n"
        "  NANOBROWSER_RENDERER=opengl|vulkan   Force the Qt Quick backend\n"
        "  NANOBROWSER_CHROMIUM_GPU=full|raster|off\n"
        "                                       How far Chromium may use the GPU\n"
        "  NANOBROWSER_EXTRA_FLAGS=\"...\"      Extra Chromium switches\n"
        "  NANOBROWSER_TIMING=1                 Print page load timings\n"
        "  NANOBROWSER_EXIT_AFTER=SECONDS       Close the way the X button does\n",
        program);
}

// "--page example.com" turns into https://example.com, exactly as the address
// bar would have done it.
static QString normaliseUrl(const QString &text)
{
    QString url = text.trimmed();
    if (url.isEmpty())
        return {};
    if (!url.contains(QStringLiteral("://")))
        url.prepend(QStringLiteral("https://"));
    return url;
}

static void nanoBrowserMessageHandler(QtMsgType type, const QMessageLogContext &context,
                                      const QString &message)
{
    if (message.startsWith(QLatin1String("js:")))
        return;
    g_previousMessageHandler(type, context, message);
}

static QByteArray rendererFromEnv()
{
    return qgetenv("NANOBROWSER_RENDERER").toLower();
}

static QString graphicsLogPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/graphics.log");
}

static void appendGraphicsLog()
{
    const QString path = graphicsLogPath();
    // QFileInfo::absolutePath() is the directory the log file lives in;
    // QDir::absolutePath() would hand back the file path itself and create a
    // *directory* named graphics.log (which is what happened on the board -
    // and such a leftover keeps QFile::open() failing on every later run).
    const QFileInfo info(path);
    const QString dir = info.absolutePath();
    if (!dir.isEmpty() && !QDir(dir).exists() && !QDir().mkpath(dir))
        return;
    if (info.isDir())
        QDir().rmdir(path);

    const QString line = QString("%1 %2 %3\n").arg(
                              QDateTime::currentDateTime().toString(Qt::ISODate),
                              g_graphicsApi,
                              g_graphicsReason).toUtf8();

    QFile logFile(path);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logFile.seek(logFile.size()); // QIODevice::AppendOnly is not available
        logFile.write(line.toUtf8());
        logFile.close();
    }
}

static bool unstableVulkanIcdPresent()
{
    const QDir::Filters filters = QDir::Files;
    const QStringList filter({QStringLiteral("*.json")});
    const char *const icdDirs[] = {
        "/usr/local/share/vulkan/icd.d",
        "/usr/share/vulkan/icd.d",
        "/etc/vulkan/icd.d",
    };
    const char *const unstableNames[] = { "nouveau", "hasvk" };

    const QString envIcds = QString::fromUtf8(qgetenv("VK_ICD_FILENAMES"));
    for (const QString &icd : envIcds.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString name = QFileInfo(icd).fileName();
        for (const char *u : unstableNames)
            if (name.contains(QLatin1String(u), Qt::CaseInsensitive))
                return true;
    }

    for (const char *dir : icdDirs) {
        const QDir d{QLatin1String(dir)};
        if (!d.exists())
            continue;
        const QStringList icds = d.entryList(filter, filters);
        for (const QString &name : icds)
            for (const char *u : unstableNames)
                if (name.contains(QLatin1String(u), Qt::CaseInsensitive))
                    return true;
    }
    return false;
}

static bool vulkanHardwareAvailable(QVulkanInstance &instance)
{
    PFN_vkEnumeratePhysicalDevices enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        instance.getInstanceProcAddr("vkEnumeratePhysicalDevices"));
    PFN_vkGetPhysicalDeviceProperties getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        instance.getInstanceProcAddr("vkGetPhysicalDeviceProperties"));
    if (!enumerate || !getProperties)
        return false;

    uint32_t count = 0;
    if (enumerate(instance.vkInstance(), &count, nullptr) != VK_SUCCESS || count == 0)
        return false;

    QVarLengthArray<VkPhysicalDevice, 8> devices(count);
    if (enumerate(instance.vkInstance(), &count, devices.data()) != VK_SUCCESS)
        return false;

    bool sawHardware = false;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        getProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
            continue;
        const QByteArray name = QByteArray(props.deviceName);
        if (name.contains("llvmpipe"))
            continue;
        if (name.contains("NVK") || name.contains("nouveau"))
            return false;
        sawHardware = true;
    }
    return sawHardware;
}

static bool vulkanInstanceUsable(QVulkanInstance &instance)
{
    // Chromium (QtWebEngineCore) requires a Vulkan 1.2 instance and
    // presents to the compositor via external VkImage file
    // descriptors. The legacy GCNANO/VeriSilicon ICD on the Weston
    // target reports only loader interface v2 and does not expose
    // vkEnumerateInstanceVersion; the loader then falls back to a
    // 1.0-level instance and QtWebEngineCore aborts the process when
    // it later tries to export those image file descriptors
    // ("Unable to extract file descriptor out of external VkImage").

    // Requiring a 1.2+ instance keeps this broken probe path out on
    // the target. Future-proof: a newer, fully conformant ICD passes
    // this check and enables real Vulkan without code changes.
    // The target Vulkan headers declare vkEnumerateInstanceVersion
    // with a single uint32_t* argument and define VK_API_VERSION_1_2.
    PFN_vkEnumerateInstanceVersion enumerate = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        instance.getInstanceProcAddr("vkEnumerateInstanceVersion"));
    if (!enumerate)
        return false;

    uint32_t supportedVersion = 0;
    if (enumerate(&supportedVersion) != VK_SUCCESS)
        return false;
    return supportedVersion >= VK_API_VERSION_1_2;
}

static void useOpenGL(const QString &reason, bool fallback)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
    g_graphicsApi = QStringLiteral("OpenGL");
    g_graphicsReason = reason;
    g_graphicsFallback = fallback;
    qInfo().noquote() << "NanoBrowser: using OpenGL renderer ("
                      << (reason.isEmpty() ? QStringLiteral("chosen at build time") : reason)
                      << ")";
    appendGraphicsLog();
}

static void pickGraphicsApi()
{
    const QByteArray forced = rendererFromEnv();
    // What was asked for: the environment first, then the build (the board is
    // built with opengl, a desktop build defaults to auto).
    const QString wanted = forced.isEmpty()
        ? QStringLiteral(NANOBROWSER_DEFAULT_RENDERER)
        : QString::fromLatin1(forced);

    if (wanted == QLatin1String("opengl")) {
        // OpenGL gives Chromium and Qt Quick one shared GL context: Chromium
        // rasterises on the GPU and hands the tiles over without a copy. On the
        // STM32MP257F that is 2.3x faster to the first frame than the Vulkan
        // backend (1.3 s against 3.1 s), YouTube loads in 14.9 s instead of
        // 17.5 s, and the process exits with status 0 - the VeriSilicon driver
        // does not survive the teardown of a window it rendered.
        useOpenGL(forced.isEmpty()
                      ? QStringLiteral("the build's choice")
                      : QStringLiteral("forced via NANOBROWSER_RENDERER"), false);
        return;
    }

    if (wanted == QLatin1String("auto") && unstableVulkanIcdPresent()) {
        useOpenGL(QStringLiteral("unstable Vulkan drivers detected (nouveau/hasvk)"), true);
        return;
    }

    QVulkanInstance probe;
    // The VeriSilicon ICD on the target refuses a VkApplicationInfo whose
    // apiVersion is 0 ("value of 0 which is not permitted") and prints an ERROR
    // line about it before the window exists. Qt leaves the field at 0 unless a
    // version is set, so ask for the lowest version Vulkan defines - this probe
    // only enumerates devices, it never creates one.
    probe.setApiVersion(QVersionNumber(1, 0));
    const bool instanceCreated = probe.create();
    const bool hardwareSuitable = instanceCreated
        && vulkanHardwareAvailable(probe);
    const bool instanceUsable = hardwareSuitable
        && vulkanInstanceUsable(probe);
    const bool suitable = instanceUsable;

    const QString vulkanFallbackReason =
        !instanceCreated
            ? QStringLiteral("could not create a Vulkan instance")
            : !hardwareSuitable
                ? QStringLiteral("no supported Vulkan GPU")
                : QStringLiteral("Vulkan instance below 1.2 (ICD does not expose vkEnumerateInstanceVersion)");

    if (!suitable) {
        // Even an explicit NANOBROWSER_RENDERER=vulkan falls back: a Vulkan
        // instance Qt cannot use would leave the window without a renderer.
        useOpenGL(vulkanFallbackReason, true);
        return;
    }

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    g_graphicsApi = QStringLiteral("Vulkan");
    g_graphicsReason.clear();
    g_graphicsFallback = false;
    qInfo() << "NanoBrowser: using Vulkan renderer";
    appendGraphicsLog();
}

static void preferVulkanCapablePlatform()
{
    // All the platform plugins that Qt ships can create a QPlatformIntegration,
    // but only some of them can also create a platform *Vulkan* instance
    // (createPlatformVulkanInstance()). On the STM32MP2 (OpenSTLinux) image the
    // default Wayland plugin, libqwayland-generic.so, cannot - so
    // QVulkanInstance::create() fails and Qt Quick silently falls back to
    // OpenGL even though the VeriSilicon GPU exposes Vulkan 1.3. Its sibling
    // libqwayland-egl.so does implement it.
    //
    // Prefer the EGL variant when we are on Wayland and the user has not chosen
    // a platform themself. QT_QPA_PLATFORM still wins.
    if (!qgetenv("QT_QPA_PLATFORM").isEmpty())
        return;
    if (qgetenv("WAYLAND_DISPLAY").isEmpty() && qgetenv("WAYLAND_SOCKET").isEmpty())
        return;

    // Deliberately avoid QLibraryInfo here (the target Qt build is trimmed) and
    // this has to run before QGuiApplication exists. Qt's own plugin search
    // paths plus the well-known target locations are enough.
    QStringList pluginDirs = QCoreApplication::libraryPaths();
    pluginDirs << QStringLiteral("/usr/lib/plugins")
               << QStringLiteral("/usr/lib/qt6/plugins")
               << QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/plugins");
    for (const QString &dir : pluginDirs) {
        if (QFile::exists(dir + QStringLiteral("/platforms/libqwayland-egl.so"))) {
            qputenv("QT_QPA_PLATFORM", "wayland-egl");
            qInfo() << "NanoBrowser: platform plugin wayland-egl (can create a Vulkan instance)";
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// A UTF-8 character type
// ---------------------------------------------------------------------------
//
// The image ships en_US.UTF-8 in /usr/lib/locale/locale-archive, but sessions
// start with LANG=C, and Qt then warns that it depends on a UTF-8 locale. That
// warning is printed once per Qt process, and Qt's text handling really does
// want UTF-8: with an ASCII character type, QString::fromLocal8Bit() mangles
// non-ASCII paths and environment values.
//
// Only LC_CTYPE is changed. Number, date and collation formats keep following
// whatever the session asks for, and LANG is left alone, so Chromium still
// picks the same UI language and sends the same Accept-Language header.
static void preferUtf8CharacterType()
{
    if (setlocale(LC_CTYPE, "") == nullptr)
        return;
    const QByteArray codeset = nl_langinfo(CODESET);
    if (codeset == "UTF-8")
        return;
    if (setlocale(LC_CTYPE, "en_US.UTF-8") == nullptr)
        return;   // no UTF-8 locale in the image; nothing to switch to
    qputenv("LC_CTYPE", "en_US.UTF-8");
    qInfo("NanoBrowser: character type was %s, using en_US.UTF-8", codeset.constData());
}

// ---------------------------------------------------------------------------
// Finding the sound server
// ---------------------------------------------------------------------------
//
// Whether a PulseAudio/PipeWire socket could be pointed at (see below). Qt
// WebEngine sends the audio of the pages here, so the socket has to be found
// before the first page starts playing.
static bool g_soundServerAvailable = false;

// The sound server is not where libpulse looks by default on this image:
// weston.service hands the session an XDG_RUNTIME_DIR of /home/weston, while
// PipeWire keeps its socket in /run/user/<uid>/pulse/native. Qt WebEngine
// inherits this process' environment, so it is enough to point PULSE_SERVER at
// a socket that really exists:
//   * the socket in XDG_RUNTIME_DIR, when it is ours and really a socket;
//   * otherwise our own /run/user/<uid>/pulse/native (the normal case when the
//     browser is started with the wrong runtime directory);
//   * as root, any /run/user/<uid>/pulse/native that is a socket (libpulse
//     refuses a runtime directory it does not own, but connects happily when
//     the socket is named explicitly).
static bool prepareSoundServer()
{
    if (!qgetenv("PULSE_SERVER").isEmpty())
        return true;

    const QByteArray runtimeDir = qgetenv("XDG_RUNTIME_DIR");
    const QByteArray ownDir = QByteArrayLiteral("/run/user/") + QByteArray::number(getuid());
    struct stat info {};

    const QByteArray sessionSocket = runtimeDir + QByteArrayLiteral("/pulse/native");
    if (!runtimeDir.isEmpty()
        && ::stat(sessionSocket.constData(), &info) == 0
        && S_ISSOCK(info.st_mode) && info.st_uid == getuid())
        return true;

    const QByteArray ownSocket = ownDir + QByteArrayLiteral("/pulse/native");
    if (::stat(ownSocket.constData(), &info) == 0
        && S_ISSOCK(info.st_mode) && info.st_uid == getuid()) {
        qputenv("PULSE_SERVER", QByteArrayLiteral("unix:") + ownSocket);
        return true;
    }

    if (getuid() == 0) {
        const QDir runUser(QStringLiteral("/run/user"));
        const QStringList entries =
            runUser.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &entry : entries) {
            const QByteArray path = QByteArrayLiteral("/run/user/") + entry.toLocal8Bit()
                + QByteArrayLiteral("/pulse/native");
            if (::stat(path.constData(), &info) == 0 && S_ISSOCK(info.st_mode)) {
                qputenv("PULSE_SERVER", QByteArrayLiteral("unix:") + path);
                return true;
            }
        }
    }
    return false;
}

// The VPU of the STM32MP25 is a *stateless* V4L2 decoder (Hantro, /dev/video5)
// for H.264 and VP8. This BSP has no VA-API, and Chromium does not implement the
// stateless V4L2 API, so Qt WebEngine cannot decode video in hardware here: its
// <video> elements always decode on the CPU. Playing something on the VPU is
// YtGst's job, and the browser deliberately knows nothing about yt-dlp or
// GStreamer.

int main(int argc, char *argv[])
{
    g_processStartMs = QDateTime::currentMSecsSinceEpoch();

    // The one and only page argument. It is resolved before QGuiApplication so
    // --help and --version work without a display, and so the URL is known when
    // QML is built.
    QString initialUrl;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--page")) {
            if (i + 1 < argc)
                initialUrl = normaliseUrl(QString::fromLocal8Bit(argv[++i]));
        } else if (arg.startsWith(QLatin1String("--page="))) {
            initialUrl = normaliseUrl(arg.mid(7));
        } else if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            printUsage(argv[0]);
            return 0;
        } else if (arg == QLatin1String("--version")) {
            std::printf("NanoBrowser %s\n", NANOBROWSER_VERSION);
            return 0;
        }
    }

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setApplicationName(QStringLiteral("NanoBrowser"));

    g_previousMessageHandler = qInstallMessageHandler(nanoBrowserMessageHandler);

    if (qgetenv("QT_LOGGING_RULES").isEmpty())
        qputenv("QT_LOGGING_RULES", "*.warning=false");

    if (!qEnvironmentVariableIsSet("FONTCONFIG_FILE")
        && QFile::exists(QStringLiteral("/etc/fonts/fonts.conf"))) {
        qputenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf");
        qputenv("FONTCONFIG_PATH", "/etc/fonts");
    }

    preferVulkanCapablePlatform();
    preferUtf8CharacterType();

    QGuiApplication app(argc, argv);

    pickGraphicsApi();

    // Qt WebEngine's audio goes to the sound server, so point PULSE_SERVER at a
    // socket that exists before the first page plays. Note that this Qt build
    // drops qWarning() output, which is why the result is reported with qInfo().
    g_soundServerAvailable = prepareSoundServer();
    const QByteArray pulseServer = qgetenv("PULSE_SERVER");
    qInfo("NanoBrowser: sound server: %s", !g_soundServerAvailable ? "not found"
          : pulseServer.isEmpty() ? "in XDG_RUNTIME_DIR"
          : pulseServer.constData());

    // The Chromium flags depend on which renderer Qt Quick ended up with:
    // --ignore-gpu-blocklist: the VeriSilicon GPU is not on Chromium's allow list.
    // --enable-unsafe-swiftshader: keep a software fallback so that the GPU
    //   process survives features the Vulkan/GL driver rejects.
    // --disable-features=WebGPU: the target build of Qt WebEngine has no WebGPU.
    //
    // With the OpenGL renderer Qt and Chromium share one GL context, so Chromium
    // can keep its tiles on the GPU (--enable-gpu-rasterization) and hand them to
    // Qt without a round trip through system memory (--enable-zero-copy). That is
    // what makes the two Cortex-A35 cores of the STM32MP25 usable at all.
    //
    // With the Vulkan renderer there is no such shared GL context: "full" would
    // have Chromium present through its own Vulkan path, which exports the
    // rendered image as a dma-buf file descriptor via vkGetMemoryFdKHR() -
    // something the Vivante Vulkan driver on this SoC cannot do, so Chromium
    // aborts with "Unable to extract file descriptor out of external VkImage".
    // "raster" keeps the GPU process (GPU rasterisation, working WebGL) and only
    // composites in software, which is the part that must not go through Vulkan.
    // Video in <video> elements is decoded on the CPU here either way; see the
    // note above about the stateless VPU and YtGst.
    // How far Chromium is allowed to use the GPU is decided separately from the
    // renderer Qt Quick uses:
    //   full   - GPU rasterisation and zero-copy textures; needs a GL context
    //            shared with Qt, which is what the OpenGL renderer gives.
    //   raster - GPU rasterisation, the frames come back through shared memory
    //            because Qt has no GL context to share with Chromium.
    //   off    - no GPU process at all. No WebGL and every pixel on the CPU.
    // The default follows the renderer; NANOBROWSER_CHROMIUM_GPU overrides it,
    // so the modes can be compared on a board without rebuilding.
    //
    // Under Vulkan the default is "raster", not "off": measured on the
    // STM32MP257F, scrolling a Wikipedia article gave 33.7 fps against 33.3 fps
    // in software, i.e. no regression, and WebGL 2.0 on the Vivante GPU becomes
    // available at all instead of "GpuChannelHost creation failed". The Fish
    // Tank runs at 6.2 fps that way (9.3 fps with OpenGL and zero copy, which is
    // what NANOBROWSER_RENDERER=opengl is for).
    const bool vulkanRenderer = (g_graphicsApi == QLatin1String("Vulkan"));
    QString gpuMode = QString::fromLocal8Bit(qgetenv("NANOBROWSER_CHROMIUM_GPU"));
    if (gpuMode != QLatin1String("full") && gpuMode != QLatin1String("raster")
            && gpuMode != QLatin1String("off"))
        gpuMode = vulkanRenderer ? QStringLiteral("raster") : QStringLiteral("full");

    QByteArray extraFlags = "--enable-unsafe-swiftshader --ignore-gpu-blocklist "
                            // Work that only costs time on a board with two
                            // Cortex-A35 cores: translation, cast, background
                            // network chatter and the component updater.
                            "--disable-features=WebGPU,Translate,MediaRouter,OptimizationHints "
                            "--disable-background-networking --disable-component-update "
                            "--disable-sync --no-first-run --no-pings "
                            "--disable-domain-reliability --disable-breakpad "
                            // 100 MB of on-disk HTTP cache: the second visit to a
                            // site then costs nothing but the HTML, which is what
                            // makes the browser feel instant on a slow link.
                            "--disk-cache-size=104857600 ";
    if (gpuMode == QLatin1String("full"))
        extraFlags += "--enable-gpu-rasterization --enable-zero-copy";
    else if (gpuMode == QLatin1String("raster"))
        extraFlags += "--enable-gpu-rasterization --disable-gpu-compositing";
    else
        extraFlags += "--disable-gpu";

    // Chromium switches to try out without a rebuild, e.g.
    //   NANOBROWSER_EXTRA_FLAGS="--enable-low-end-device-mode"
    // They are appended, so they win over the defaults above.
    const QByteArray experimentFlags = qgetenv("NANOBROWSER_EXTRA_FLAGS").trimmed();
    if (!experimentFlags.isEmpty())
        extraFlags += QByteArrayLiteral(" ") + experimentFlags;

    const QByteArray baseFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (!baseFlags.contains(extraFlags))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", baseFlags.isEmpty()
            ? extraFlags
            : baseFlags + " " + extraFlags);

    qInfo("NanoBrowser: Chromium GPU mode '%s', flags: %s",
          gpuMode.toUtf8().constData(), extraFlags.constData());

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QtWebEngineQuick::initialize();
#endif

    // The profile lives in qml/Main.qml (storageName "nanobrowser"), so nothing
    // is opened here: a second, unused profile pointed at the same directory
    // would only fight over the same files.
    Session session;

    QQmlApplicationEngine engine;
    // Target Qt: no QQmlEngine::addContextProperty(); context properties go
    // through the root context instead.
    QQmlContext *context = engine.rootContext();
    if (context) {
        context->setContextProperty("graphicsApi", g_graphicsApi);
        context->setContextProperty("graphicsReason", g_graphicsReason);
        context->setContextProperty("graphicsFallback", g_graphicsFallback);
        context->setContextProperty("initialUrl", initialUrl);
        context->setContextProperty("processStartMs", g_processStartMs);
        context->setContextProperty("session", &session);
        context->setContextProperty("timingEnabled",
                                    !qgetenv("NANOBROWSER_TIMING").isEmpty());
        context->setContextProperty("exitAfterSeconds",
                                    QString::fromLocal8Bit(qgetenv("NANOBROWSER_EXIT_AFTER")).toInt());
    }

    engine.loadFromModule("NanoBrowser", "Main");

    const int exitCode = app.exec();

    // Normally QML leaves through Session::closeNow() (see above); this is the
    // way out when the event loop ends some other way, e.g. no QML at all.
    std::fflush(nullptr);
    std::_Exit(exitCode);
}

// Q_OBJECT lives in this .cpp, so the generated meta-object has to be included
// (AUTOMOC, CMAKE_AUTOMOC is on in CMakeLists.txt).
#include "main.moc"
