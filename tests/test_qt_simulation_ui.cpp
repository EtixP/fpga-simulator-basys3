// Run the actual simulation controls against the counter RTL. Deterministic
// controller clocks test presentation; one case exercises the real Qt timer.
#include "Vcounter.h"
#include "qt_board_ui_fixture.h"

#include <QStyleHints>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

class SimulationUiTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<BoardUiFixture<Vcounter>> ui_;
    std::unique_ptr<vb::qt::SimulationController> controller_;
    qint64 wallNanoseconds_ = 0;

    QString text(const char* name) const {
        const auto* item = ui_->item(name);
        return item ? item->property("text").toString() : QString();
    }

    uint16_t displayedLeds() const {
        uint16_t result = 0;
        for (int bit = 0; bit < 16; ++bit) {
            const auto name = QByteArray("led") + QByteArray::number(bit);
            if (ui_->item(name.constData())->property("active").toBool())
                result |= uint16_t(1u << bit);
        }
        return result;
    }

    bool load(bool automatic = false) {
        ui_->closeShell();
        controller_.reset();
        if (automatic) {
            controller_ = std::make_unique<vb::qt::SimulationController>(
                ui_->adapter, QStringLiteral("Counter"));
        } else {
            vb::qt::SimulationController::Options options;
            options.automaticScheduling = false;
            options.nowNanoseconds = [this] { return wallNanoseconds_; };
            controller_ = std::make_unique<vb::qt::SimulationController>(
                ui_->adapter, QStringLiteral("Counter"), std::move(options));
        }
        return ui_->loadShell(controller_.get());
    }

    bool editStep(const QString& value) const {
        auto* spin = ui_->item("stepCycles");
        if (!spin || !spin->isEnabled()) return false;
        auto* editor = spin->property("contentItem").value<QQuickItem*>();
        if (!editor) return false;
        editor->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(ui_->window, Qt::Key_A, Qt::ControlModifier);
        for (const auto character : value)
            QTest::keyClick(ui_->window, Qt::Key(int(Qt::Key_0) + character.digitValue()));
        QTest::keyClick(ui_->window, Qt::Key_Return);
        return spin->property("value").toInt() == value.toInt();
    }

    QString geometryProblem() const {
        const QRectF windowBounds(0, 0, ui_->window->width(), ui_->window->height());
        QRectF previous;
        for (const char* name : {"runPauseButton", "stepButton", "stepCycles", "resetButton", "pacingMode"}) {
            auto* item = ui_->item(name);
            if (!item || !item->isVisible() || item->width() <= 0 || item->height() <= 0)
                return QStringLiteral("Missing or zero-sized simulation control: %1").arg(QString::fromLatin1(name));
            const auto rectangle = ui_->bounds(item);
            if (!windowBounds.contains(rectangle))
                return QStringLiteral("Simulation control outside window: %1").arg(QString::fromLatin1(name));
            if (!previous.isNull() && rectangle.left() < previous.right())
                return QStringLiteral("Overlapping simulation controls: %1").arg(QString::fromLatin1(name));
            previous = rectangle;
        }
        previous = {};
        for (const char* name : {"simulationStatus", "simulationCycles", "simulationTime", "simulationSpeed"}) {
            auto* item = ui_->item(name);
            if (!item || !item->isVisible())
                return QStringLiteral("Missing simulation metric: %1").arg(QString::fromLatin1(name));
            const auto rectangle = ui_->bounds(item);
            if (!windowBounds.contains(rectangle) || (!previous.isNull() && rectangle.left() < previous.right()))
                return QStringLiteral("Clipped or overlapping metric: %1").arg(QString::fromLatin1(name));
            if (item->property("contentWidth").toReal() > item->width() + 1)
                return QStringLiteral("Metric text does not fit: %1").arg(QString::fromLatin1(name));
            previous = rectangle;
        }
        return {};
    }

