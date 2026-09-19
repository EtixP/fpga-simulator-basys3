#include "qt/BoardAdapter.h"

#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <cstdlib>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("VirtualBasys"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualBasys Qt frontend"));
    parser.addHelpOption();
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Exit after the first rendered frame; fail after 10 seconds."));
    parser.addOption(smokeOption);
    parser.process(app);
    const bool smokeTest = parser.isSet(smokeOption);

    // The application owns the adapter; QML is destroyed before it. No design
    // is loaded yet. A future composition root must keep BoardModel alive longer.
    vb::qt::BoardAdapter boardAdapter;
    QQmlEngine::setObjectOwnership(&boardAdapter, QQmlEngine::CppOwnership);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("board"),
                                 QVariant::fromValue(&boardAdapter)}});
    engine.loadFromModule("VirtualBasys", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Failed to load the VirtualBasys QML module.";
        return EXIT_FAILURE;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (!window) {
        qCritical() << "The VirtualBasys QML root must be a window.";
        return EXIT_FAILURE;
    }

    QObject::connect(window, &QQuickWindow::sceneGraphError, &app,
                     [&app](QQuickWindow::SceneGraphError, const QString& message) {
                         qCritical() << "Qt Quick rendering failed:" << message;
                         app.exit(EXIT_FAILURE);
                     }, Qt::QueuedConnection);

    bool renderedFrame = false;
    QTimer deadline;
    if (smokeTest) {
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &app, [&app] {
            qCritical() << "Qt Quick smoke test timed out before rendering a frame.";
            app.exit(EXIT_FAILURE);
        });
        // Queue onto the app thread; disconnect before captured locals are destroyed.
        QObject::connect(window, &QQuickWindow::frameSwapped, &deadline, [&] {
            renderedFrame = true;
            deadline.stop();
            std::puts("Qt Quick smoke test: PASS");
            app.exit(EXIT_SUCCESS);
        }, Qt::QueuedConnection);
        deadline.start(10'000);
    }

    const int result = app.exec();
    // Closing the window before a frame is produced must not pass the smoke test.
    return smokeTest && !renderedFrame ? EXIT_FAILURE : result;
}
