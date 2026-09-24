// Chooses the VGA display technique by measurement: the production
// scene-graph texture item versus a QQuickPaintedItem painting the same QImage,
// both fed by the real vga_pattern simulation under the real controller. The
// render thread's synchronize+render time is recorded per presented frame,
// split by whether that frame uploaded a new VGA image.
#include "Vvga_pattern.h"
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/SimulationController.h"
#include "qt/VgaDisplay.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickPaintedItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

// The alternative: CPU painting of the same frame image with nearest scaling.
class PaintedVgaDisplay : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(vb::qt::VgaFrameModel* frame READ frame WRITE setFrame FINAL)

public:
    explicit PaintedVgaDisplay(QQuickItem* parent = nullptr) : QQuickPaintedItem(parent) {
        setOpaquePainting(true);
        setAntialiasing(false);
    }
    vb::qt::VgaFrameModel* frame() const { return frame_; }
    void setFrame(vb::qt::VgaFrameModel* frame) {
        frame_ = frame;
        if (frame_) connect(frame_, &vb::qt::VgaFrameModel::frameChanged, this, [this] { update(); });
    }
    void paint(QPainter* painter) override {
        if (!frame_ || frame_->image().isNull()) return;
        painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter->drawImage(boundingRect(), frame_->image());
    }

private:
    vb::qt::VgaFrameModel* frame_ = nullptr;
};

namespace {
uint64_t positive(const char* text) {
    const std::string_view input(text);
    uint64_t result = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), result);
    if (error != std::errc{} || end != input.data() + input.size() || !result)
        throw std::invalid_argument("cycles and runs must be positive integers");
    return result;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

