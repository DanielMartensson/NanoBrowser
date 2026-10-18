#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QVulkanInstance>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineQuick>
#include <QUrl>
#include <QDebug>
#include <QStandardPaths>
#include <QVarLengthArray>
#include <QtGlobal>
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

static QString g_graphicsApi = QStringLiteral("OpenGL");
static QString g_graphicsReason = {};

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

static void pickGraphicsApi()
{
    const QByteArray forced = rendererFromEnv();

    if (forced == "opengl") {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
        g_graphicsApi = QStringLiteral("OpenGL");
        g_graphicsReason = QStringLiteral("forced via NANOBROWSER_RENDERER");
        qInfo() << "NanoBrowser: using OpenGL renderer (forced)";
        appendGraphicsLog();
        return;
    }

    if (forced != "vulkan" && unstableVulkanIcdPresent()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
        g_graphicsApi = QStringLiteral("OpenGL");
        g_graphicsReason = QStringLiteral("unstable Vulkan drivers detected (nouveau/hasvk)");
        qInfo() << "NanoBrowser: using OpenGL renderer (unstable Vulkan drivers detected)";
        appendGraphicsLog();
        return;
    }

    QVulkanInstance probe;
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

    if (forced != "vulkan" && !suitable) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
        g_graphicsApi = QStringLiteral("OpenGL");
        g_graphicsReason = vulkanFallbackReason;
        qInfo() << "NanoBrowser: using OpenGL renderer (" << vulkanFallbackReason << ")";
        appendGraphicsLog();
        return;
    }

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    g_graphicsApi = QStringLiteral("Vulkan");
    g_graphicsReason.clear();
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
// Playing YouTube videos with the hardware video decoder
// ---------------------------------------------------------------------------
//
// Whether a PulseAudio/PipeWire socket could be pointed at (see below). The
// hardware player builds its pipeline from this: without a sound server the
// audio branch is left out instead of letting pulsesink fail the whole
// pipeline and take the picture with it.
static bool g_soundServerAvailable = false;

// The sound server is not where libpulse looks by default on this image:
// weston.service hands the session an XDG_RUNTIME_DIR of /home/weston, while
// PipeWire keeps its socket in /run/user/<uid>/pulse/native. GStreamer inherits
// this process' environment, so it is enough to point PULSE_SERVER at a socket
// that really exists:
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
// for H.264 and VP8. This BSP has no VA-API at all, and Chromium does not
// implement the stateless V4L2 API, so Qt WebEngine can never decode video in
// hardware here - its <video> element always runs on the CPU. The way to play a
// YouTube video on the VPU is GStreamer (v4l2slh264dec), fed with the H.264
// variant of the stream.
//
// Nothing is downloaded: yt-dlp is asked for the direct stream URLs (H.264
// video and AAC audio, DASH) and GStreamer streams them from YouTube's servers.
// Video and audio are decoded inside a single pipeline, so they share the
// pipeline clock (PipeWire's GstPulseSinkClock) and stay in sync.
//
// waylandsink is used on purpose: qml6glsink renders through Qt's OpenGL
// context, which does not exist when Qt Quick composites on the Vulkan RHI. The
// video therefore gets its own Wayland surface, which Weston composites
// together with the (Vulkan-rendered) browser window.
class HardwarePlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)

