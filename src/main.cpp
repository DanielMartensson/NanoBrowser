#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QVulkanInstance>
#include <QtWebEngineQuick>
#include <QUrl>
#include <QDebug>

static QByteArray rendererFromEnv()
{
    return qgetenv("NANOBROWSER_RENDERER").toLower();
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
    if (forced != "vulkan" && !probe.create()) {
        qInfo() << "NanoBrowser: using OpenGL renderer (Vulkan not available)";
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
    pickGraphicsApi();
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);

    pickGraphicsApi();
    QtWebEngineQuick::initialize();
#endif

    QQmlApplicationEngine engine;

    engine.loadFromModule("NanoBrowser", "Main");

    return app.exec();
}