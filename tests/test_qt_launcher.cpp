// The shared launcher body, in process, with a recording engine: the startup
// reset rules, the clock port taken from the constraints, engine failures and
// what the window shows, and virtualbasys run's own design description and
// file list. Each launch is a --smoke-test (one rendered frame).
#include "check.h"
#include "qt/Launcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
struct Record {
    struct Poke { uint64_t cycle; std::string port; uint64_t value; };
    std::vector<Poke> pokes;
    uint64_t now = 0;
    std::string clock;
    int engines = 0;
};

// Ports btnC, clk, led[15:0], sw[15:0]; the record outlives the engine.
class RecordingEngine final : public vb::SimEngine {
public:
    explicit RecordingEngine(Record& record) : record_(record) {}
    vb::SignalId lookup(std::string_view name) override {
        for (size_t i = 0; i < ports_.size(); ++i)
            if (ports_[i].name == name) return vb::SignalId(i);
        return vb::kNoSignal;
    }
    vb::SignalInfo info(vb::SignalId id) const override { return ports_.at(size_t(id)); }
    std::vector<vb::SignalInfo> ports() const override { return ports_; }
    uint64_t now() const override { return record_.now; }
    void step(uint64_t cycles) override { record_.now += cycles; }
    uint64_t peek(vb::SignalId id) override { return values_.at(size_t(id)); }
    void poke(vb::SignalId id, uint64_t value) override {
        values_.at(size_t(id)) = value;
        record_.pokes.push_back({record_.now, ports_.at(size_t(id)).name, value});
    }
    void setTraceFile(std::string_view) override {}
    void trace(bool) override {}

private:
    Record& record_;
    std::vector<vb::SignalInfo> ports_{{"btnC", 1, true}, {"clk", 1, true}, {"led", 16, false},
                                      {"sw", 16, true}};
    std::vector<uint64_t> values_ = std::vector<uint64_t>(4, 0);
};

QTemporaryDir* scratch = nullptr;

// The visual tree, as the UI tests search it: panes reparent their content.
QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = findItem(child, name)) return found;
    return nullptr;
}

QString writeXdc(const char* name, const QByteArray& text) {
    const QString path = scratch->filePath(QString::fromLatin1(name));
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(text);
    return path;
}

struct Seen {
    QString title;
    QString files;
};

// One smoke-test launch; `seen` is read from the window while it is up.
int launch(QGuiApplication& app, vb::qt::LauncherDesign design, const QStringList& options,
           Record& record, Seen* seen = nullptr) {
    design.createEngine = [&record](const std::string& clock, std::string&) {
        record.clock = clock;
        ++record.engines;
        return std::unique_ptr<vb::SimEngine>(new RecordingEngine(record));
    };
    if (seen) {
        QTimer::singleShot(0, &app, [seen] {
            for (QWindow* window : QGuiApplication::topLevelWindows()) {
                auto* quick = qobject_cast<QQuickWindow*>(window);
                if (!quick) continue;
                seen->title = quick->title();
                if (auto* files = findItem(quick->contentItem(), QStringLiteral("projectDesignFiles")))
                    seen->files = files->property("text").toString();
            }
        });
    }
    return vb::qt::runLauncher(app, QStringList{QStringLiteral("launcher"), QStringLiteral("--smoke-test")}
                                        + options, design);
}