private slots:
    void init() {
        wallNanoseconds_ = 0;
        ui_ = std::make_unique<BoardUiFixture<Vcounter>>(
            "counter", VB_SOURCE_DIR "/examples/counter.xdc");
        QVERIFY2(load(), qPrintable(ui_->warnings.join('\n')));
        QTRY_VERIFY(ui_->item("simulationToolbar") && ui_->item("simulationToolbar")->isVisible());
        QTRY_VERIFY2(geometryProblem().isEmpty(), qPrintable(geometryProblem()));
    }

    void cleanup() {
        if (!ui_) return;
        if (controller_) controller_->pause();
        ui_->closeShell();
        controller_.reset();
        QVERIFY(!ui_->board.buttonState(vb::Button::C));
        QVERIFY2(ui_->warnings.isEmpty(), qPrintable(ui_->warnings.join('\n')));
        ui_.reset();
    }

    void initialStateAndExactStepThroughKeyboardAndMouse() {
        QCOMPARE(text("projectDesignName"), QStringLiteral("Counter"));
        QCOMPARE(text("projectDesignFiles"), QStringLiteral("examples/counter.v\nexamples/counter.xdc"));
        QCOMPARE(text("runPauseButton"), QStringLiteral("Run"));
        QVERIFY(text("simulationStatus").contains(QStringLiteral("Paused")));
        QCOMPARE(text("simulationCycles"), QStringLiteral("0 cycles"));
        QCOMPARE(text("simulationTime"), QStringLiteral("0.000000000 s"));
        QCOMPARE(text("simulationSpeed"), QStringLiteral("Speed —"));
        QVERIFY(!ui_->item("simulationError")->isVisible());
        QCOMPARE(ui_->item("stepCycles")->property("value").toInt(), 1);
        QVERIFY(ui_->click("sw0"));
        QVERIFY(ui_->keyActivate("stepButton"));
        QCOMPARE(ui_->board.now(), uint64_t{1});
        QCOMPARE(displayedLeds(), uint16_t{1});
        QCOMPARE(text("simulationCycles"), QStringLiteral("1 cycles"));
        QCOMPARE(text("simulationTime"), QStringLiteral("0.000000010 s"));
        QVERIFY(editStep(QStringLiteral("31")));
        QVERIFY(ui_->click("stepButton"));
        QCOMPARE(ui_->board.now(), uint64_t{32});
        QCOMPARE(displayedLeds(), uint16_t{32});
        QCOMPARE(text("simulationCycles"), QStringLiteral("32 cycles"));
        QCOMPARE(text("simulationTime"), QStringLiteral("0.000000320 s"));
        QTest::qWait(20);
        QCOMPARE(ui_->board.now(), uint64_t{32});
        QVERIFY(ui_->capture("simulation-counter-paused"));
    }

    void runPauseStateAndMeasuredSpeedAreAuthoritative() {
        QVERIFY(ui_->click("sw0"));
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(controller_->running());
        QCOMPARE(text("runPauseButton"), QStringLiteral("Pause"));
        QVERIFY(!ui_->item("stepButton")->isEnabled());
        QVERIFY(!ui_->item("stepCycles")->isEnabled());
        QVERIFY(ui_->item("resetButton")->isEnabled());
        QCOMPARE(text("simulationSpeed"), QStringLiteral("Measuring speed…"));

        // 100k cycles per 5ms =20MHz=0.20x, independent of host execution speed.
        for (int batch = 0; batch < 52; ++batch) {
            wallNanoseconds_ += 5'000'000;
            controller_->processBatch();
        }
        QCOMPARE(ui_->board.now(), uint64_t{5'200'000});
        QVERIFY(controller_->speedAvailable());
        QCOMPARE(text("simulationSpeed"), QStringLiteral("20.00 MHz · 0.20× real-time"));
        QCOMPARE(text("simulationCycles"), QStringLiteral("5200000 cycles"));
        QCOMPARE(text("simulationTime"), QStringLiteral("0.052000000 s"));
        QCOMPARE(displayedLeds(), uint16_t(5'200'000u & 0xffffu));

        QVERIFY(ui_->keyActivate("runPauseButton"));
        QVERIFY(!controller_->running());
        QCOMPARE(text("runPauseButton"), QStringLiteral("Run"));
        QVERIFY(ui_->item("stepButton")->isEnabled());
        QVERIFY(ui_->item("stepCycles")->isEnabled());
        QCOMPARE(text("simulationSpeed"), QStringLiteral("Speed —"));
        const auto pausedCycle = ui_->board.now();
        controller_->processBatch(); // a stale queued callback cannot step
        QTest::qWait(20);
        QCOMPARE(ui_->board.now(), pausedCycle);
    }

    void actualTimerRunAndPauseControlSimulation() {
        QVERIFY2(load(true), qPrintable(ui_->warnings.join('\n')));
        QVERIFY(ui_->click("sw0"));
        QVERIFY(ui_->click("runPauseButton"));
        QTRY_VERIFY(ui_->board.now() > 0);
        QTRY_VERIFY(controller_->cycleText() != QStringLiteral("0"));
        QTRY_VERIFY(controller_->speedAvailable());
        QVERIFY(ui_->capture("simulation-counter-running"));
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(!controller_->running());
        const auto pausedCycle = ui_->board.now();
        QVERIFY(pausedCycle > 0);
        QTest::qWait(30);
        QCOMPARE(ui_->board.now(), pausedCycle);
        QCOMPARE(controller_->cycleText(), QString::number(pausedCycle));
        QVERIFY(ui_->click("stepButton"));
        QCOMPARE(ui_->board.now(), pausedCycle + 1);
    }

    void resetPausesAndPulsesWithoutRewindingTimeOrSwitches() {
        QVERIFY(ui_->click("sw0"));
        QVERIFY(editStep(QStringLiteral("31")));
        QVERIFY(ui_->click("stepButton"));
        QCOMPARE(displayedLeds(), uint16_t{31});
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(controller_->running());
        QVERIFY(ui_->click("resetButton"));
        QVERIFY(!controller_->running());
        QCOMPARE(ui_->board.now(), uint64_t{47});
        QCOMPARE(displayedLeds(), uint16_t{0});
        QVERIFY(ui_->board.switchState(0));
        QVERIFY(!ui_->board.buttonState(vb::Button::C));
        QCOMPARE(text("simulationCycles"), QStringLiteral("47 cycles"));
        QCOMPARE(text("simulationTime"), QStringLiteral("0.000000470 s"));
        QVERIFY(ui_->click("stepButton"));
        QCOMPARE(ui_->board.now(), uint64_t{78});
        QCOMPARE(displayedLeds(), uint16_t{31});

        // A command issued while a physical reset hold exists must preserve
        // that hold, allowing the original widget release to end it normally.
        QVERIFY(ui_->press("button0"));
        QVERIFY(ui_->board.buttonState(vb::Button::C));
        QVERIFY(controller_->reset());
        QCOMPARE(ui_->board.now(), uint64_t{94});
        QVERIFY(ui_->board.buttonState(vb::Button::C));
        QVERIFY(ui_->release("button0"));
        QVERIFY(!ui_->board.buttonState(vb::Button::C));
    }

    void pacingSelectorFollowsUserAndControllerState() {
        auto* pacing = ui_->item("pacingMode");
        QCOMPARE(pacing->property("currentIndex").toInt(), 0);
        pacing->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(ui_->window, Qt::Key_Down);
        QTRY_VERIFY(controller_->realtime());
        QCOMPARE(pacing->property("currentIndex").toInt(), 1);
        QCOMPARE(pacing->property("currentText").toString(), QStringLiteral("1× real-time target"));
        QCOMPARE(ui_->board.now(), uint64_t{0});
        controller_->setRealtime(false);
        QTRY_COMPARE(pacing->property("currentIndex").toInt(), 0);
        QVERIFY(ui_->keyActivate("runPauseButton"));
        QVERIFY(controller_->running());
        pacing->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(ui_->window, Qt::Key_Down);
        QTRY_VERIFY(controller_->realtime());
        QCOMPARE(text("simulationSpeed"), QStringLiteral("Measuring speed…"));
        QCOMPARE(ui_->board.now(), uint64_t{0});
    }

    void minimumGeometryAndConnectedPlaceholderMessages() {
        ui_->window->resize(960, 640);
        QTRY_COMPARE(ui_->window->size(), QSize(960, 640));
        controller_->setRealtime(true);
        QTRY_VERIFY2(geometryProblem().isEmpty(), qPrintable(geometryProblem()));
        // Native window resize and SplitView polish complete asynchronously.
        // Wait for the output tab's actual scene bounds before clicking it.
        QTRY_VERIFY(ui_->reveal(ui_->item("uartTab")));
        QVERIFY(ui_->click("uartTab"));
        QCOMPARE(text("outputTitle"), QStringLiteral("UART terminal unavailable"));
        QVERIFY(ui_->click("logsTab"));
        QCOMPARE(text("outputTitle"), QStringLiteral("Log view unavailable"));
        QVERIFY(ui_->click("overviewNav"));
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(controller_->running());
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(!controller_->running());
        QVERIFY(ui_->click("boardNav"));
        QCOMPARE(ui_->board.now(), uint64_t{0});
        QVERIFY(ui_->capture("simulation-counter-minimum"));
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    SimulationUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_simulation_ui.moc"
