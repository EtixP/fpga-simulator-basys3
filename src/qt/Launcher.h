#pragma once
#include "constraints/Xdc.h"
#include "engine/SimEngine.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QGuiApplication;

namespace vb::qt {

// What a launcher runs: one built-in example, or a design loaded at run time.
struct LauncherDesign {
    enum class StartupReset {
        Never,        // opens at cycle 0
        Always,       // the Reset control's pulse once, before the window opens
        IfBtncBound,  // the same, when the constraints bind BTNC (R6 convention)
    };

    QString title;            // the design's name in the window
    QString source;           // QML designSource: example stem or top module
    QStringList files;        // Project pane; empty: examples/<source>.v and .xdc
    QString builtInXdc;       // Qt resource used without --xdc
    bool boardDefaultXdc = false;  // builtInXdc constrains the whole board
    StartupReset startupReset = StartupReset::Never;
    uint64_t cyclesPerFrame = 100'000;  // a scripted run's frame (--frames)
    QString description;      // --help text
    // --help only: an argument the caller handles itself (Qt's usage line
    // and argument list), e.g. virtualbasys's "run SOURCE.v ...".
    struct HelpArgument {
        QString name, description, syntax;
    };
    std::optional<HelpArgument> helpArgument;
    // A new engine clocked by `clock` (the port bound to the 100 MHz pin W5,
    // or "clk"), or null with `error` set. Unset: only --preview is possible.
    std::function<std::unique_ptr<SimEngine>(const std::string& clock, std::string& error)> createEngine;
};

// Ctrl-C, SIGTERM and SIGHUP (a closed terminal) end the app normally, so a
// scripted run still writes its --log. Call right after creating the
// application. `onSignal`, if given,
// runs inside the signal handler and must be async-signal-safe.
void quitOnSignals(QGuiApplication& app, void (*onSignal)() = nullptr);

// The constraints a launcher uses: the file at `path` (--xdc) or, when
// `builtIn` is set, that Qt resource. Empty when they cannot be read.
std::string readConstraints(const std::string& path, const QString& builtIn);

// The master clock: the port the constraints bind to the 100 MHz pin W5,
// like every other board resource; "clk" when none is bound.
std::string clockPort(const XdcDoc& xdc);

// virtualbasys run (main_run.cpp): any design, with the Basys 3 master
// constraints unless --xdc, and the Reset pulse at startup when the design
// binds btnC (R6). Title, files and engine are set once the design is built.
LauncherDesign runnerDesign();

// The Project pane's lines for a loaded design: the files Verilator read,
// relative to the working directory inside it and ~/ in the home folder,
// then the constraints as given (`xdc`), or the built-in ones when empty.
QStringList designFileList(const std::vector<std::string>& files, const QString& xdc);

// Checks the launcher options in `arguments` (arguments[0] is the program)
// as runLauncher would for a design that will be there, printing the same
// messages; false if any is wrong. Runs nothing.
bool checkLauncherArguments(const QStringList& arguments, const LauncherDesign& design);

// Parses the launcher options in `arguments` (arguments[0] is the program),
// opens the window and runs until it closes. Returns the process exit code.
int runLauncher(QGuiApplication& app, const QStringList& arguments, const LauncherDesign& design);

}  // namespace vb::qt