public:
    explicit HardwarePlayer(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_ytDlp = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
        m_gstLaunch = QStandardPaths::findExecutable(QStringLiteral("gst-launch-1.0"));
        m_available = !m_ytDlp.isEmpty()
            && !m_gstLaunch.isEmpty()
            && QFile::exists(QStringLiteral("/dev/video5")); // the VPU's stateless H.264 decoder
    }

    bool available() const { return m_available; }
    bool playing() const { return m_play && m_play->state() != QProcess::NotRunning; }
    QString status() const { return m_status; }
    QString title() const { return m_title; }

    Q_INVOKABLE void play(const QString &pageUrl)
    {
        if (!m_available) {
            emit failed(QStringLiteral("Hardware playback unavailable: yt-dlp, gst-launch-1.0 or /dev/video5 is missing"));
            return;
        }

        stop();
        m_status = QStringLiteral("Resolving the H.264 stream (yt-dlp) ...");
        emit changed();
        qInfo("NanoBrowser: hardware player: resolving %s", pageUrl.toUtf8().constData());

        if (!m_resolve) {
            m_resolve = new QProcess(this);
            m_resolve->setProcessChannelMode(QProcess::MergedChannels);
            connect(m_resolve, &QProcess::finished, this, &HardwarePlayer::resolveFinished);
            connect(m_resolve, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
                if (!m_status.isEmpty()) {
                    m_status.clear();
                    emit changed();
                }
                emit failed(QStringLiteral("Could not run yt-dlp"));
            });
        }
        if (m_resolve->state() != QProcess::NotRunning) {
            m_resolve->kill();
            m_resolve->waitForFinished(1000);
        }
        m_resolve->start(m_ytDlp, resolveArguments(pageUrl));
    }

    Q_INVOKABLE void stop()
    {
        if (m_resolve && m_resolve->state() != QProcess::NotRunning) {
            m_resolve->kill();
            m_resolve->waitForFinished(1000);
        }
        if (m_play && m_play->state() != QProcess::NotRunning) {
            // gst-launch treats SIGTERM as a clean shutdown.
            m_play->terminate();
            if (!m_play->waitForFinished(1500))
                m_play->kill();
        }
        if (!m_status.isEmpty() || !m_title.isEmpty()) {
            m_status.clear();
            m_title.clear();
            emit changed();
        }
    }

signals:
    void changed();
    void failed(const QString &reason);

private:
    static QStringList resolveArguments(const QString &pageUrl)
    {
        // H.264 is the only codec YouTube offers that this VPU decodes (no VP9
        // and no HEVC), and AAC keeps the audio side simple. Cap at 1080p: that
        // is what the stateless decoder is specified for here.
        static const QString format = QStringLiteral(
            "bv*[vcodec^=avc1][height<=1080]+ba[ext=m4a]"
            "/bv*[vcodec^=avc1][height<=1080]+ba"
            "/b[ext=mp4][vcodec^=avc1]"
            "/b[ext=mp4]");

        // yt-dlp prints the title (-e) first and then one URL (-g) per selected
        // stream: video first, then audio.
        return { QStringLiteral("--no-warnings"), QStringLiteral("--no-playlist"),
                 QStringLiteral("--socket-timeout"), QStringLiteral("15"),
                 QStringLiteral("-e"), QStringLiteral("-g"),
                 QStringLiteral("-f"), format, pageUrl };
    }

    static QString sink()
    {
        if (!qgetenv("WAYLAND_DISPLAY").isEmpty() || !qgetenv("WAYLAND_SOCKET").isEmpty())
            return QStringLiteral("waylandsink");
        return QStringLiteral("autovideosink");
    }

    static QString gstreamerLogPath()
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!QDir(dir).exists())
            QDir().mkpath(dir);
        return dir + QStringLiteral("/gstreamer.log");
    }

    void resolveFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        const QString output = QString::fromUtf8(m_resolve->readAll()).trimmed();
        if (exitStatus != QProcess::NormalExit || exitCode != 0 || output.isEmpty()) {
            qInfo("NanoBrowser: hardware player: yt-dlp failed (exit status %d, code %d, "
                  "%d bytes of output)", static_cast<int>(exitStatus), exitCode, output.size());
            if (!output.isEmpty())
                qInfo("NanoBrowser: yt-dlp said: %s", output.toUtf8().constData());
            m_status.clear();
            emit changed();
            emit failed(QStringLiteral("yt-dlp could not resolve this page"));
            return;
        }

        QStringList urls;
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &raw : lines) {
            const QString line = raw.trimmed();
            if (line.startsWith(QLatin1String("http")))
                urls.append(line);
            else if (m_title.isEmpty())
                m_title = line;
        }
        if (urls.isEmpty()) {
            qInfo("NanoBrowser: hardware player: no stream URL in yt-dlp's answer: %s",
                  output.toUtf8().constData());
            m_status.clear();
            emit changed();
            emit failed(QStringLiteral("No playable H.264 stream found"));
            return;
        }
        qInfo("NanoBrowser: hardware player: resolved \"%s\", %d stream URL(s)",
              m_title.toUtf8().constData(), int(urls.size()));
        emit changed();
        startPipeline(urls.value(0), urls.value(1));
    }

    void startPipeline(const QString &videoUrl, const QString &audioUrl)
    {
        QStringList args{ QStringLiteral("-q") };
        // Without a reachable sound server, pulsesink fails and takes the rest
        // of the pipeline (the picture) down with it, so leave the audio out.
        const QString muxedAudio = g_soundServerAvailable
            ? QStringLiteral(" d.audio_0 ! queue ! aacparse ! avdec_aac "
                             "! audioconvert ! audioresample ! pulsesink")
            : QString();

        if (audioUrl.isEmpty()) {
            // Muxed stream: qtdemux splits video and audio dynamically.
            args << QStringLiteral("souphttpsrc location=\"%1\" ! qtdemux name=d "
                                   "d.video_0 ! queue ! h264parse ! v4l2slh264dec ! queue ! %2%3")
                        .arg(videoUrl, sink(), muxedAudio);
        } else {
            args << QStringLiteral("souphttpsrc location=\"%1\" ! qtdemux ! h264parse ! v4l2slh264dec ! queue ! %2")
                        .arg(videoUrl, sink());
            if (g_soundServerAvailable)
                args << QStringLiteral("souphttpsrc location=\"%1\" ! qtdemux ! queue ! aacparse ! avdec_aac "
                                       "! audioconvert ! audioresample ! pulsesink")
                            .arg(audioUrl);
        }

        if (!m_play) {
            m_play = new QProcess(this);
            m_play->setProcessChannelMode(QProcess::MergedChannels);
            m_play->setStandardOutputFile(gstreamerLogPath());
            connect(m_play, &QProcess::finished, this, &HardwarePlayer::playbackFinished);
        }
        if (m_play->state() != QProcess::NotRunning) {
            m_play->kill();
            m_play->waitForFinished(1000);
        }
        m_play->start(m_gstLaunch, args);
        qInfo("NanoBrowser: hardware player: gst-launch-1.0 started (%s), log: %s",
              g_soundServerAvailable ? "video + audio" : "video only, no sound server",
              gstreamerLogPath().toUtf8().constData());
        m_status = g_soundServerAvailable
            ? QStringLiteral("Hardware decoding: v4l2slh264dec (VPU) + AAC audio")
            : QStringLiteral("Hardware decoding: v4l2slh264dec (VPU), video only - no sound server");
        emit changed();
    }

    void playbackFinished()
    {
        m_status.clear();
        m_title.clear();
        emit changed();
    }

    QString m_ytDlp;
    QString m_gstLaunch;
    bool m_available = false;
    QString m_status;
    QString m_title;
    QProcess *m_resolve = nullptr;
    QProcess *m_play = nullptr;
};

