#include VB_MODEL_HEADER
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/SimulationController.h"

#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <cstdlib>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("VirtualBasys"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VirtualBasys Qt frontend"));
    parser.addHelpOption();
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Exit after the first rendered frame; fail after 10 seconds."));
    parser.addOption(smokeOption);
    const QCommandLineOption previewOption(QStringLiteral("preview"),
        QStringLiteral("Show the disabled board preview without loading the built-in example."));
    const QCommandLineOption realtimeOption(QStringLiteral("realtime"),
        QStringLiteral("Select best-effort 1x pacing (starts paused)."));
    parser.addOption(previewOption);
    parser.addOption(realtimeOption);
    parser.process(app);
    const bool smokeTest = parser.isSet(smokeOption);

    // Each launcher links one Verilated design. The resource-backed constraints
    // work from any directory; no source-tree path is needed at runtime.
    std::unique_ptr<vb::SimEngine> simulator;
    std::unique_ptr<vb::BoardModel> board;
    if (!parser.isSet(previewOption)) {
        QFile constraints(QStringLiteral(":/examples/" VB_DESIGN ".xdc"));
        if (!constraints.open(QIODevice::ReadOnly)) {
            qCritical() << "Cannot open the built-in example constraints:" << constraints.errorString();
            return EXIT_FAILURE;
        }
        try {
            simulator = vb::makeVerilatorEngine<VB_MODEL>({.topModule = VB_DESIGN});
            const auto xdc = vb::parseXdc(constraints.readAll().toStdString());
            board = std::make_unique<vb::BoardModel>(
                *simulator, vb::PinBinding::bind(xdc, *simulator));
        } catch (const std::exception& error) {
            qCritical() << "Cannot load the built-in example:" << error.what();
            return EXIT_FAILURE;
        }
    }
    // Destruction reverses this order: QML -> controller -> adapter -> board ->
    // engine. Loading/rendering starts paused at cycle zero and never clocks RTL.
    vb::qt::BoardAdapter boardAdapter(board.get());
    vb::qt::SimulationController controller(boardAdapter, QStringLiteral(VB_DESIGN_TITLE));
    controller.setRealtime(parser.isSet(realtimeOption));
    QQmlEngine::setObjectOwnership(&boardAdapter, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("board"),
                                 QVariant::fromValue(&boardAdapter)},
                                {QStringLiteral("controller"),
                                 QVariant::fromValue(&controller)}});
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
