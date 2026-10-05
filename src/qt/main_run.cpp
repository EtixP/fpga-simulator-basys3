// virtualbasys: runs any design. Its Verilog is verilated and compiled into a
// design module at run time (cached), which brings the process's one
// Verilator runtime.
//   virtualbasys run SOURCE.v [SOURCE.v ...] [--top NAME] [-I DIR] [options]
#include "constraints/Xdc.h"
#include "design/DesignBuilder.h"
#include "design/DesignModule.h"
#include "design/Process.h"
#include "qt/Launcher.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
constexpr const char* kUsage =
    "usage: virtualbasys run SOURCE.v [SOURCE.v ...] [--top NAME] [-I DIR] [--xdc FILE] [options]\n"
    "       virtualbasys --preview | --help";

// Launcher options that take a value: passed through with it, unread.
constexpr std::array<const char*, 7> kValueOptions{
    "--xdc", "--frames", "--screenshot", "--log", "--at", "--switches", "--send"};

bool launcherValueOption(const QString& argument) {
    return std::find(kValueOptions.begin(), kValueOptions.end(), argument.toStdString())
        != kValueOptions.end();
}

bool takesValue(const QString& argument) {
    return argument == QLatin1String("--top") || argument == QLatin1String("-I")
        || launcherValueOption(argument);
}

bool verilogFile(const QString& argument) {
    for (const char* suffix : {".v", ".sv", ".vh", ".svh"})
        if (argument.endsWith(QLatin1String(suffix), Qt::CaseInsensitive)) return true;
    return false;
}

// A help option given as an option of its own, not as another option's
// value (say, --log's file name); empty if none.
QString helpOption(const QStringList& arguments) {
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString& argument = arguments[i];
        if (argument == QLatin1String("--help") || argument == QLatin1String("-h")
            || argument == QLatin1String("--help-all"))
            return argument;
        if (takesValue(argument)) ++i;
    }
    return {};
}

struct RunCommand {
    std::vector<std::string> sources;
    std::string top;
    std::vector<std::string> includes;
    QStringList launcher;  // program name and the launcher's own options
    QString xdc;           // --xdc as given
    bool xdcGiven = false;
    std::vector<std::string> errors;
};

// Sources come first, straight after "run"; then --top, -I and launcher options.
RunCommand parseRun(const QStringList& arguments) {
    RunCommand run;
    run.launcher << arguments.value(0);
    qsizetype i = 2;
    bool constraintsAsSource = false;
    for (; i < arguments.size() && !arguments[i].startsWith(QLatin1Char('-')); ++i) {
        if (arguments[i].endsWith(QLatin1String(".xdc"), Qt::CaseInsensitive)) {
            run.errors.push_back("'" + arguments[i].toStdString() + "' is a constraints file; give it with --xdc");
            constraintsAsSource = true;
        } else {
            run.sources.push_back(arguments[i].toStdString());
        }
    }
    bool topGiven = false;
    for (; i < arguments.size(); ++i) {
        const QString& argument = arguments[i];
        const bool hasValue = i + 1 < arguments.size();
        if (argument == QLatin1String("--top") || argument == QLatin1String("-I")) {
            const bool top = argument == QLatin1String("--top");
            if (top && topGiven) run.errors.push_back("--top is given more than once");
            topGiven = topGiven || top;
            const QString value = hasValue ? arguments[++i] : QString();
            if (value.isEmpty())
                run.errors.push_back(top ? "--top needs a module name" : "-I needs a directory");
            else if (top)
                run.top = value.toStdString();
            else
                run.includes.push_back(value.toStdString());
        } else if (argument.startsWith(QLatin1String("--top="))) {
            run.errors.push_back("--top takes the module name as the next argument: --top NAME");
        } else if (argument.startsWith(QLatin1String("-I"))) {
            run.includes.push_back(argument.mid(2).toStdString());
        } else if (argument == QLatin1String("--preview")) {
            run.errors.push_back("--preview shows the board without a design and cannot be combined with run");
        } else if (!argument.startsWith(QLatin1Char('-')) && verilogFile(argument)) {
            run.errors.push_back("source files go straight after 'run', before any option: '"
                                 + argument.toStdString() + "'");
        } else {
            run.launcher << argument;
            if (launcherValueOption(argument) && hasValue) {
                if (argument == QLatin1String("--xdc")) {
                    run.xdc = arguments[i + 1];
                    run.xdcGiven = true;
                }
                run.launcher << arguments[++i];
            }
        }
    }
    if (run.sources.empty() && !constraintsAsSource)
        run.errors.push_back("run: expected Verilog source files after 'run'");
    return run;
}

// Each line of a tool's output, prefixed with the tool's name (R5).
void printPrefixed(const char* tool, const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) std::fprintf(stderr, "%s: %s\n", tool, line.c_str());
}