bool heldBtncForReset(const Record& record) {
    return record.pokes.size() >= 2 && record.pokes[0].port == "btnC" && record.pokes[0].value == 1
        && record.pokes[0].cycle == 0 && record.pokes[1].port == "btnC" && record.pokes[1].value == 0
        && record.pokes[1].cycle == 16;
}
}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    scratch = &dir;
    const QString withButton = writeXdc("button.xdc", "set_property PACKAGE_PIN W5 [get_ports clk]\n"
                                                     "set_property PACKAGE_PIN U18 [get_ports btnC]\n");
    const QString noButton = writeXdc("nobutton.xdc", "set_property PACKAGE_PIN W5 [get_ports clk]\n");
    const QString otherClock = writeXdc("sysclk.xdc", "set_property PACKAGE_PIN W5 [get_ports sysclk]\n");
    const QString noClock = writeXdc("noclock.xdc", "set_property PACKAGE_PIN U16 [get_ports {led[0]}]\n");

    // virtualbasys run's designs: the master constraints by default, and one
    // reset pulse, from cycle 0 to 16, only when BTNC is bound.
    vb::qt::LauncherDesign runner = vb::qt::runnerDesign();
    runner.title = QStringLiteral("probe");
    runner.source = QStringLiteral("probe");
    CHECK(runner.startupReset == vb::qt::LauncherDesign::StartupReset::IfBtncBound);
    CHECK(runner.boardDefaultXdc && runner.builtInXdc == QStringLiteral(":/constraints/Basys3_Master.xdc"));
    {
        Record record;
        CHECK_EQ(launch(app, runner, {}, record), 0);  // the master file binds btnC
        CHECK(heldBtncForReset(record));
        CHECK_EQ(record.now, 16);
        CHECK(record.clock == "clk");
    }
    {
        Record record;
        CHECK_EQ(launch(app, runner, {"--xdc", noButton}, record), 0);
        CHECK(record.pokes.empty());
        CHECK_EQ(record.now, 0);
    }
    {
        // --preview: no design, and the Project pane says how to load one.
        Record record;
        Seen seen;
        CHECK_EQ(launch(app, runner, {"--preview"}, record, &seen), 0);
        CHECK_EQ(record.engines, 0);
        CHECK(seen.files == QStringLiteral("Run virtualbasys run DESIGN.v to load a design."));
    }

    vb::qt::LauncherDesign design;
    design.title = QStringLiteral("probe");
    design.source = QStringLiteral("probe");
    // Never and Always, for the built-in examples.
    design.startupReset = vb::qt::LauncherDesign::StartupReset::Never;
    {
        Record record;
        CHECK_EQ(launch(app, design, {"--xdc", withButton}, record), 0);
        CHECK_EQ(record.now, 0);
    }
    design.startupReset = vb::qt::LauncherDesign::StartupReset::Always;
    {
        Record record;
        CHECK_EQ(launch(app, design, {"--xdc", withButton}, record), 0);
        CHECK(heldBtncForReset(record));
    }

    // The clock is the port bound to W5; "clk" when none is.
    {
        Record record;
        launch(app, design, {"--xdc", otherClock}, record);
        CHECK(record.clock == "sysclk");
        Record fallback;
        launch(app, design, {"--xdc", noClock}, fallback);
        CHECK(fallback.clock == "clk");
    }

    // An engine that cannot be created is an error, before any window.
    {
        vb::qt::LauncherDesign failing = design;
        failing.createEngine = [](const std::string&, std::string& error) {
            error = "no such clock";
            return std::unique_ptr<vb::SimEngine>();
        };
        CHECK_EQ(vb::qt::runLauncher(app, {"launcher", "--smoke-test", "--xdc", withButton}, failing), 1);
    }

    // What the window shows: the design's name and its own files.
    design.startupReset = vb::qt::LauncherDesign::StartupReset::Never;
    design.files = QStringList{QStringLiteral("rtl/top.v"), QStringLiteral("rtl/pins.xdc")};
    {
        Record record;
        Seen seen;
        CHECK_EQ(launch(app, design, {"--xdc", withButton}, record, &seen), 0);
        CHECK(seen.title == QStringLiteral("VirtualBasys — probe"));
        CHECK(seen.files == QStringLiteral("rtl/top.v\nrtl/pins.xdc"));
    }
    design.files.clear();
    {
        Record record;
        Seen seen;
        launch(app, design, {"--xdc", withButton}, record, &seen);
        CHECK(seen.files == QStringLiteral("examples/probe.v\nexamples/probe.xdc"));
    }

    // virtualbasys run's Project pane: paths relative to the working directory
    // inside it, ~/ in the home folder, absolute elsewhere; then the
    // constraints as given, or the built-in ones.
    {
        const QString root = QFileInfo(dir.path()).canonicalFilePath();
        CHECK(QDir(root).mkpath(QStringLiteral("work/rtl")) && QDir(root).mkpath(QStringLiteral("home")));
        const QString previous = QDir::currentPath();
        const QByteArray home = qgetenv("HOME");
        CHECK(QDir::setCurrent(root + QStringLiteral("/work")));
        qputenv("HOME", (root + QStringLiteral("/home")).toUtf8());
        const std::vector<std::string> files{(root + "/work/rtl/top.v").toStdString(),
                                             (root + "/home/lib/uart.v").toStdString(), "/opt/ip/fifo.v"};
        const QStringList builtIn = vb::qt::designFileList(files, {});
        const QStringList own = vb::qt::designFileList(files, QStringLiteral("../pins.xdc"));
        qputenv("HOME", home);
        CHECK(QDir::setCurrent(previous));
        CHECK((builtIn == QStringList{"rtl/top.v", "~/lib/uart.v", "/opt/ip/fifo.v",
                                      "Basys 3 master constraints (built in)"}));
        CHECK(own.last() == QStringLiteral("../pins.xdc"));
    }

    // Checking options runs nothing and needs no design yet.
    {
        vb::qt::LauncherDesign unloaded;
        CHECK(vb::qt::checkLauncherArguments({"launcher", "--frames", "3", "--switches", "1"}, unloaded));
        CHECK(!vb::qt::checkLauncherArguments({"launcher", "--frames", "x"}, unloaded));
        CHECK(!vb::qt::checkLauncherArguments({"launcher", "--bogus"}, unloaded));
        CHECK(!vb::qt::checkLauncherArguments({"launcher", "stray"}, unloaded));
    }
    std::puts("test_qt_launcher: PASS");
    return 0;
}
