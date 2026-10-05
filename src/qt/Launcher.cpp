#include "qt/Launcher.h"

#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "qt/SimulationController.h"
#include "script/RunOptions.h"

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
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

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace vb::qt {
namespace {
// Scripted-run options, parsed by the shared launcher CLI (script/RunArgs.cpp),
// which also defines their messages. They leave the argument list in their
// original order, which decides the order of same-cycle inputs.
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
        // "--frames=1" goes to parseRunArgs whole, which rejects it, instead
        // of Qt's parser accepting and ignoring it.
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

// The image format follows the file name; names without a suffix Qt can write
// get BMP.
bool saveScreenshot(const QImage& image, const QString& path) {
    const QByteArray suffix = QFileInfo(path).suffix().toLower().toLatin1();
    const bool known = !suffix.isEmpty() && QImageWriter::supportedImageFormats().contains(suffix);
    if (!known && !suffix.isEmpty())
        std::fprintf(stderr, "warning: Qt cannot write '.%s' images; the screenshot is BMP\n",
                     suffix.constData());
    return !image.isNull() && image.save(path, known ? nullptr : "BMP");
}

#ifdef Q_OS_UNIX
// Ctrl-C, SIGTERM and SIGHUP end the app normally, so a scripted run still
// writes its --log. The handler only writes to a pipe; the event loop quits.
int signalPipe[2] = {-1, -1};
void (*signalHook)() = nullptr;
void quitOnSignal(int) {
    const int savedErrno = errno;
    if (signalHook) signalHook();
    const char byte = 1;
    [[maybe_unused]] const auto written = ::write(signalPipe[1], &byte, 1);
    errno = savedErrno;
}
#endif
}  // namespace

void quitOnSignals(QGuiApplication& app, void (*onSignal)()) {
#ifdef Q_OS_UNIX
    signalHook = onSignal;
    // Installed first, so a signal during startup also quits normally once the
    // event loop runs. A second Ctrl-C terminates at once. The notifier lives
    // as long as the application.
    if (::pipe(signalPipe) != 0) return;
    auto* notifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [&app] {
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
    sigaction(SIGHUP, &action, nullptr);
#else
    (void)app;
    (void)onSignal;
#endif
}

std::string readConstraints(const std::string& path, const QString& builtIn) {
    if (!builtIn.isEmpty()) {
        QFile resource(builtIn);
        return resource.open(QIODevice::ReadOnly) ? resource.readAll().toStdString() : std::string();
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string clockPort(const XdcDoc& xdc) {
    for (const auto& pin : xdc.pins)
        if (pin.packagePin && *pin.packagePin == "W5" && !pin.port.bit) return pin.port.port;
    return "clk";
}

LauncherDesign runnerDesign() {
    LauncherDesign design;
    design.builtInXdc = QStringLiteral(":/constraints/Basys3_Master.xdc");
    design.boardDefaultXdc = true;
    design.startupReset = LauncherDesign::StartupReset::IfBtncBound;
    design.description = QStringLiteral(
        "VirtualBasys: runs a Verilog or SystemVerilog design on the virtual Basys 3. Its sources "
        "are verilated and compiled on the first run, then cached. Without --xdc the design uses the "
        "Basys 3 master constraints, so its ports need the master file's names (clk, sw, led, btnC, "
        "seg, an, dp, RsRx, RsTx, vgaRed...). A design that binds btnC gets one Reset pulse at startup.");
    design.helpArgument = LauncherDesign::HelpArgument{
        QStringLiteral("run"),
        QStringLiteral("Run a design: its source files (.v: Verilog-2005, .sv: SystemVerilog), "
                       "straight after run, then --top NAME (the top module, when there are several), "
                       "-I DIR (an `include directory; repeatable) and the options above."),
        QStringLiteral("run SOURCE.v [SOURCE.v ...] [--top NAME] [-I DIR]")};
    return design;
}

QStringList designFileList(const std::vector<std::string>& files, const QString& xdc) {
    const QDir current = QDir::current();
    const QString home = QDir::homePath();
    QStringList lines;
    for (const auto& file : files) {
        const QString path = QString::fromStdString(file);
        const QString relative = current.relativeFilePath(path);
        if (!relative.startsWith(QLatin1String("..")) && !QDir::isAbsolutePath(relative))
            lines << relative;
        else if (path.startsWith(home + QLatin1Char('/')))
            lines << QLatin1String("~") + path.mid(home.size());
        else
            lines << path;
    }
    lines << (xdc.isEmpty() ? QStringLiteral("Basys 3 master constraints (built in)") : xdc);
    return lines;
}

namespace {
struct LaunchOptions {
    SplitArguments arguments;
    RunArgs run;
    bool smokeTest = false;
    bool preview = false;
    bool realtime = false;
};

// Parses and checks the launcher options, printing every error. `exiting`:
// --help and Qt's own option errors end the process (QCommandLineParser::
// process); otherwise they are reported the same way and nullopt returned.
// `designProvided`: whether a design will be there to run.
std::optional<LaunchOptions> parseLaunch(const QStringList& commandLine, const LauncherDesign& design,
                                         bool exiting, bool designProvided) {
    LaunchOptions launch;
    launch.arguments = splitArguments(commandLine);
    QCommandLineParser parser;
    parser.setApplicationDescription(design.description);
    parser.addHelpOption();
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Exit after the first rendered frame; fail after 10 seconds."));
    parser.addOption(smokeOption);
    if (design.helpArgument)
        parser.addPositionalArgument(design.helpArgument->name, design.helpArgument->description,
                                     design.helpArgument->syntax);
    const QCommandLineOption previewOption(QStringLiteral("preview"),
        QStringLiteral("Show the disabled board preview without loading a design."));
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
                        "write --screenshot and --log and exit.").arg(design.cyclesPerFrame),
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
    if (exiting) {
        parser.process(launch.arguments.frontend);
    } else if (!parser.parse(launch.arguments.frontend)) {
        std::fprintf(stderr, "%s: %s\n", qPrintable(QCoreApplication::applicationName()),
                     qPrintable(parser.errorText()));
        return std::nullopt;
    }
    launch.smokeTest = parser.isSet(smokeOption);
    launch.preview = parser.isSet(previewOption);
    launch.realtime = parser.isSet(realtimeOption);

    const auto& arguments = launch.arguments;
    std::vector<char*> runArgv;
    for (const auto& argument : arguments.run) runArgv.push_back(const_cast<char*>(argument.c_str()));
    launch.run = parseRunArgs(static_cast<int>(runArgv.size()), runArgv.data(), {});
    auto& errors = launch.run.errors;
    // Stray words (say, unquoted --send text) were rejected in order above;
    // anything Qt's parser still takes as positional ("-") is an error too.
    for (const QString& stray : parser.positionalArguments())
        errors.push_back("unknown or incomplete argument '" + stray.toStdString() + "'");
    if (launch.preview && arguments.run.size() > 1)
        errors.push_back("--preview shows no design and cannot be combined with --xdc or a scripted run");
    if (launch.smokeTest && arguments.scripted)
        errors.push_back("--smoke-test cannot be combined with a scripted run");
    if (!launch.preview && !designProvided) errors.push_back("no design to run");
    for (const auto& error : errors) std::fprintf(stderr, "error: %s\n", error.c_str());
    if (!errors.empty()) return std::nullopt;
    launch.run.run.cyclesPerFrame = design.cyclesPerFrame;
    return launch;
}
}  // namespace

bool checkLauncherArguments(const QStringList& arguments, const LauncherDesign& design) {
    return parseLaunch(arguments, design, false, true).has_value();
}

int runLauncher(QGuiApplication& app, const QStringList& commandLine, const LauncherDesign& design) {
    auto parsed = parseLaunch(commandLine, design, true, static_cast<bool>(design.createEngine));
    if (!parsed) return EXIT_FAILURE;
    const SplitArguments& arguments = parsed->arguments;
    RunArgs& run = parsed->run;
    const bool smokeTest = parsed->smokeTest;
    if (!run.run.screenshotPath.empty() && run.run.maxFrames < 0)
        std::fprintf(stderr, "warning: --screenshot is written only when --frames ends the run\n");

    // One Verilator runtime per process: a built-in example links its design,
    // a loaded design brings it in its module. Built-in constraints are Qt
    // resources, so no source-tree path is needed at run time.
    std::unique_ptr<SimEngine> simulator;
    std::unique_ptr<BoardModel> board;
    if (!parsed->preview) {
        const bool builtInXdc = !arguments.xdc;
        const std::string xdcText = readConstraints(run.xdcPath, builtInXdc ? design.builtInXdc : QString());
        if (xdcText.empty()) {
            if (builtInXdc)
                qCritical() << "Cannot open the built-in constraints" << design.builtInXdc;
            else
                std::fprintf(stderr, "error: cannot read XDC '%s'\n", run.xdcPath.c_str());
            return EXIT_FAILURE;
        }
        try {
            const auto xdc = parseXdc(xdcText);
            for (const auto& warning : xdc.warnings) std::fprintf(stderr, "xdc %s\n", warning.c_str());
            std::string error;
            simulator = design.createEngine(clockPort(xdc), error);
            if (!simulator) {
                std::fprintf(stderr, "error: %s\n", error.c_str());
                return EXIT_FAILURE;
            }
            // The full-board default constrains every resource; ports a small
            // design lacks are expected notes there, not worth printing.
            const bool boardDefault = builtInXdc && design.boardDefaultXdc;
            auto binding = PinBinding::bind(xdc, *simulator, {.builtinDefault = boardDefault});
            bool unboundPort = false;
            for (const auto& diagnostic : binding.diagnostics()) {
                if (boardDefault && diagnostic.rfind("note: ", 0) == 0) continue;
                std::fprintf(stderr, "bind %s\n", diagnostic.c_str());
                unboundPort = unboundPort || diagnostic.rfind("warning: design port '", 0) == 0;
            }
            if (boardDefault && unboundPort)
                std::fprintf(stderr, "hint: without --xdc, ports need the Basys 3 master constraints' "
                                     "names: clk, sw, led, btnC, btnU, btnL, btnR, btnD, seg, dp, an, "
                                     "RsRx, RsTx, vgaRed, vgaGreen, vgaBlue, Hsync, Vsync, JA, JB, JC, "
                                     "JXADC\n");
            board = std::make_unique<BoardModel>(*simulator, std::move(binding));
        } catch (const std::exception& error) {
            qCritical() << "Cannot load the design:" << error.what();
            return EXIT_FAILURE;
        }
    }
    // Destruction reverses this order: QML -> controller -> adapter -> board ->
    // engine. Loading/rendering starts paused and never clocks RTL, except for
    // the design's startup reset (LauncherDesign::startupReset). A scripted run
    // instead starts with the shared scripted startup and runs.
    BoardAdapter boardAdapter(board.get());
    // --log keeps the whole structured log for the file; the Logs view then
    // reads it without clearing it.
    if (!run.run.logPath.empty()) boardAdapter.setBoardLogRetained(true);
    SimulationController controller(boardAdapter, design.title);
    if (arguments.scripted) {
        if (!controller.startScript(run.run)) {
            std::fprintf(stderr, "error: cannot start the scripted run: %s\n",
                         qPrintable(controller.errorString()));
            return EXIT_FAILURE;
        }
    }
    // Some designs need their btnC reset (R6) before their outputs mean
    // anything: uart_echo's receiver synchronizer powers up low and decodes a
    // false start bit; vga_pattern's syncs power up asserted, giving the monitor
    // a false first edge. Apply the Reset control's 16-cycle pulse once at
    // startup. A scripted run's startup already did.
    else if (board && (design.startupReset == LauncherDesign::StartupReset::Always
                       || (design.startupReset == LauncherDesign::StartupReset::IfBtncBound
                           && board->hasButton(Button::C)))
             && !controller.reset()) {
        qCritical() << "Cannot apply the startup reset:" << controller.errorString();
        return EXIT_FAILURE;
    }
    controller.setRealtime(parsed->realtime);
    QQmlEngine::setObjectOwnership(&boardAdapter, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("board"),
                                 QVariant::fromValue(&boardAdapter)},
                                {QStringLiteral("controller"),
                                 QVariant::fromValue(&controller)},
                                {QStringLiteral("designSource"), design.source},
                                {QStringLiteral("designFiles"), design.files}});
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
        // --frames 0 is a launch check: it saves nothing.
        if (!run.run.screenshotPath.empty() && run.run.maxFrames > 0
            && !saveScreenshot(window->grabWindow(), QString::fromStdString(run.run.screenshotPath))) {
            std::fprintf(stderr, "error: could not write screenshot '%s'\n",
                         run.run.screenshotPath.c_str());
            artifactFailure = true;
        }
        app.exit(EXIT_SUCCESS);
    };
    if (controller.scripted()) {
        QObject::connect(&controller, &SimulationController::runFinished, &app, finishRun,
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

}  // namespace vb::qt