void measure(const char* mode, uint64_t minimumCycles, uint64_t run) {
    auto engine = vb::makeVerilatorEngine<Vvga_pattern>({.topModule = "vga_pattern"});
    std::ifstream input(VB_XDC);
    if (!input) throw std::runtime_error("cannot read example constraints");
    const std::string xdc((std::istreambuf_iterator<char>(input)), {});
    vb::BoardModel board(*engine, vb::PinBinding::bind(vb::parseXdc(xdc), *engine));
    vb::qt::BoardAdapter adapter(&board);
    vb::qt::SimulationController controller(adapter, QStringLiteral("VGA Pattern"));
    if (!controller.reset()) throw std::runtime_error("startup reset failed");
    board.tick(3'500'000);  // frame 0 exists before timing starts
    adapter.refresh();

    const std::string_view kind(mode);
    // Declared before the engine: the render thread may deliver a last frame
    // while the window is torn down, so its sample state must outlive it.
    std::mutex samplesLock;
    std::vector<double> uploadMs;
    std::vector<double> otherMs;
    uint64_t presentations = 0;
    QElapsedTimer frameTimer;
    quint64 shownSerial = 0;
    bool uploading = false;
    std::unique_ptr<QQmlApplicationEngine> qml;
    QQuickWindow* window = nullptr;
    if (kind != "none") {
        qml = std::make_unique<QQmlApplicationEngine>();
        bool qmlError = false;
        QObject::connect(qml.get(), &QQmlEngine::warnings, qml.get(),
                         [&](const QList<QQmlError>& errors) {
                             for (const auto& error : errors) std::cerr << qPrintable(error.toString()) << '\n';
                             qmlError = true;
                         });
        QQmlEngine::setObjectOwnership(&adapter, QQmlEngine::CppOwnership);
        qml->setInitialProperties({{QStringLiteral("board"), QVariant::fromValue(&adapter)}});
        const QByteArray item = kind == "texture" ? "VgaDisplay" : "PaintedVgaDisplay";
        qml->loadData(
            "import QtQuick\n"
            "import VirtualBasys.Board\n"
            "import VirtualBasys.Bench\n"
            "Window {\n"
            "    required property BoardAdapter board\n"
            "    width: 700; height: 540; visible: true; color: 'black'\n"
            "    " + item + " { x: 30; y: 30; width: 640; height: 480; frame: board.vga }\n"
            "}\n");
        if (qml->rootObjects().size() != 1 || qmlError) throw std::runtime_error("scene did not load");
        window = qobject_cast<QQuickWindow*>(qml->rootObjects().first());
        if (!window || !QTest::qWaitForWindowExposed(window))
            throw std::runtime_error("window was not exposed");
        window->requestActivate();
        if (!QTest::qWaitForWindowActive(window))
            throw std::runtime_error("benchmark window must stay in the foreground");
        // Synchronization runs with the GUI thread blocked; reading the model
        // there is safe. Timings are taken on the render thread.
        QObject::connect(window, &QQuickWindow::beforeSynchronizing, window, [&] {
            uploading = adapter.vga()->imageSerial() != shownSerial;
            shownSerial = adapter.vga()->imageSerial();
            frameTimer.start();
        }, Qt::DirectConnection);
        QObject::connect(window, &QQuickWindow::afterRendering, window, [&] {
            const double ms = frameTimer.nsecsElapsed() / 1e6;
            std::lock_guard lock(samplesLock);
            (uploading ? uploadMs : otherMs).push_back(ms);
        }, Qt::DirectConnection);
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] { ++presentations; },
                         Qt::QueuedConnection);
        QTest::qWait(100);
        std::lock_guard lock(samplesLock);
        uploadMs.clear();
        otherMs.clear();
        presentations = 0;
    }

    const auto initialCycle = board.now();
    const auto initialFrames = board.vgaCompletedFrames();
    const auto start = std::chrono::steady_clock::now();
    QEventLoop loop;
    QTimer limit;
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
    if (!controller.run()) throw std::runtime_error("controller refused Run");
    limit.start(1);
    deadline.start(60'000);
    loop.exec();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (timedOut || !controller.errorString().isEmpty())
        throw std::runtime_error("measurement failed or timed out");
    if (window && !window->isActive())
        throw std::runtime_error("benchmark window must stay in the foreground");
    const auto cycles = board.now() - initialCycle;
    const auto frames = board.vgaCompletedFrames() - initialFrames;
    std::lock_guard lock(samplesLock);
    if (window && uploadMs.empty()) throw std::runtime_error("no frame upload was presented");
    std::cout << mode << ',' << run << ',' << cycles << ',' << seconds << ',' << cycles / seconds << ','
              << frames << ',' << frames / seconds << ',' << presentations << ','
              << uploadMs.size() << ',' << median(uploadMs) << ',' << otherMs.size() << ','
              << median(otherMs) << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication::setQuitOnLastWindowClosed(false);
    qmlRegisterType<PaintedVgaDisplay>("VirtualBasys.Bench", 1, 0, "PaintedVgaDisplay");
    try {
        if (argc > 3) throw std::invalid_argument("usage: benchmark_qt_vga_display [cycles] [runs]");
        const uint64_t cycles = argc > 1 ? positive(argv[1]) : 16'800'000;
        const uint64_t runs = argc > 2 ? positive(argv[2]) : 3;
        if (cycles > 1'000'000'000 || runs > 100)
            throw std::invalid_argument("benchmark limit: 1 billion cycles, 100 runs");
        std::cout << "display,run,cycles,seconds,cycles_per_second,vga_frames,vga_frames_per_second,"
                     "presentations,upload_presentations,upload_median_ms,other_presentations,"
                     "other_median_ms\n"
                  << std::setprecision(9);
        const char* modes[] = {"none", "texture", "painted"};
        for (uint64_t run = 1; run <= runs; ++run)
            for (unsigned order = 0; order < 3; ++order)
                measure(modes[(order + run - 1) % 3], cycles, run);
    } catch (const std::exception& error) {
        std::cerr << "benchmark: " << error.what() << '\n';
        return 1;
    }
}

#include "qt_vga_display.moc"