constexpr int kInterrupted = 130;  // as a shell reports a Ctrl-C
}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    // Ctrl-C, SIGTERM or SIGHUP during a build also ends Verilator and the compiler.
    vb::qt::quitOnSignals(app, &vb::design::cancelProcesses);
    QCoreApplication::setApplicationName(QStringLiteral("VirtualBasys"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    const QStringList arguments = QCoreApplication::arguments();
    vb::qt::LauncherDesign design = vb::qt::runnerDesign();

    if (const QString help = helpOption(arguments); !help.isEmpty())
        return vb::qt::runLauncher(app, {arguments.value(0), help}, design);
    if (arguments.value(1) != QLatin1String("run")) {
        // --preview needs no design; anything else is a usage error.
        if (arguments.contains(QStringLiteral("--preview")))
            return vb::qt::runLauncher(app, arguments, design);
        std::fprintf(stderr, "%s\n", kUsage);
        return EXIT_FAILURE;
    }

    // Every mistake the user can fix without a compile is reported before it.
    RunCommand run = parseRun(arguments);
    for (const auto& error : run.errors) std::fprintf(stderr, "error: %s\n", error.c_str());
    if (!run.errors.empty()) return EXIT_FAILURE;
    if (!vb::qt::checkLauncherArguments(run.launcher, design)) return EXIT_FAILURE;
    const std::string xdcText =
        vb::qt::readConstraints(run.xdc.toStdString(), run.xdcGiven ? QString() : design.builtInXdc);
    if (xdcText.empty()) {
        if (run.xdcGiven)
            std::fprintf(stderr, "error: cannot read XDC '%s'\n", qPrintable(run.xdc));
        else
            std::fprintf(stderr, "error: cannot open the built-in constraints\n");
        return EXIT_FAILURE;
    }
    const std::string clock = vb::qt::clockPort(vb::parseXdc(xdcText));

    vb::design::DesignRequest request;
    request.sources = run.sources;
    request.top = run.top;
    request.includeDirs = run.includes;
    request.validate = [&](const std::string& top, const std::vector<std::string>& inputs) -> std::string {
        if (std::find(inputs.begin(), inputs.end(), clock) != inputs.end()) return {};
        return "'" + top + "' has no input port '" + clock + "' for the 100 MHz clock (pin W5); "
            + (run.xdcGiven ? "bind its clock input to W5 in " + run.xdc.toStdString()
                            : std::string("name its clock input clk, as the Basys 3 master constraints "
                                          "do, or bind it to W5 with --xdc"));
    };
    QElapsedTimer timer;
    request.onBuildStart = [&timer, &request](const std::string& top) {
        if (request.rebuild)
            std::fprintf(stderr, "virtualbasys: compiling '%s' with Verilator again\n", top.c_str());
        else
            std::fprintf(stderr, "virtualbasys: compiling '%s' with Verilator (first run of these "
                                 "sources; later runs reuse it)\n", top.c_str());
        timer.start();
    };
    const auto tools = vb::design::Toolchain::fromBuild();
    vb::design::DesignBuild build = vb::design::buildDesign(request, tools);
    // Verilator's messages, also when the build is then interrupted: they are
    // complete once the lint pass has finished.
    printPrefixed("verilator", build.diagnostics);
    if (build.interrupted || vb::design::cancelRequested()) {
        std::fprintf(stderr, "virtualbasys: interrupted\n");
        return kInterrupted;
    }
    std::string error;
    std::unique_ptr<vb::design::DesignModule> module;
    if (build.ok) module = vb::design::DesignModule::load(build.module, error);
    if (build.ok && !module && build.reused) {
        // A damaged cache entry, or one another build left: build it again, once.
        std::fprintf(stderr, "virtualbasys: the cached module cannot be loaded (%s); building it again\n",
                     error.c_str());
        request.rebuild = true;
        build = vb::design::buildDesign(request, tools);
        if (build.interrupted || vb::design::cancelRequested()) {
            std::fprintf(stderr, "virtualbasys: interrupted\n");
            return kInterrupted;
        }
        if (build.ok) module = vb::design::DesignModule::load(build.module, error);
    }
    if (!build.ok) {
        printPrefixed("build", build.buildLog);
        std::fprintf(stderr, "error: %s\n", build.error.c_str());
        return EXIT_FAILURE;
    }
    if (timer.isValid())
        std::fprintf(stderr, "virtualbasys: compiled in %.1f s\n", timer.elapsed() / 1000.0);
    if (!module) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    design.title = QString::fromStdString(build.top);
    design.source = design.title;
    design.files = vb::qt::designFileList(build.files, run.xdc);
    design.createEngine = [&module, top = build.top](const std::string& clockName, std::string& failure) {
        return module->createEngine(top, clockName, failure);
    };
    return vb::qt::runLauncher(app, run.launcher, design);
}
