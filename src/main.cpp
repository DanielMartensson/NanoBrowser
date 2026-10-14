#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
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
    // QDir::absolutePath() gives the directory part (leftOfLastPathSeparator
    // is not available in the target Qt build).
    const QString dir = QDir(path).absolutePath();
    if (!dir.isEmpty() && !QDir(dir).exists())
        QDir(dir).mkdir(dir);

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

    const QByteArray baseFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    const QByteArray extraFlags = "--enable-unsafe-swiftshader --ignore-gpu-blocklist --disable-features=WebGPU";
    if (!baseFlags.contains(extraFlags))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", baseFlags.isEmpty()
            ? extraFlags
            : baseFlags + " " + extraFlags);

    QGuiApplication app(argc, argv);

    pickGraphicsApi();

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QtWebEngineQuick::initialize();
#endif

    QWebEngineProfile *profile = new QWebEngineProfile(QStringLiteral("nanobrowser"), &app);
    profile->setPersistentStoragePath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/QtWebEngine/nanobrowser"));
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    QQmlApplicationEngine engine;
    // Target Qt: no QQmlEngine::addContextProperty(); context properties go
    // through the root context instead.
    QQmlContext *context = engine.rootContext();
    if (context) {
        context->setContextProperty("graphicsApi", g_graphicsApi);
        context->setContextProperty("graphicsReason", g_graphicsReason);
    }

    engine.loadFromModule("NanoBrowser", "Main");

    return app.exec();
}
