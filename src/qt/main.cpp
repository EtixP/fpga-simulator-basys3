#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

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

    // The engine owns the QML objects and is destroyed before the application.
    // M0 has no simulator objects; future adapters must go through BoardModel.
    QQmlApplicationEngine engine;
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
