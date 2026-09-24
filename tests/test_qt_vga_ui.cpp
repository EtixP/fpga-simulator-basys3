// The Qt VGA monitor against the unchanged vga_pattern RTL and a synthetic
// raster design. Rendered windows are grabbed and compared pixel by pixel:
// the expected image comes from the RTL's bar/border specification (or the
// raster arithmetic), never from the adapter or the display item.
#include "Vvga_pattern.h"
#include "qt_board_ui_fixture.h"
#include "qt_vga_raster.h"

#include "qt/VgaDisplay.h"

#include <QQmlComponent>
#include <QScreen>
#include <QStyleHints>
#include <QWheelEvent>
#include <QtQml/qqmlextensionplugin.h>

#include <functional>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
constexpr uint64_t kResetCycles = vb::qt::SimulationController::ResetCycles;

// The RTL's pattern: eight 80-pixel bars, white sides and top, blue bottom.
QRgb barPixel(int x, int y) {
    static const uint16_t bars[8] = {0xFFF, 0xFF0, 0x0FF, 0x0F0, 0xF0F, 0xF00, 0x00F, 0x000};
    uint16_t rgb = bars[x / 80];
    if (x == 0 || x == 639 || y == 0) rgb = 0xFFF;
    if (y == 479) rgb = 0x00F;
    return qRgb(((rgb >> 8) & 15) * 17, ((rgb >> 4) & 15) * 17, (rgb & 15) * 17);
}

// Frame k of vga_pattern completes at its (k+2)th Vsync fall: 1,568,000
// cycles of counting to the first, 1,680,000 per frame, plus the reset hold,
// the /4 enable's first tick and the capture-slot label (see test_vga_pattern).
uint64_t patternStamp(uint64_t k) { return 1'568'000 + (k + 1) * 1'680'000 + kResetCycles + 3; }

using Expected = std::function<QRgb(int, int)>;

