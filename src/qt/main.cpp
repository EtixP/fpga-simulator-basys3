#include VB_MODEL_HEADER
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/SimulationController.h"
#include "script/RunOptions.h"

#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSocketNotifier>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
// Scripted-run options, parsed by the shared launcher CLI (script/RunArgs.cpp)
// with the same syntax and messages as every frontend. They leave the argument
// list in their original order, which decides the order of same-cycle inputs.
constexpr std::array<const char*, 7> kRunOptions{
    "--xdc", "--frames", "--screenshot", "--log", "--at", "--switches", "--send"};

struct SplitArguments {
    std::vector<std::string> run;  // argv[0] and the run options, in order
    QStringList frontend;          // argv[0] and everything else
    bool scripted = false;         // any run option other than --xdc
    bool xdc = false;              // --xdc given, even with an empty path
};

SplitArguments splitArguments(const QStringList& arguments) {
    SplitArguments split;
    split.run.push_back(arguments.value(0).toStdString());
    split.frontend << arguments.value(0);
    const auto isRunOption = [](const QString& name) {
        return std::find(kRunOptions.begin(), kRunOptions.end(), name.toStdString())
            != kRunOptions.end();
    };
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString& argument = arguments[i];
        // Run options take their value as the next argument only. An inline
        // "--frames=1" goes to parseRunArgs whole, which rejects it as the
        // legacy demos did, instead of Qt's parser accepting and ignoring it.
        if (const auto equals = argument.indexOf(QLatin1Char('='));
            equals > 0 && isRunOption(argument.left(equals))) {
            split.scripted = true;
            split.run.push_back(argument.toStdString());
            continue;
        }
        if (!isRunOption(argument)) {
            if (argument.startsWith(QLatin1Char('-')) && argument != QLatin1String("--"))
                split.frontend << argument;
            else
                split.run.push_back(argument.toStdString());  // rejected in order
            continue;
        }
        split.scripted = split.scripted || argument != QLatin1String("--xdc");
        split.xdc = split.xdc || argument == QLatin1String("--xdc");
        split.run.push_back(argument.toStdString());
        // A missing value is reported by parseRunArgs, like any incomplete option.
        if (i + 1 < arguments.size()) split.run.push_back(arguments[++i].toStdString());
    }
    return split;
}

