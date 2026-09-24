// Paired fixed-cycle board, event-loop controller and rendered Qt throughput.
#include VB_MODEL_HEADER
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/SimulationController.h"

#include <QEventLoop>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTest>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
uint64_t positive(const char* text) {
    const std::string_view input(text);
    uint64_t result = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), result);
    if (error != std::errc{} || end != input.data() + input.size() || !result)
        throw std::invalid_argument("cycles and runs must be positive integers");
    return result;
}

void measure(const char* mode, bool realtime, uint64_t minimumCycles, uint64_t run) {
    auto engine = vb::makeVerilatorEngine<VB_MODEL>({.topModule = VB_DESIGN});
    std::ifstream input(VB_XDC);
    if (!input) throw std::runtime_error("cannot read example constraints");
    const std::string xdc((std::istreambuf_iterator<char>(input)), {});
    vb::BoardModel board(*engine, vb::PinBinding::bind(vb::parseXdc(xdc), *engine));
    vb::qt::BoardAdapter adapter(&board);
    vb::qt::SimulationController controller(adapter, QStringLiteral(VB_DESIGN));
    // Identical deterministic startup and input schedule for every path.
    board.setButton(vb::Button::C, true);
    board.tick(16);
    board.setButton(vb::Button::C, false);
    for (unsigned i = 0; i < 4; ++i) board.setSwitch(i, true);
    board.setButton(vb::Button::U, true);
    // UART designs echo a continuous queued stream through the terminal for
    // the whole warmup and measurement (~211 frames per 22M cycles).
    if (board.hasUartRx()) {
        QString text;
        while (text.size() < 512) text += QStringLiteral("VirtualBasys UART benchmark 0123456789\n");
        if (!adapter.sendUartText(text.left(512))) throw std::runtime_error("UART stream rejected");
    }
    board.tick(2'000'000);
    board.setButton(vb::Button::U, false);
    adapter.refresh();

    uint64_t frames = 0;
    bool qmlError = false;
    std::unique_ptr<QQmlApplicationEngine> qml;
    const bool rendered = std::string_view(mode) == "rendered";
    if (rendered) {
        qml = std::make_unique<QQmlApplicationEngine>();
        QQmlEngine::setObjectOwnership(&adapter, QQmlEngine::CppOwnership);
        QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
        QObject::connect(qml.get(), &QQmlEngine::warnings, qml.get(),
                         [&](const QList<QQmlError>&) { qmlError = true; });
        qml->setInitialProperties({{QStringLiteral("board"), QVariant::fromValue(&adapter)},
                                  {QStringLiteral("controller"), QVariant::fromValue(&controller)}});
        qml->load(QUrl::fromLocalFile(QStringLiteral(VB_QT_QML_DIR "/Main.qml")));
        if (qml->rootObjects().size() != 1) throw std::runtime_error("QML window did not load");
        auto* window = qobject_cast<QQuickWindow*>(qml->rootObjects().first());
        if (!window || !QTest::qWaitForWindowExposed(window))
            throw std::runtime_error("QML window was not exposed");
        window->requestActivate();
        if (!QTest::qWaitForWindowActive(window))
            throw std::runtime_error("benchmark window must stay in the foreground");
        QObject::connect(window, &QQuickWindow::frameSwapped, qml.get(), [&] { ++frames; },
                         Qt::QueuedConnection);
        QTest::qWait(50);
        frames = 0;
    }

    const auto initialCycle = board.now();
    const auto start = std::chrono::steady_clock::now();
    if (std::string_view(mode) == "board") {
        uint64_t remaining = minimumCycles;
        while (remaining) {
            const uint64_t chunk = std::min(remaining, vb::qt::SimulationController::BatchCycles);
            board.tick(chunk);
            remaining -= chunk;
        }
    } else {
        QEventLoop loop;
        QTimer limit;
        limit.setTimerType(Qt::PreciseTimer);
        QObject::connect(&limit, &QTimer::timeout, &loop, [&] {
            if (board.now() - initialCycle >= minimumCycles || !controller.errorString().isEmpty()) {
                controller.pause();
                loop.quit();
            }
        });
        QTimer deadline;
        deadline.setSingleShot(true);
        bool timedOut = false;
        QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
            timedOut = true;
            controller.pause();
            loop.quit();
        });
        controller.setRealtime(realtime);
        if (!controller.run()) throw std::runtime_error("controller refused Run");
        limit.start(1);
        deadline.start(30'000);
        loop.exec();
        if (timedOut || !controller.errorString().isEmpty())
            throw std::runtime_error("controller measurement failed or timed out");
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    const auto cycles = board.now() - initialCycle;
    if (cycles < minimumCycles || qmlError || (rendered && !frames))
        throw std::runtime_error("measurement missing cycles or a clean rendered frame");
    if (board.hasUartTx() && board.uartTxBytes().empty())
        throw std::runtime_error("UART benchmark produced no echo traffic");
    std::cout << VB_DESIGN << ',' << mode << ',' << (realtime ? "realtime" : "turbo") << ','
              << run << ',' << cycles << ',' << seconds << ',' << cycles / seconds << ','
              << cycles / seconds / 100'000'000.0 << ',' << frames << '\n';

    // Optional authentic snapshots are taken after timing has ended, paused.
    const QString directory = qEnvironmentVariable("VB_QT_SCREENSHOT_DIR");
    if (rendered && !directory.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(qml->rootObjects().first());
        window->requestUpdate();
        QTest::qWait(30);
        const QString name = QStringLiteral(VB_DESIGN "-%1-%2.png")
            .arg(realtime ? QStringLiteral("realtime") : QStringLiteral("turbo")).arg(run);
        if (!QDir().mkpath(directory) || !window->grabWindow().save(QDir(directory).filePath(name)))
            throw std::runtime_error("could not capture the paused benchmark window");
    }
}
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication::setQuitOnLastWindowClosed(false);
    try {
        if (argc > 3) throw std::invalid_argument("usage: benchmark_qt_control_* [cycles] [runs]");
        const uint64_t cycles = argc > 1 ? positive(argv[1]) : 20'000'000;
        const uint64_t runs = argc > 2 ? positive(argv[2]) : 3;
        if (cycles > 1'000'000'000 || runs > 100)
            throw std::invalid_argument("benchmark limit: 1 billion cycles, 100 runs");
        std::cout << "design,mode,pacing,run,cycles,seconds,cycles_per_second,realtime_multiplier,frames\n"
                  << std::setprecision(9);
        for (uint64_t run = 1; run <= runs; ++run) {
            for (unsigned order = 0; order < 3; ++order) {
                const char* modes[] = {"board", "controller", "rendered"};
                measure(modes[(order + run - 1) % 3], false, cycles, run);
            }
            measure("rendered", true, cycles, run);
        }
    } catch (const std::exception& error) {
        std::cerr << "benchmark: " << error.what() << '\n';
        return 1;
    }
}
