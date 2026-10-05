// A launcher for one built-in example, which it links (VB_MODEL).
#include VB_MODEL_HEADER
#include "engine/VerilatorEngine.h"
#include "qt/Launcher.h"

#include <QGuiApplication>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    vb::qt::quitOnSignals(app);
    QCoreApplication::setApplicationName(QStringLiteral("VirtualBasys"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    vb::qt::LauncherDesign design;
    design.title = QStringLiteral(VB_DESIGN_TITLE);
    design.source = QStringLiteral(VB_DESIGN);
    design.builtInXdc = QStringLiteral(":/examples/" VB_DESIGN ".xdc");
    design.startupReset = VB_STARTUP_RESET ? vb::qt::LauncherDesign::StartupReset::Always
                                           : vb::qt::LauncherDesign::StartupReset::Never;
    design.cyclesPerFrame = VB_CYCLES_PER_FRAME;
    design.description = QStringLiteral(
        "VirtualBasys: the built-in %1 example. Scripted runs (--frames, --at, --switches, --send, "
        "--log, --screenshot) start with a 16-cycle reset and run by themselves; inputs and sends "
        "apply at exact virtual cycles.").arg(QStringLiteral(VB_DESIGN_TITLE));
    // The example's own clock port; its constraints bind it to W5.
    design.createEngine = [](const std::string&, std::string& error) -> std::unique_ptr<vb::SimEngine> {
        try {
            return vb::makeVerilatorEngine<VB_MODEL>({.topModule = VB_DESIGN});
        } catch (const std::exception& failure) {
            error = failure.what();
            return nullptr;
        }
    };
    return vb::qt::runLauncher(app, QCoreApplication::arguments(), design);
}