std::string readConstraints(const std::string& path, bool builtIn) {
    if (builtIn) {
        QFile builtIn(QStringLiteral(":/examples/" VB_DESIGN ".xdc"));
        return builtIn.open(QIODevice::ReadOnly) ? builtIn.readAll().toStdString() : std::string();
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// The image format follows the file name; names without a known image suffix
// get BMP, as the legacy demos always wrote.
bool saveScreenshot(const QImage& image, const QString& path) {
    const QByteArray suffix = QFileInfo(path).suffix().toLower().toLatin1();
    const bool known = !suffix.isEmpty() && QImageWriter::supportedImageFormats().contains(suffix);
    if (!known && !suffix.isEmpty())
        std::fprintf(stderr, "warning: Qt cannot write '.%s' images; the screenshot is BMP\n",
                     suffix.constData());
    return !image.isNull() && image.save(path, known ? nullptr : "BMP");
}

#ifdef Q_OS_UNIX
// Ctrl-C and SIGTERM end the app normally, so a scripted run still writes its
// --log, as the legacy demos did through SDL's quit event. The handler only
// writes to a pipe; the event loop quits.
int signalPipe[2] = {-1, -1};
void quitOnSignal(int) {
    const int savedErrno = errno;
    const char byte = 1;
    [[maybe_unused]] const auto written = ::write(signalPipe[1], &byte, 1);
    errno = savedErrno;
}
#endif
}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("VirtualBasys"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

#ifdef Q_OS_UNIX
    // Installed first, so a signal during startup also quits normally once the
    // event loop runs. A second Ctrl-C terminates at once.
    std::unique_ptr<QSocketNotifier> signalNotifier;
    if (::pipe(signalPipe) == 0) {
        signalNotifier = std::make_unique<QSocketNotifier>(signalPipe[0], QSocketNotifier::Read);
        QObject::connect(signalNotifier.get(), &QSocketNotifier::activated, &app, [&app] {
            char byte = 0;
            [[maybe_unused]] const auto consumed = ::read(signalPipe[0], &byte, 1);
            app.quit();
        });
        struct sigaction action {};
        action.sa_handler = quitOnSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART | SA_RESETHAND;
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
    }
#endif

    const SplitArguments arguments = splitArguments(QCoreApplication::arguments());
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "VirtualBasys Qt frontend. Scripted runs (--frames, --at, --switches, --send, --log, "
        "--screenshot) start with a 16-cycle reset and run by themselves; inputs and sends "
        "apply at exact virtual cycles."));
    parser.addHelpOption();
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Exit after the first rendered frame; fail after 10 seconds."));
    parser.addOption(smokeOption);
    const QCommandLineOption previewOption(QStringLiteral("preview"),
        QStringLiteral("Show the disabled board preview without loading the built-in example."));
    const QCommandLineOption realtimeOption(QStringLiteral("realtime"),
        QStringLiteral("Select best-effort 1x pacing (starts paused unless scripted)."));
    parser.addOption(previewOption);
    parser.addOption(realtimeOption);
    // Listed for --help; the values are parsed from the split list above.
    parser.addOptions({
        {QStringLiteral("xdc"), QStringLiteral("Use these constraints instead of the built-in ones."),
         QStringLiteral("path")},
        {QStringLiteral("frames"),
         QStringLiteral("End the run after N frames of %1 cycles, after the startup reset, then "
                        "write --screenshot and --log and exit.").arg(VB_CYCLES_PER_FRAME),
         QStringLiteral("N")},
        {QStringLiteral("screenshot"),
         QStringLiteral("Save the window when --frames ends the run (PNG, BMP, ... by suffix)."),
         QStringLiteral("file")},
        {QStringLiteral("log"), QStringLiteral("Write the structured log when the app exits."),
         QStringLiteral("file")},
        {QStringLiteral("at"), QStringLiteral("Set an input at a cycle, e.g. 2000000:BTNU=1 "
                                              "(SW0..SW15, BTNC/BTNU/BTNL/BTNR/BTND). Repeatable."),
         QStringLiteral("cycle:name=v")},
        {QStringLiteral("switches"),
         QStringLiteral("Switches on at cycle 0; the rightmost digit is SW0, e.g. 0101."),
         QStringLiteral("bits")},
        {QStringLiteral("send"), QStringLiteral("Send text to the UART at a cycle. Repeatable."),
         QStringLiteral("cycle:text")},
    });
    parser.process(arguments.frontend);
    const bool smokeTest = parser.isSet(smokeOption);

    std::vector<char*> runArgv;
    for (const auto& argument : arguments.run) runArgv.push_back(const_cast<char*>(argument.c_str()));
    vb::RunArgs run = vb::parseRunArgs(static_cast<int>(runArgv.size()), runArgv.data(), {});
    // Stray words (say, unquoted --send text) were rejected in order above;
    // anything Qt's parser still takes as positional ("-") is an error too.
    for (const QString& stray : parser.positionalArguments())
        run.errors.push_back("unknown or incomplete argument '" + stray.toStdString() + "'");
    if (parser.isSet(previewOption) && arguments.run.size() > 1)
        run.errors.push_back("--preview shows no design and cannot be combined with --xdc or a scripted run");
    if (smokeTest && arguments.scripted)
        run.errors.push_back("--smoke-test cannot be combined with a scripted run");
    for (const auto& error : run.errors) std::fprintf(stderr, "error: %s\n", error.c_str());
    if (!run.errors.empty()) return EXIT_FAILURE;
    run.run.cyclesPerFrame = VB_CYCLES_PER_FRAME;
    if (!run.run.screenshotPath.empty() && run.run.maxFrames < 0)
        std::fprintf(stderr, "warning: --screenshot is written only when --frames ends the run\n");

    // Each launcher links one Verilated design. The resource-backed constraints
    // work from any directory; no source-tree path is needed at runtime.
    std::unique_ptr<vb::SimEngine> simulator;
    std::unique_ptr<vb::BoardModel> board;
    if (!parser.isSet(previewOption)) {
        const std::string xdcText = readConstraints(run.xdcPath, !arguments.xdc);
        if (xdcText.empty()) {
            if (!arguments.xdc)
                qCritical() << "Cannot open the built-in example constraints.";
            else
                std::fprintf(stderr, "error: cannot read XDC '%s'\n", run.xdcPath.c_str());
            return EXIT_FAILURE;
        }
        try {
            simulator = vb::makeVerilatorEngine<VB_MODEL>({.topModule = VB_DESIGN});
            const auto xdc = vb::parseXdc(xdcText);
            for (const auto& warning : xdc.warnings) std::fprintf(stderr, "xdc %s\n", warning.c_str());
            auto binding = vb::PinBinding::bind(xdc, *simulator);
            for (const auto& diagnostic : binding.diagnostics())
                std::fprintf(stderr, "bind %s\n", diagnostic.c_str());
            board = std::make_unique<vb::BoardModel>(*simulator, std::move(binding));
        } catch (const std::exception& error) {
            qCritical() << "Cannot load the example:" << error.what();
            return EXIT_FAILURE;
        }
    }
    // Destruction reverses this order: QML -> controller -> adapter -> board ->
    // engine. Loading/rendering starts paused and never clocks RTL; only a
    // launcher built with VB_STARTUP_RESET advances its 16 reset cycles first.
    // A scripted run instead starts with the shared scripted startup and runs.
    vb::qt::BoardAdapter boardAdapter(board.get());
    // --log keeps the whole structured log for the file; the Logs view then
    // reads it without clearing it.
    if (!run.run.logPath.empty()) boardAdapter.setBoardLogRetained(true);
    vb::qt::SimulationController controller(boardAdapter, QStringLiteral(VB_DESIGN_TITLE));
    if (arguments.scripted) {
        if (!controller.startScript(run.run)) {
            std::fprintf(stderr, "error: cannot start the scripted run: %s\n",
                         qPrintable(controller.errorString()));
            return EXIT_FAILURE;
        }
    }
