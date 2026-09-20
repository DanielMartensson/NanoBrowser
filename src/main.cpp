#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QVulkanInstance>
#include <QtWebEngineQuick>
#include <QUrl>
#include <QDebug>
#include <QVarLengthArray>
#include <vulkan/vulkan.h>

static QByteArray rendererFromEnv()
{
    return qgetenv("NANOBROWSER_RENDERER").toLower();
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

static void pickGraphicsApi()
{
    const QByteArray forced = rendererFromEnv();
    if (forced == "opengl") {
        qInfo() << "NanoBrowser: using OpenGL renderer (forced)";
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
        return;
    }

    QVulkanInstance probe;
    const bool created = probe.create();
    const bool suitable = created && vulkanHardwareAvailable(probe);
    if (forced != "vulkan" && !suitable) {
        qInfo() << "NanoBrowser: using OpenGL renderer (Vulkan not available or unsuitable)";
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
        return;
    }

    qInfo() << "NanoBrowser: using Vulkan renderer";
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setApplicationName(QStringLiteral("NanoBrowser"));

    const QByteArray baseFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    const QByteArray extraFlags = "--enable-unsafe-swiftshader --ignore-gpu-blocklist";
    if (!baseFlags.contains(extraFlags))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", baseFlags.isEmpty()
            ? extraFlags
            : baseFlags + " " + extraFlags);

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);

    pickGraphicsApi();
#else
    QGuiApplication app(argc, argv);

    pickGraphicsApi();
    QtWebEngineQuick::initialize();
#endif

    QQmlApplicationEngine engine;

    engine.loadFromModule("NanoBrowser", "Main");

    return app.exec();
}