int main(int argc, char *argv[])
{
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

    QGuiApplication app(argc, argv);

    pickGraphicsApi();

    // The hardware player hands this process' environment to gst-launch-1.0, so
    // the sound server has to be found before the first pipeline starts. Note
    // that this Qt build drops qWarning() output, which is why the result is
    // reported with qInfo() and shown in the player's status line instead.
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
    // Video is decoded by the VPU in either case, see HardwarePlayer.
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
                            "--disable-features=WebGPU ";
    if (gpuMode == QLatin1String("full"))
        extraFlags += "--enable-gpu-rasterization --enable-zero-copy";
    else if (gpuMode == QLatin1String("raster"))
        extraFlags += "--enable-gpu-rasterization --disable-gpu-compositing";
    else
        extraFlags += "--disable-gpu";
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

    QWebEngineProfile *profile = new QWebEngineProfile(QStringLiteral("nanobrowser"), &app);
    profile->setPersistentStoragePath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/QtWebEngine/nanobrowser"));
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    // Declared before the engine so that it outlives it (enables hardware
    // playback of YouTube videos from QML).
    HardwarePlayer hardwarePlayer;

    QQmlApplicationEngine engine;
    // Target Qt: no QQmlEngine::addContextProperty(); context properties go
    // through the root context instead.
    QQmlContext *context = engine.rootContext();
    if (context) {
        context->setContextProperty("graphicsApi", g_graphicsApi);
        context->setContextProperty("graphicsReason", g_graphicsReason);
        context->setContextProperty("hardwarePlayer", &hardwarePlayer);
    }

    engine.loadFromModule("NanoBrowser", "Main");

    return app.exec();
}

// Q_OBJECT lives in this .cpp, so the generated meta-object has to be included
// (AUTOMOC, CMAKE_AUTOMOC is on in CMakeLists.txt).
#include "main.moc"