#if VB_STARTUP_RESET
    // Some examples need their btnC reset (R6) before their outputs mean
    // anything: uart_echo's receiver synchronizer powers up low and decodes a
    // false start bit; vga_pattern's syncs power up asserted, giving the monitor
    // a false first edge. Apply the Reset control's 16-cycle pulse once, as the
    // legacy demos do at startup. A scripted run's startup already did.
    else if (board && !controller.reset()) {
        qCritical() << "Cannot apply the startup reset:" << controller.errorString();
        return EXIT_FAILURE;
    }
#endif
    controller.setRealtime(parser.isSet(realtimeOption));
    QQmlEngine::setObjectOwnership(&boardAdapter, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("board"),
                                 QVariant::fromValue(&boardAdapter)},
                                {QStringLiteral("controller"),
                                 QVariant::fromValue(&controller)},
                                {QStringLiteral("designSource"),
                                 QStringLiteral(VB_DESIGN)}});
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

    // A finite scripted run ends at its last cycle: save the window as it then
    // appears, and exit. The log is written below for every scripted run.
    bool artifactFailure = false;
    const auto finishRun = [&] {
        // --frames 0 is a launch check: like the legacy demos, it renders and
        // saves nothing.
        if (!run.run.screenshotPath.empty() && run.run.maxFrames > 0
            && !saveScreenshot(window->grabWindow(), QString::fromStdString(run.run.screenshotPath))) {
            std::fprintf(stderr, "error: could not write screenshot '%s'\n",
                         run.run.screenshotPath.c_str());
            artifactFailure = true;
        }
        app.exit(EXIT_SUCCESS);
    };
    if (controller.scripted()) {
        QObject::connect(&controller, &vb::qt::SimulationController::runFinished, &app, finishRun,
                         Qt::QueuedConnection);
        if (controller.finished())
            QTimer::singleShot(0, &app, finishRun);  // --frames 0: nothing to run
        else
            controller.run();
    }

    const int result = app.exec();

    if (controller.scripted()) {
        if (!run.run.screenshotPath.empty() && run.run.maxFrames > 0 && !controller.finished())
            std::fprintf(stderr, "warning: the run ended early; no screenshot was saved\n");
        const auto [inputs, sends] = controller.unappliedScriptEvents();
        const char* when = controller.finished() ? "beyond the run horizon were never applied"
                                                 : "were not reached before the window closed";
        if (inputs) std::fprintf(stderr, "warning: %zu scripted --at event(s) %s\n", inputs, when);
        if (sends) std::fprintf(stderr, "warning: %zu scripted --send event(s) %s\n", sends, when);
    }
    if (board && !run.run.logPath.empty()) {
        std::ofstream out(run.run.logPath, std::ios::binary);
        for (const auto& line : board->structuredLog()) out << line << '\n';
        out.flush();
        if (!out.good()) {
            std::fprintf(stderr, "error: could not write log '%s'\n", run.run.logPath.c_str());
            artifactFailure = true;
        }
    }
    // Closing the window before a frame is produced must not pass the smoke test.
    if (smokeTest && !renderedFrame) return EXIT_FAILURE;
    return artifactFailure ? EXIT_FAILURE : result;
}