Expected rasterPixels(uint64_t monitorFrame) {
    return [monitorFrame](int x, int y) {
        const uint16_t c = vga_raster::pattern(monitorFrame + 1, uint32_t(x), uint32_t(y));
        return qRgb(((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17);
    };
}

// Compares every device pixel of `display` inside `clip` (scene coordinates)
// with the expected framebuffer pixel it must show. The display snaps its
// origin to the nearest device pixel; each framebuffer pixel then covers an
// exact square of scale x devicePixelRatio device pixels.
QString compareDisplay(QQuickWindow* window, QQuickItem* display, const Expected& expected,
                       QRectF clip = {}) {
    const QImage shot = window->grabWindow().convertToFormat(QImage::Format_RGB32);
    if (shot.isNull()) return QStringLiteral("window grab failed");
    const qreal ratio = shot.devicePixelRatio();
    const qreal perPixel = display->width() / 640.0 * ratio;
    if (perPixel != std::floor(perPixel) || display->height() / 480.0 * ratio != perPixel)
        return QStringLiteral("non-integer scale %1").arg(perPixel);
    const QPointF origin = display->mapToScene(QPointF());
    const int left = int(std::round(origin.x() * ratio));
    const int top = int(std::round(origin.y() * ratio));
    if (clip.isNull()) clip = QRectF(0, 0, window->width(), window->height());
    const QRect bounds = QRectF(clip.x() * ratio, clip.y() * ratio, clip.width() * ratio,
                                clip.height() * ratio).toAlignedRect().intersected(shot.rect());
    const int step = int(perPixel);
    int checked = 0;
    int mismatches = 0;
    QString first;
    for (int v = 0; v < 480 * step; ++v) {
        const int py = top + v;
        if (py < bounds.top() || py > bounds.bottom()) continue;
        const auto* line = reinterpret_cast<const QRgb*>(shot.constScanLine(py));
        for (int u = 0; u < 640 * step; ++u) {
            const int px = left + u;
            if (px < bounds.left() || px > bounds.right()) continue;
            ++checked;
            const QRgb want = expected(u / step, v / step);
            if ((line[px] & 0xFFFFFF) != (want & 0xFFFFFF) && !mismatches++) {
                first = QStringLiteral("first mismatch at framebuffer (%1,%2): got %3 want %4")
                    .arg(u / step).arg(v / step).arg(line[px] & 0xFFFFFF, 6, 16, QLatin1Char('0'))
                    .arg(want & 0xFFFFFF, 6, 16, QLatin1Char('0'));
            }
        }
    }
    if (!checked) return QStringLiteral("no display pixels inside the clip");
    return mismatches ? QStringLiteral("%1 of %2 pixels differ; %3").arg(mismatches).arg(checked).arg(first)
                      : QString();
}
}  // namespace

class VgaUiTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<BoardUiFixture<Vvga_pattern>> ui_;
    std::unique_ptr<vb::qt::SimulationController> controller_;
    qint64 wallNanoseconds_ = 0;
    quint64 wheelTimestamp_ = 1'000;

    QString text(const char* name) const {
        const auto* item = ui_->item(name);
        return item ? item->property("text").toString() : QString();
    }
    vb::qt::VgaFrameModel& vga() const { return *ui_->adapter.vga(); }
    QQuickItem* display() const { return ui_->item("vgaDisplay"); }

    bool load() {
        ui_->closeShell();
        controller_.reset();
        vb::qt::SimulationController::Options options;
        options.automaticScheduling = false;
        options.nowNanoseconds = [this] { return wallNanoseconds_; };
        controller_ = std::make_unique<vb::qt::SimulationController>(
            ui_->adapter, QStringLiteral("VGA Pattern"), std::move(options));
        // As the launcher does: the RTL's syncs power up asserted until reset.
        if (ui_->board.now() == 0 && !controller_->reset()) return false;
        return ui_->loadShell(controller_.get(),
                              {{QStringLiteral("designSource"), QStringLiteral("vga_pattern")}});
    }

    bool editStep(const QString& value) const {
        auto* spin = ui_->item("stepCycles");
        if (!spin || !spin->isEnabled()) return false;
        auto* editor = spin->property("contentItem").value<QQuickItem*>();
        editor->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(ui_->window, Qt::Key_A, Qt::ControlModifier);
        for (const auto character : value)
            QTest::keyClick(ui_->window, Qt::Key(int(Qt::Key_0) + character.digitValue()));
        QTest::keyClick(ui_->window, Qt::Key_Return);
        return spin->property("value").toInt() == value.toInt();
    }

    bool stepMillion() {
        if (ui_->item("stepCycles")->property("value").toInt() != 1'000'000
            && !editStep(QStringLiteral("1000000"))) return false;
        return ui_->click("stepButton");
    }

    void runBatch() {
        wallNanoseconds_ += 20'000'000;  // 100k cycles per 20 ms: 5 Mcycles/s
        controller_->processBatch();
    }

    QRectF viewport() const { return ui_->bounds(ui_->item("boardFlickable")); }

    QString footerProblem() const {
        const QRectF windowBounds(0, 0, ui_->window->width(), ui_->window->height());
        QRectF previous;
        for (const char* name : {"simulationStatus", "simulationCycles", "simulationTime",
                                 "simulationVgaFrame", "simulationSpeed"}) {
            auto* label = ui_->item(name);
            if (!label || !label->isVisible()) return QStringLiteral("missing %1").arg(QLatin1String(name));
            const auto rect = ui_->bounds(label);
            if (!windowBounds.contains(rect) || (!previous.isNull() && rect.left() < previous.right())
                || label->property("contentWidth").toReal() > label->width() + 1)
                return QStringLiteral("clipped or overlapping %1").arg(QLatin1String(name));
            previous = rect;
        }
        return {};
    }

    // Captures park the pointer away from controls so no tooltip shows.
    bool capture(const char* name) const {
        QTest::mouseMove(ui_->window, QPoint(8, 8));
        QTest::qWait(150);
        return ui_->capture(name);
    }

private slots:
    void init() {
        wallNanoseconds_ = 0;
        ui_ = std::make_unique<BoardUiFixture<Vvga_pattern>>(
            "vga_pattern", VB_SOURCE_DIR "/examples/vga_pattern.xdc");
        QVERIFY2(load(), qPrintable(ui_->warnings.join('\n')));
        QTRY_VERIFY(ui_->item("vgaMonitor") && ui_->item("vgaMonitor")->isVisible());
    }

    void cleanup() {
        if (!ui_) return;
        if (controller_) controller_->pause();
        ui_->closeShell();
        controller_.reset();
        // The view mirrors the board; it never invents or skips state.
        QCOMPARE(vga().completedFrames(), qint64(ui_->board.vgaCompletedFrames()));
        QVERIFY2(ui_->warnings.isEmpty(), qPrintable(ui_->warnings.join('\n')));
        ui_.reset();
    }

    void waitsThenShowsTheExactFirstFrame() {
        QCOMPARE(text("projectDesignFiles"), QStringLiteral("examples/vga_pattern.v\nexamples/vga_pattern.xdc"));
        QCOMPARE(text("simulationCycles"), QStringLiteral("16 cycles"));
        QVERIFY(ui_->item("vgaWaitingTitle")->isVisible());
        QCOMPARE(text("vgaFrameInfo"), QStringLiteral("Waiting for the first complete frame"));
        QCOMPARE(text("simulationVgaFrame"), QStringLiteral("VGA: no complete frame yet"));
        // At the default window size the whole frame is visible without scrolling.
        QVERIFY(viewport().contains(ui_->bounds(display())));
        QVERIFY(capture("vga-monitor-waiting"));
        for (int step = 0; step < 3; ++step) QVERIFY(stepMillion());
        QCOMPARE(ui_->board.now(), uint64_t{3'000'016});
        QVERIFY(!vga().hasFrame());  // frame 0 closes at 3,248,019
        QVERIFY(stepMillion());
        QCOMPARE(vga().completedFrames(), qint64(1));
        QTRY_COMPARE(text("vgaFrameInfo"),
                     QStringLiteral("Frame 0 · completed at cycle %1 (0.0%2 s) · 4 cycles/pixel")
                         .arg(patternStamp(0)).arg(patternStamp(0) * 10, 8, 10, QLatin1Char('0')));
        QVERIFY(!ui_->item("vgaWaitingTitle")->isVisible());
        QVERIFY(!ui_->item("vgaStatus")->isVisible());
        QCOMPARE(text("simulationVgaFrame"), QStringLiteral("VGA frame 0 · cycle %1").arg(patternStamp(0)));
        QCOMPARE(display()->width(), 640.0);
        // A clamped laptop-height window still shows the whole frame.
        ui_->window->resize(1280, 859);
        QTRY_COMPARE(ui_->window->height(), 859);
        QTRY_VERIFY(viewport().contains(ui_->bounds(display())));
        ui_->window->resize(1280, 880);
        QTRY_VERIFY2(compareDisplay(ui_->window, display(), barPixel, viewport()).isEmpty(),
                     qPrintable(compareDisplay(ui_->window, display(), barPixel, viewport())));
        // Hand-anchored checks independent of barPixel: yellow bar, white top,
        // blue bottom (channel order and vertical orientation).
        const QImage shot = ui_->window->grabWindow();
        const qreal ratio = shot.devicePixelRatio();
        const QPointF origin = display()->mapToScene(QPointF());
        const auto at = [&](int x, int y) {
            return shot.pixel(int(std::round(origin.x() * ratio)) + int(x * ratio),
                              int(std::round(origin.y() * ratio)) + int(y * ratio)) & 0xFFFFFF;
        };
        QCOMPARE(at(120, 240), QRgb(0xFFFF00));
        QCOMPARE(at(320, 0), QRgb(0xFFFFFF));
        QCOMPARE(at(320, 479), QRgb(0x0000FF));
        QVERIFY(capture("vga-monitor"));
    }

    void runningUpdatesFramesAndMeasuredRate() {
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(controller_->running());
        while (ui_->board.vgaCompletedFrames() < 3) runBatch();
        QVERIFY(controller_->speedAvailable());
        const qint64 frames = vga().completedFrames();
        QTRY_VERIFY(text("vgaFrameInfo").startsWith(QStringLiteral("Frame %1 · completed at cycle %2 (")
                         .arg(frames - 1).arg(patternStamp(uint64_t(frames - 1)))));
        // 100k cycles per 20 ms of injected wall time = 5 Mcycles/s.
        QCOMPARE(text("vgaFrameRate"),
                 QStringLiteral("≈ 3.0 simulated frames/s at the measured speed; real time would be 59.5."));
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(!controller_->running());
        QTRY_COMPARE(text("vgaFrameRate"),
                     QStringLiteral("Every completed frame is exact; frames advance with virtual time, not wall time."));
        QTRY_VERIFY2(compareDisplay(ui_->window, display(), barPixel, viewport()).isEmpty(),
                     qPrintable(compareDisplay(ui_->window, display(), barPixel, viewport())));
    }

    void resetMidFrameShowsTheMonitorsDiagnosis() {
        for (int step = 0; step < 4; ++step) QVERIFY(stepMillion());
        QCOMPARE(vga().completedFrames(), qint64(1));
        QVERIFY(ui_->click("resetButton"));  // mid-way through frame 1
        QCOMPARE(ui_->board.now(), uint64_t{4'000'032});
        // The monitor keeps the last completed frame until the next one.
        QCOMPARE(vga().completedFrames(), qint64(1));
        QVERIFY(compareDisplay(ui_->window, display(), barPixel, viewport()).isEmpty());
        // Every later frame's status is shown exactly as the monitor reports
        // it; the pattern recovers once a complete clean period follows.
        bool sawProblem = false;
        while (ui_->board.vgaCompletedFrames() < 4) {
            const auto before = ui_->board.vgaCompletedFrames();
            while (ui_->board.vgaCompletedFrames() == before) QVERIFY(stepMillion());
            QTRY_COMPARE(ui_->item("vgaStatus")->isVisible(), !ui_->board.vgaOk());
            if (!ui_->board.vgaOk()) {
                sawProblem = true;
                QCOMPARE(text("vgaStatus"), QStringLiteral("Signal problem in the latest frame: %1")
                                               .arg(QString::fromStdString(ui_->board.vgaStatus())));
                QCOMPARE(ui_->item("simulationVgaFrame")->property("color").value<QColor>(),
                         QColor(QStringLiteral("#f0b190")));
                QVERIFY(capture("vga-monitor-reset-diagnosis"));
            }
        }
        QVERIFY(sawProblem);  // the reset cut a frame short
        QVERIFY(ui_->board.vgaOk());
        QTRY_VERIFY(ui_->item("simulationVgaFrame")->property("color").value<QColor>()
                    != QColor(QStringLiteral("#f0b190")));
        QTRY_VERIFY2(compareDisplay(ui_->window, display(), barPixel, viewport()).isEmpty(),
                     qPrintable(compareDisplay(ui_->window, display(), barPixel, viewport())));
    }

    void scrollingAndMinimumWindowStayPixelExact() {
        for (int step = 0; step < 4; ++step) QVERIFY(stepMillion());
        ui_->window->resize(960, 640);
        QTRY_COMPARE(ui_->window->size(), QSize(960, 640));
        // Footer metrics, including the VGA readout, fit without overlap once
        // the native resize has been laid out.
        QTRY_VERIFY2(footerProblem().isEmpty(), qPrintable(footerProblem()));
        QTRY_VERIFY2(compareDisplay(ui_->window, display(), barPixel, viewport()).isEmpty(),
                     qPrintable(compareDisplay(ui_->window, display(), barPixel, viewport())));
        // Animated wheel scrolling moves the display by arbitrary amounts; the
        // visible part stays exact while partly scrolled out of view.
        auto* flick = ui_->item("boardFlickable");
        for (int angle : {-90, -125, 45}) {
            const QPointF at = ui_->bounds(flick).center();
            QWheelEvent event(at, ui_->window->mapToGlobal(at), QPoint(), QPoint(0, angle),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            wheelTimestamp_ += 500;
            event.setTimestamp(wheelTimestamp_);
            QGuiApplication::sendEvent(ui_->window, &event);
            QTRY_VERIFY(!flick->property("moving").toBool());
            QVERIFY(flick->property("contentY").toReal() > 0);
            QTRY_VERIFY2(compareDisplay(ui_->window, display(), barPixel, viewport()).isEmpty(),
                         qPrintable(compareDisplay(ui_->window, display(), barPixel, viewport())));
        }
        QVERIFY(capture("vga-monitor-minimum"));
    }

    void wideWorkspaceShowsExactTwoTimesScale() {
        for (int step = 0; step < 4; ++step) QVERIFY(stepMillion());
        QVERIFY(ui_->click("projectToggle"));
        QVERIFY(ui_->click("inspectorToggle"));
        ui_->window->resize(1400, 880);
        QTRY_COMPARE(ui_->window->width(), 1400);
        // The card is wide enough for 2x only once it exceeds 1280 + margins.
        QTRY_COMPARE(display()->width(), 1280.0);
        QCOMPARE(display()->height(), 960.0);
        QTRY_VERIFY2(compareDisplay(ui_->window, display(), barPixel, viewport()).isEmpty(),
                     qPrintable(compareDisplay(ui_->window, display(), barPixel, viewport())));
        QVERIFY(ui_->click("restoreLayoutButton"));
        QTRY_COMPARE(display()->width(), 640.0);
    }

    void rasterFramesAtFractionalPositionsAndScales() {
        // A minimal scene: the production display item at fractional scene
        // positions and two integer scales, fed by a synthetic raster board.
        vga_raster::RasterEngine engine;
        vb::BoardModel board(engine, vb::PinBinding::bind(vb::parseXdc(vga_raster::xdc(true)), engine));
        vb::qt::BoardAdapter adapter(&board);
        QQmlEngine::setObjectOwnership(&adapter, QQmlEngine::CppOwnership);
        QQmlApplicationEngine qml;
        QStringList warnings;
        QObject::connect(&qml, &QQmlEngine::warnings, &qml, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        qml.setInitialProperties({{QStringLiteral("board"), QVariant::fromValue(&adapter)}});
        // Ordinary literals: moc cannot parse this QML as a raw string literal.
        qml.loadData(
            "import QtQuick\n"
            "import VirtualBasys.Board\n"
            "Window {\n"
            "    required property BoardAdapter board\n"
            "    property real offset: 0\n"
            "    property int pixelScale: 1\n"
            "    width: 1320; height: 1000; visible: true; color: '#202020'\n"
            "    VgaDisplay {\n"
            "        objectName: 'rasterDisplay'\n"
            "        x: 10 + offset; y: 7 + offset\n"
            "        width: 640 * pixelScale; height: 480 * pixelScale\n"
            "        frame: board.vga\n"
            "    }\n"
            "}\n");
        QCOMPARE(qml.rootObjects().size(), 1);
        auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto* item = boardUiFindItem(window->contentItem(), QStringLiteral("rasterDisplay"));
        QVERIFY(item);

        board.tick(vga_raster::frameStamp(0) + 1);
        QVERIFY(adapter.refresh());
        for (int scale : {1, 2}) {
            window->setProperty("pixelScale", scale);
            for (qreal offset : {0.0, 0.25, 0.5, 0.75}) {
                window->setProperty("offset", offset);
                QTRY_VERIFY2(compareDisplay(window, item, rasterPixels(0)).isEmpty(),
                             qPrintable(QStringLiteral("scale %1 offset %2: %3").arg(scale).arg(offset)
                                 .arg(compareDisplay(window, item, rasterPixels(0)))));
            }
        }
        // A new frame is uploaded and shown; frames between refreshes are not.
        board.tick(vga_raster::frameStamp(3) + 1 - board.now());
        QVERIFY(adapter.refresh());
        QTRY_VERIFY2(compareDisplay(window, item, rasterPixels(3)).isEmpty(),
                     qPrintable(compareDisplay(window, item, rasterPixels(3))));

        // Another model with the same serial count still gets its own image.
        {
            vga_raster::RasterEngine otherEngine;
            vb::BoardModel other(otherEngine,
                vb::PinBinding::bind(vb::parseXdc(vga_raster::xdc(true)), otherEngine));
            vb::qt::BoardAdapter otherAdapter(&other);
            other.tick(vga_raster::frameStamp(0) + 1);
            QVERIFY(otherAdapter.refresh());
            other.tick(vga_raster::frameStamp(1) + 1 - other.now());
            QVERIFY(otherAdapter.refresh());
            QCOMPARE(otherAdapter.vga()->imageSerial(), adapter.vga()->imageSerial());
            item->setProperty("frame", QVariant::fromValue(otherAdapter.vga()));
            QTRY_VERIFY2(compareDisplay(window, item, rasterPixels(1)).isEmpty(),
                         qPrintable(compareDisplay(window, item, rasterPixels(1))));
        }
        // Its model is gone: the display clears instead of keeping a stale frame.
        QTRY_VERIFY(!compareDisplay(window, item, rasterPixels(1)).isEmpty());
        const QImage cleared = window->grabWindow();
        const qreal ratio = cleared.devicePixelRatio();
        const QPointF inside = item->mapToScene(QPointF(320, 240)) * ratio;
        QCOMPARE(cleared.pixel(inside.toPoint()) & 0xFFFFFF, QRgb(0x202020));

        // A drawn display resized to nothing, then moved, does not keep
        // requesting frames.
        item->setProperty("frame", QVariant::fromValue(adapter.vga()));
        QTRY_VERIFY(compareDisplay(window, item, rasterPixels(3)).isEmpty());
        item->setWidth(0);
        window->setProperty("offset", 0.25);
        QTest::qWait(100);
        int frames = 0;
        QObject::connect(window, &QQuickWindow::afterAnimating, window, [&] { ++frames; });
        QTest::qWait(400);
        QVERIFY2(frames <= 2, qPrintable(QStringLiteral("%1 frames while idle").arg(frames)));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    // Only the native renderer exposes half-pixel misalignment; the offscreen
    // software renderer rounds image positions itself (docs/qt_vga.md).
    void nativeSnappingAfterParentMovesAndScreenChanges() {
        if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
            QSKIP("snapping is observable only with the native renderer");
        vga_raster::RasterEngine engine;
        vb::BoardModel board(engine, vb::PinBinding::bind(vb::parseXdc(vga_raster::xdc(true)), engine));
        vb::qt::BoardAdapter adapter(&board);
        board.tick(vga_raster::frameStamp(0) + 1);
        QVERIFY(adapter.refresh());
        QQuickWindow window;
        window.resize(720, 560);
        window.setColor(QColor(0x20, 0x20, 0x20));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        // A C++-constructed display whose parent is already in the window.
        auto* holder = new QQuickItem(window.contentItem());
        auto* display = new vb::qt::VgaDisplay(holder);
        display->setSize(QSizeF(640, 480));
        display->setFrame(adapter.vga());
        holder->setPosition(QPointF(20, 20));
        QTRY_VERIFY2(compareDisplay(&window, display, rasterPixels(0)).isEmpty(),
                     qPrintable(compareDisplay(&window, display, rasterPixels(0))));
        holder->setPosition(QPointF(23.5, 27.75));  // an ancestor moves by a fraction
        QTRY_VERIFY2(compareDisplay(&window, display, rasterPixels(0)).isEmpty(),
                     qPrintable(compareDisplay(&window, display, rasterPixels(0))));

        // A half-point origin is exact at 2x; after moving to a 1x screen the
        // snap must be recomputed for the new pixel ratio.
        QScreen* high = nullptr;
        QScreen* low = nullptr;
        for (auto* screen : QGuiApplication::screens()) {
            if (screen->devicePixelRatio() >= 2 && !high) high = screen;
            if (screen->devicePixelRatio() < 1.5 && !low) low = screen;
        }
        if (!high || !low) QSKIP("needs screens with 2x and 1x pixel ratios");
        holder->setPosition(QPointF(20.5, 20.5));
        for (QScreen* screen : {high, low, high}) {
            window.setScreen(screen);
            window.setPosition(screen->availableGeometry().topLeft() + QPoint(40, 40));
            QTRY_COMPARE(window.devicePixelRatio(), screen->devicePixelRatio());
            QTRY_VERIFY2(compareDisplay(&window, display, rasterPixels(0)).isEmpty(),
                         qPrintable(QStringLiteral("ratio %1: %2").arg(screen->devicePixelRatio())
                             .arg(compareDisplay(&window, display, rasterPixels(0)))));
        }
    }

    void rasterDiagnosisAndUnavailableBoards() {
        // The shell for a synthetic board whose Hsync is active-high.
        vga_raster::RasterEngine engine;
        engine.invertHsync = true;
        vb::BoardModel board(engine, vb::PinBinding::bind(vb::parseXdc(vga_raster::xdc(true)), engine));
        vb::qt::BoardAdapter adapter(&board);
        vb::qt::BoardAdapter empty;
        vga_raster::RasterEngine partialEngine;
        vb::BoardModel partialBoard(partialEngine,
            vb::PinBinding::bind(vb::parseXdc(vga_raster::xdc(false)), partialEngine));
        vb::qt::BoardAdapter partial(&partialBoard);
        for (auto* target : {&adapter, &empty, &partial}) {
            QQmlEngine::setObjectOwnership(target, QQmlEngine::CppOwnership);
            QQmlApplicationEngine qml;
            QStringList warnings;
            QObject::connect(&qml, &QQmlEngine::warnings, &qml, [&](const QList<QQmlError>& errors) {
                for (const auto& error : errors) warnings.append(error.toString());
            });
            qml.setInitialProperties({{QStringLiteral("board"), QVariant::fromValue(target)}});
            qml.load(QUrl::fromLocalFile(QStringLiteral(VB_QT_QML_DIR "/Main.qml")));
            QCOMPARE(qml.rootObjects().size(), 1);
            auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
            QVERIFY(window && QTest::qWaitForWindowExposed(window));
            const auto find = [&](const char* name) {
                return boardUiFindItem(window->contentItem(), QString::fromLatin1(name));
            };
            if (target != &adapter) {
                // No VGA pins (or no design): no monitor, and the board is unchanged.
                QVERIFY(!find("vgaMonitor"));
                QVERIFY(!find("vgaMonitorLoader")->isVisible());
            } else {
                QTRY_VERIFY(find("vgaMonitor") && find("vgaMonitor")->isVisible());
                board.tick(vga_raster::frameStamp(0) + 1);
                QVERIFY(adapter.refresh());
                QVERIFY(!board.vgaOk());
                QTRY_VERIFY(find("vgaStatus")->isVisible());
                QVERIFY(find("vgaStatus")->property("text").toString().contains(
                    QStringLiteral("hsync appears active-HIGH")));
                // The next clean frame clears the diagnosis.
                engine.invertHsync = false;
                board.tick(vga_raster::frameStamp(2) + 1 - board.now());
                QVERIFY(adapter.refresh());
                QVERIFY(board.vgaOk());
                QTRY_VERIFY(!find("vgaStatus")->isVisible());
                auto* monitorDisplay = find("vgaDisplay");
                auto* flick = find("boardFlickable");
                const QRectF clip = flick->mapRectToScene(QRectF(0, 0, flick->width(), flick->height()));
                QTRY_VERIFY2(compareDisplay(window, monitorDisplay, rasterPixels(2), clip).isEmpty(),
                             qPrintable(compareDisplay(window, monitorDisplay, rasterPixels(2), clip)));
            }
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        }
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    VgaUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_vga_ui.moc"
