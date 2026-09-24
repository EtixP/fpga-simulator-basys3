// Drive the Qt UART terminal with actual mouse/keyboard/wheel events against
// the unchanged uart_echo RTL. The board's decoded bytes and structured log
// are the oracle: the terminal must mirror them exactly, in order.
#include "Vuart_echo.h"
#include "qt_board_ui_fixture.h"

#include <QStyleHints>
#include <QWheelEvent>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
constexpr uint64_t kFrame = 10 * vb::kUartCyclesPerBit;

// Two distinct printable characters per index, for identifiable rows.
QString label(int index) {
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return QString(QChar::fromLatin1(digits[index / 36])) + QChar::fromLatin1(digits[index % 36]);
}

// A design exposing one or both serial pins; the TX line idles high.
class SerialPinsEngine final : public vb::SimEngine {
public:
    vb::SignalId lookup(std::string_view name) override {
        for (size_t i = 0; i < ports_.size(); ++i)
            if (ports_[i].name == name) return vb::SignalId(i);
        return vb::kNoSignal;
    }
    vb::SignalInfo info(vb::SignalId id) const override { return ports_.at(size_t(id)); }
    std::vector<vb::SignalInfo> ports() const override { return ports_; }
    void step(uint64_t cycles) override { now_ += cycles; }
    uint64_t now() const override { return now_; }
    uint64_t peek(vb::SignalId id) override { return size_t(id) == 1 ? 1 : rx_; }
    void poke(vb::SignalId id, uint64_t value) override {
        if (size_t(id) != 0) throw std::invalid_argument("not an input");
        rx_ = value & 1;
    }
    void setTraceFile(std::string_view) override {}
    void trace(bool) override {}

private:
    uint64_t now_ = 0;
    uint64_t rx_ = 0;
    const std::vector<vb::SignalInfo> ports_{{"serial_in", 1, true}, {"serial_out", 1, false}};
};
}

class UartUiTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<BoardUiFixture<Vuart_echo>> ui_;
    std::unique_ptr<vb::qt::SimulationController> controller_;
    qint64 wallNanoseconds_ = 0;
    quint64 wheelTimestamp_ = 1'000;

    vb::qt::UartConsoleModel& uart() const { return *ui_->adapter.uart(); }

    QString text(const char* name) const {
        const auto* item = ui_->item(name);
        return item ? item->property("text").toString() : QString();
    }

    QVariant role(int row, int role) const {
        return uart().data(uart().index(row, 0), role);
    }

    // Exact bytes shown in rows of one direction, decoded from their hex.
    QByteArray shownBytes(vb::qt::UartConsoleModel::Kind kind) const {
        QByteArray bytes;
        for (int row = 0; row < uart().rowCount(); ++row) {
            if (role(row, vb::qt::UartConsoleModel::KindRole).toInt() != kind) continue;
            bytes += QByteArray::fromHex(
                role(row, vb::qt::UartConsoleModel::HexRole).toString().toLatin1());
        }
        return bytes;
    }

    QByteArray boardTx() const {
        const auto& bytes = ui_->board.uartTxBytes();
        return QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
    }

    bool hasLog(const std::string& line) const {
        const auto& log = ui_->board.structuredLog();
        return std::find(log.begin(), log.end(), line) != log.end();
    }

    // Launchers apply the Reset control once so the RTL receiver synchronizer
    // does not decode its power-on low level as a start bit.
    bool load(bool automatic = false) {
        ui_->closeShell();
        controller_.reset();
        if (automatic) {
            controller_ = std::make_unique<vb::qt::SimulationController>(
                ui_->adapter, QStringLiteral("UART Echo"));
        } else {
            vb::qt::SimulationController::Options options;
            options.automaticScheduling = false;
            options.nowNanoseconds = [this] { return wallNanoseconds_; };
            controller_ = std::make_unique<vb::qt::SimulationController>(
                ui_->adapter, QStringLiteral("UART Echo"), std::move(options));
        }
        if (ui_->board.now() == 0 && !controller_->reset()) return false;
        return ui_->loadShell(controller_.get(),
                              {{QStringLiteral("designSource"), QStringLiteral("uart_echo")}});
    }

    bool typeAndSend(const QString& value, bool button = false) {
        auto* input = ui_->item("uartInput");
        if (!ui_->click("uartInput")) return false;
        QTest::keyClick(ui_->window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(ui_->window, Qt::Key_Backspace);
        for (const QChar character : value) QTest::keyClick(ui_->window, character.toLatin1());
        if (input->property("text").toString() != value) return false;
        if (button) return ui_->click("uartSendButton");
        QTest::keyClick(ui_->window, Qt::Key_Return);
        return true;
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

    bool stepMillion() {
        if (ui_->item("stepCycles")->property("value").toInt() != 1'000'000
            && !editStep(QStringLiteral("1000000"))) return false;
        return ui_->click("stepButton");
    }

    void runBatch() {
        wallNanoseconds_ += 20'000'000;  // each batch publishes a snapshot
        controller_->processBatch();
    }

    // Captures park the pointer on the header logo so no hover tooltip shows.
    bool capture(const char* name) const {
        QTest::mouseMove(ui_->window, QPoint(8, 8));
        QTest::qWait(ui_->window->property("visible").toBool() ? 150 : 0);
        return ui_->capture(name);
    }

    QQuickItem* list() const { return ui_->item("uartLines"); }
    bool atEnd() const { return list()->property("atYEnd").toBool(); }

    // Flickable derives wheel velocity from event timestamps, so synthetic
    // events carry increasing ones like real input.
    void wheel(QQuickItem* target, int angle) {
        const QPointF position = ui_->bounds(target).center();
        QWheelEvent event(position, ui_->window->mapToGlobal(position), QPoint(),
                          QPoint(0, angle), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        wheelTimestamp_ += 500;
        event.setTimestamp(wheelTimestamp_);
        QGuiApplication::sendEvent(ui_->window, &event);
    }

    QString geometryProblem() const {
        const QRectF windowBounds(0, 0, ui_->window->width(), ui_->window->height());
        QRectF previous;
        for (const char* name : {"uartInput", "uartLineEnding", "uartSendButton", "uartStatus",
                                 "uartHexToggle", "uartClearButton"}) {
            auto* item = ui_->item(name);
            if (!item || !item->isVisible() || item->width() <= 0 || item->height() <= 0)
                return QStringLiteral("Missing or zero-sized terminal control: %1").arg(QLatin1String(name));
            const auto rectangle = ui_->bounds(item);
            if (!windowBounds.contains(rectangle))
                return QStringLiteral("Terminal control outside window: %1").arg(QLatin1String(name));
            if (!previous.isNull() && rectangle.left() < previous.right())
                return QStringLiteral("Overlapping terminal controls: %1").arg(QLatin1String(name));
            previous = rectangle;
        }
        const auto lines = ui_->bounds(list());
        if (lines.height() < 40 || !windowBounds.contains(lines))
            return QStringLiteral("Scrollback is clipped or too short");
        if (lines.bottom() > ui_->bounds(ui_->item("uartInput")).top())
            return QStringLiteral("Scrollback overlaps the input row");
        return {};
    }

private slots:
    void init() {
        wallNanoseconds_ = 0;
        ui_ = std::make_unique<BoardUiFixture<Vuart_echo>>(
            "uart_echo", VB_SOURCE_DIR "/examples/uart_echo.xdc");
        QVERIFY2(load(), qPrintable(ui_->warnings.join('\n')));
        QTRY_VERIFY(ui_->item("uartTerminal") && ui_->item("uartTerminal")->isVisible());
        QTRY_VERIFY2(geometryProblem().isEmpty(), qPrintable(geometryProblem()));
    }

    void cleanup() {
        if (!ui_) return;
        if (controller_) controller_->pause();
        ui_->closeShell();
        controller_.reset();
        QVERIFY(!ui_->board.buttonState(vb::Button::C));
        // The terminal never invents traffic: it mirrors the board exactly.
        QCOMPARE(uart().txBytes(), qint64(ui_->board.uartTxBytes().size()));
        QCOMPARE(uart().framingErrors(), qint64(ui_->board.uartTxFramingErrorCycles().size()));
        QVERIFY2(ui_->warnings.isEmpty(), qPrintable(ui_->warnings.join('\n')));
        ui_.reset();
    }

    void opensOnTerminalWithStartupResetAndEmptyStates() {
        QCOMPARE(ui_->window->property("outputIndex").toInt(), 1);
        QCOMPARE(text("projectDesignName"), QStringLiteral("UART Echo"));
        QCOMPARE(text("projectDesignFiles"), QStringLiteral("examples/uart_echo.v\nexamples/uart_echo.xdc"));
        QCOMPARE(text("simulationCycles"), QStringLiteral("16 cycles"));
        QVERIFY(hasLog("[cycle 0] BTNC 0->1") && hasLog("[cycle 16] BTNC 1->0"));
        QCOMPARE(uart().rowCount(), 1);
        QCOMPARE(role(0, vb::qt::UartConsoleModel::KindRole).toInt(), int(vb::qt::UartConsoleModel::Notice));
        QCOMPARE(text("uartLineText0"), QStringLiteral("Reset: BTNC held for 16 cycles"));
        QCOMPARE(role(0, vb::qt::UartConsoleModel::CycleRole).toString(), QStringLiteral("0"));
        QVERIFY(!ui_->item("uartEmptyState")->isVisible());
        QVERIFY(!ui_->item("uartSendButton")->isEnabled());  // nothing typed
        QVERIFY(ui_->item("uartInput")->isEnabled());
        QVERIFY(!ui_->item("uartSendError")->isVisible());
        QCOMPARE(text("uartStatus"), QStringLiteral("TX 0 B  ·  RX 0 B"));
        // Clearing an untouched terminal shows the no-traffic guidance.
        QVERIFY(ui_->click("uartClearButton"));
        QTRY_VERIFY(ui_->item("uartEmptyState")->isVisible());
        QCOMPARE(text("uartEmptyTitle"), QStringLiteral("No UART traffic yet"));
        QVERIFY(text("uartEmptyDetail").contains(QStringLiteral("press Send")));
        QVERIFY(!ui_->item("uartClearButton")->isEnabled());
        QCOMPARE(ui_->board.now(), uint64_t{16});
        // The other output tabs keep their placeholders; UART returns intact.
        QVERIFY(ui_->click("logsTab"));
        QCOMPARE(text("outputTitle"), QStringLiteral("Log view unavailable"));
        QVERIFY(!ui_->item("uartTerminal")->isVisible());
        QVERIFY(ui_->click("uartTab"));
        QTRY_VERIFY(ui_->item("uartTerminal")->isVisible());
        QVERIFY(ui_->click("restoreLayoutButton"));
        QTRY_COMPARE(ui_->window->property("outputIndex").toInt(), 1);
    }

    void typedTextEchoesExactlyWithCycleStamps() {
        QVERIFY(typeAndSend(QStringLiteral("hello")));
        QTRY_COMPARE(text("uartInput"), QString());
        QCOMPARE(uart().rowCount(), 2);
        QTRY_COMPARE(text("uartLineText1"), QStringLiteral("hello"));
        QCOMPARE(role(1, vb::qt::UartConsoleModel::CycleRole).toString(), QStringLiteral("16"));
        QCOMPARE(role(1, vb::qt::UartConsoleModel::LastCycleRole).toString(),
                 QString::number(16 + 4 * kFrame));
        QCOMPARE(text("uartStatus"), QStringLiteral("TX 0 B  ·  RX 5 B  ·  5 B queued"));
        QCOMPARE(ui_->board.now(), uint64_t{16});  // sending advances no time

        QVERIFY(stepMillion());
        QCOMPARE(ui_->board.now(), uint64_t{1'000'016});
        QCOMPARE(boardTx(), QByteArray("hello"));
        QCOMPARE(uart().rowCount(), 3);
        QTRY_COMPARE(text("uartLineText2"), QStringLiteral("hello"));
        const auto& stamps = ui_->board.uartTxByteCycles();
        QCOMPARE(role(2, vb::qt::UartConsoleModel::KindRole).toInt(), int(vb::qt::UartConsoleModel::Tx));
        QCOMPARE(role(2, vb::qt::UartConsoleModel::CycleRole).toString(), QString::number(stamps.front()));
        QCOMPARE(role(2, vb::qt::UartConsoleModel::LastCycleRole).toString(), QString::number(stamps.back()));
        // The same stamps appear in the frozen structured-log vocabulary.
        QVERIFY(hasLog("[cycle 16] UART RX 0x68 'h'"));
        QVERIFY(hasLog("[cycle " + std::to_string(stamps.front()) + "] UART TX 0x68 'h'"));
        QVERIFY(hasLog("[cycle " + std::to_string(stamps.back()) + "] UART TX 0x6F 'o'"));
        QCOMPARE(text("uartStatus"), QStringLiteral("TX 5 B  ·  RX 5 B"));

        QVERIFY(ui_->click("uartHexToggle"));
        QTRY_COMPARE(text("uartLineText2"), QStringLiteral("68 65 6C 6C 6F"));
        QCOMPARE(text("uartLineText0"), QStringLiteral("Reset: BTNC held for 16 cycles"));
        QVERIFY(capture("uart-terminal-hex"));
        QVERIFY(ui_->keyActivate("uartHexToggle"));
        QTRY_COMPARE(text("uartLineText2"), QStringLiteral("hello"));
        QVERIFY(capture("uart-terminal"));
    }

    void repeatedSendsSurvivePauseAndResume() {
        QVERIFY(typeAndSend(QStringLiteral("abc")));
        QVERIFY(typeAndSend(QStringLiteral("abc"), true));
        QVERIFY(typeAndSend(QStringLiteral("xyz")));
        QCOMPARE(uart().rowCount(), 4);
        for (int row = 1; row <= 3; ++row) {
            QCOMPARE(role(row, vb::qt::UartConsoleModel::KindRole).toInt(), int(vb::qt::UartConsoleModel::Rx));
            QCOMPARE(role(row, vb::qt::UartConsoleModel::CycleRole).toString(),
                     QString::number(16 + (row - 1) * 3 * kFrame));
        }
        QCOMPARE(uart().pendingRxBytes(), 9);

        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(controller_->running());
        while (ui_->board.uartTxBytes().size() < 4) runBatch();
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(!controller_->running());
        const auto paused = ui_->board.now();
        const auto partial = boardTx();
        QVERIFY(partial.size() >= 4 && partial.size() < 9);
        QCOMPARE(shownBytes(vb::qt::UartConsoleModel::Tx), partial);
        QCOMPARE(uart().rowCount(), 5);
        controller_->processBatch();  // a stale callback while paused is inert
        QTest::qWait(20);
        QCOMPARE(ui_->board.now(), paused);
        QCOMPARE(shownBytes(vb::qt::UartConsoleModel::Tx), partial);

        QVERIFY(ui_->keyActivate("runPauseButton"));
        QVERIFY(controller_->running());
        while (ui_->board.uartTxBytes().size() < 9 || uart().pendingRxBytes()) runBatch();
        QVERIFY(ui_->click("runPauseButton"));
        // Resumed output continues the same row with its original first stamp.
        QCOMPARE(uart().rowCount(), 5);
        QCOMPARE(boardTx(), QByteArray("abcabcxyz"));
        QCOMPARE(shownBytes(vb::qt::UartConsoleModel::Tx), QByteArray("abcabcxyz"));
        QCOMPARE(shownBytes(vb::qt::UartConsoleModel::Rx), QByteArray("abcabcxyz"));
        QTRY_COMPARE(text("uartLineText4"), QStringLiteral("abcabcxyz"));
        QCOMPARE(role(4, vb::qt::UartConsoleModel::CycleRole).toString(),
                 QString::number(ui_->board.uartTxByteCycles().front()));
        QCOMPARE(uart().pendingRxBytes(), 0);
    }

    void resetDuringTrafficKeepsOrderAndMirrorsBoard() {
        QVERIFY(typeAndSend(QStringLiteral("0123456789")));
        QVERIFY(editStep(QStringLiteral("300000")));
        QVERIFY(ui_->click("stepButton"));
        const auto pulse = ui_->board.now();
        QCOMPARE(pulse, uint64_t{300'016});
        const auto before = boardTx();
        QVERIFY(!before.isEmpty());
        QVERIFY(ui_->click("resetButton"));
        QCOMPARE(ui_->board.now(), pulse + 16);
        QVERIFY(hasLog("[cycle 300016] BTNC 0->1"));
        const int notice = uart().rowCount() - 1;
        QCOMPARE(role(notice, vb::qt::UartConsoleModel::KindRole).toInt(), int(vb::qt::UartConsoleModel::Notice));
        QCOMPARE(role(notice, vb::qt::UartConsoleModel::CycleRole).toString(), QStringLiteral("300016"));
        QCOMPARE(shownBytes(vb::qt::UartConsoleModel::Tx), before);
        // Queued host bytes continue through the physical reset pulse.
        QVERIFY(uart().pendingRxBytes() > 0);

        QVERIFY(stepMillion());
        QVERIFY(ui_->click("stepButton"));
        QCOMPARE(uart().pendingRxBytes(), 0);
        // Whatever the RTL echoes after discarding in-flight state, the
        // terminal shows exactly the board's bytes, on the correct side.
        QCOMPARE(shownBytes(vb::qt::UartConsoleModel::Tx), boardTx());
        for (int row = 0; row < uart().rowCount(); ++row) {
            if (role(row, vb::qt::UartConsoleModel::KindRole).toInt() != vb::qt::UartConsoleModel::Tx)
                continue;
            const auto first = role(row, vb::qt::UartConsoleModel::CycleRole).toString().toULongLong();
            const auto last = role(row, vb::qt::UartConsoleModel::LastCycleRole).toString().toULongLong();
            QVERIFY(row < notice ? last < pulse : first > pulse);
        }
        QVERIFY(capture("uart-terminal-reset"));
    }

    void lineEndingsScrollFollowAndClear() {
        auto* ending = ui_->item("uartLineEnding");
        ending->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(ui_->window, Qt::Key_Down);
        QTRY_COMPARE(ending->property("currentIndex").toInt(), 1);  // LF
        for (int line = 0; line < 20; ++line)
            QVERIFY(typeAndSend(QStringLiteral("L%1").arg(line, 2, 10, QLatin1Char('0'))));
        QCOMPARE(uart().rxBytes(), 80);
        QCOMPARE(role(1, vb::qt::UartConsoleModel::HexRole).toString(), QStringLiteral("4C 30 30 0A"));
        while (ui_->board.uartTxBytes().size() < 80) QVERIFY(stepMillion());
        // Echoed LF ends rows: one row per echoed line, text without the LF.
        QCOMPARE(uart().rowCount(), 41);
        QCOMPARE(role(40, vb::qt::UartConsoleModel::TextRole).toString(), QStringLiteral("L19"));
        QTRY_VERIFY(atEnd());
        QTRY_COMPARE(text("uartLineText40"), QStringLiteral("L19"));
        QVERIFY(!ui_->item("uartLatestButton")->isVisible());

        // Scrolling back stops following; new output does not move the view.
        wheel(list(), 360);
        QTRY_VERIFY(!list()->property("moving").toBool());
        QVERIFY(!atEnd());
        QVERIFY(!list()->property("following").toBool());
        QTRY_VERIFY(ui_->item("uartLatestButton")->isVisible());
        const qreal contentY = list()->property("contentY").toReal();
        QVERIFY(typeAndSend(QStringLiteral("more")));
        QVERIFY(stepMillion());
        QCOMPARE(uart().rowCount(), 43);
        QTest::qWait(50);
        // Sub-pixel settling only; the view must not jump toward the end.
        QVERIFY(qAbs(list()->property("contentY").toReal() - contentY) < 1.0);
        QVERIFY(!atEnd());
        QVERIFY(ui_->item("uartLatestButton")->isVisible());
        QVERIFY(ui_->click("uartLatestButton"));
        QTRY_VERIFY(atEnd());
        QTRY_VERIFY(!ui_->item("uartLatestButton")->isVisible());
        QTRY_COMPARE(text("uartLineText42"), QStringLiteral("more"));
        // Scrolling back to the end resumes following. Wheel scrolling is
        // animated, so each gesture settles before the next, like real input.
        wheel(list(), 240);
        QTRY_VERIFY(!list()->property("moving").toBool());
        QVERIFY(!atEnd());
        QVERIFY(!list()->property("following").toBool());
        wheel(list(), -2400);
        QTRY_VERIFY(!list()->property("moving").toBool());
        QVERIFY(atEnd());
        QVERIFY(list()->property("following").toBool());
        QVERIFY(!ui_->item("uartLatestButton")->isVisible());
        // Dragging the scroll bar handle away from the end also stops following.
        auto* bar = ui_->item("uartScrollBar");
        QTRY_VERIFY(bar->isVisible() && bar->width() > 0);
        const auto track = ui_->bounds(bar);
        const QPoint grip(qRound(track.center().x()), qRound(track.bottom() - 6));
        QTest::mousePress(ui_->window, Qt::LeftButton, Qt::NoModifier, grip);
        QTest::mouseMove(ui_->window, grip - QPoint(0, 30), 20);
        QVERIFY(!list()->property("following").toBool());  // not while held
        QTest::mouseRelease(ui_->window, Qt::LeftButton, Qt::NoModifier, grip - QPoint(0, 30));
        QTRY_VERIFY(!atEnd());
        QVERIFY(!list()->property("following").toBool());
        QTRY_VERIFY(ui_->item("uartLatestButton")->isVisible());
        QVERIFY(ui_->click("uartLatestButton"));
        QTRY_VERIFY(atEnd());

        const auto boardBytes = boardTx();
        const auto now = ui_->board.now();
        QVERIFY(ui_->click("uartClearButton"));
        QTRY_VERIFY(ui_->item("uartEmptyState")->isVisible());
        QCOMPARE(uart().rowCount(), 0);
        QCOMPARE(text("uartEmptyTitle"), QStringLiteral("Scrollback cleared"));
        QCOMPARE(uart().txBytes(), qint64(boardBytes.size()));
        QCOMPARE(boardTx(), boardBytes);
        QCOMPARE(ui_->board.now(), now);
        QVERIFY(typeAndSend(QStringLiteral("z")));
        QTRY_VERIFY(!ui_->item("uartEmptyState")->isVisible());
        QCOMPARE(text("uartLineText0"), QStringLiteral("z"));
    }

    void boundedQueueErrorIsExplainedAndRecovers() {
        auto* input = ui_->item("uartInput");
        QCOMPARE(input->property("maximumLength").toInt(), 4096);
        input->setProperty("text", QString(4096, QLatin1Char('a')));
        QVERIFY(ui_->click("uartSendButton"));
        QTRY_COMPARE(text("uartInput"), QString());
        QCOMPARE(uart().pendingRxBytes(), 4096);
        QVERIFY(typeAndSend(QStringLiteral("b")));
        QTRY_VERIFY(ui_->item("uartSendError")->isVisible());
        QVERIFY(text("uartSendError").startsWith(QStringLiteral("Not sent: needs 1 B")));
        // The message shrinks the scrollback; a following view stays at the end.
        QVERIFY(list()->property("following").toBool());
        QTRY_VERIFY(atEnd());
        QCOMPARE(text("uartInput"), QStringLiteral("b"));  // kept for retry
        QCOMPARE(uart().rxBytes(), 4096);
        QVERIFY(capture("uart-terminal-queue-full"));
        QVERIFY(stepMillion());
        QVERIFY(ui_->click("uartSendButton"));
        QTRY_VERIFY(!ui_->item("uartSendError")->isVisible());
        QCOMPARE(text("uartInput"), QString());
        QCOMPARE(uart().rxBytes(), 4097);
    }

    void followsNewestRowAcrossTheRowLimit() {
        // 1100 terminated rows in one send: a single multi-row trim batch.
        QString lines;
        for (int line = 0; line < 1100; ++line) lines += label(line) + QLatin1Char('\n');
        QVERIFY(ui_->adapter.sendUartText(lines));  // the adapter call QML makes
        QCOMPARE(uart().rowCount(), 1000);
        QCOMPARE(uart().trimmedLines(), qint64(101));  // includes the reset notice
        QTRY_VERIFY(ui_->item("uartTrimmedNotice")->isVisible());
        QCOMPARE(text("uartTrimmedNotice"),
                 QStringLiteral("101 earlier lines discarded. Scrollback keeps the latest 1000 lines."));
        const auto newestVisible = [this](const QString& expected) {
            auto* newest = ui_->item("uartLineText999");
            return atEnd() && newest && newest->property("text").toString() == expected
                && ui_->bounds(list()).contains(ui_->bounds(newest).center());
        };
        QTRY_VERIFY(newestVisible(label(1099)));
        // At the limit an insert and a trim keep count and height constant;
        // the view must still follow the new last row.
        QVERIFY(typeAndSend(QStringLiteral("ZZ")));
        QCOMPARE(uart().rowCount(), 1000);
        QCOMPARE(uart().trimmedLines(), qint64(102));
        QTRY_VERIFY(newestVisible(QStringLiteral("ZZ")));
        QVERIFY(stepMillion());  // echoed TX rows also arrive at the limit
        QCOMPARE(role(999, vb::qt::UartConsoleModel::KindRole).toInt(), int(vb::qt::UartConsoleModel::Tx));
        QTRY_VERIFY(newestVisible(role(999, vb::qt::UartConsoleModel::TextRole).toString()));
        QVERIFY(!ui_->item("uartLatestButton")->isVisible());
        QVERIFY(capture("uart-terminal-limit"));

        // Keyboard scrolling: PageUp leaves the end and stops following;
        // End returns and resumes.
        list()->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(list()->hasActiveFocus());
        QTest::keyClick(ui_->window, Qt::Key_PageUp);
        QTRY_VERIFY(!atEnd());
        QVERIFY(!list()->property("following").toBool());
        QTRY_VERIFY(ui_->item("uartLatestButton")->isVisible());
        QTest::keyClick(ui_->window, Qt::Key_Down);
        QVERIFY(!list()->property("following").toBool());
        QTest::keyClick(ui_->window, Qt::Key_End);
        QTRY_VERIFY(atEnd());
        QVERIFY(list()->property("following").toBool());
        QTest::keyClick(ui_->window, Qt::Key_Home);
        QTRY_COMPARE(text("uartLineText0"), role(0, vb::qt::UartConsoleModel::TextRole).toString());
        QVERIFY(!list()->property("following").toBool());
    }

    void carriageReturnLineEndingsSendExactBytes() {
        auto* ending = ui_->item("uartLineEnding");
        ending->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(ui_->window, Qt::Key_Down);
        QTest::keyClick(ui_->window, Qt::Key_Down);
        QTRY_COMPARE(ending->property("currentIndex").toInt(), 2);  // CR
        QVERIFY(typeAndSend(QStringLiteral("cr")));
        QCOMPARE(role(1, vb::qt::UartConsoleModel::HexRole).toString(), QStringLiteral("63 72 0D"));
        QCOMPARE(role(1, vb::qt::UartConsoleModel::TextRole).toString(), QStringLiteral("cr\\r"));
        ending->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(ui_->window, Qt::Key_Down);
        QTRY_COMPARE(ending->property("currentIndex").toInt(), 3);  // CR+LF
        QVERIFY(typeAndSend(QStringLiteral("crlf")));
        QCOMPARE(role(2, vb::qt::UartConsoleModel::HexRole).toString(),
                 QStringLiteral("63 72 6C 66 0D 0A"));
        QCOMPARE(role(2, vb::qt::UartConsoleModel::TextRole).toString(), QStringLiteral("crlf"));
        while (ui_->board.uartTxBytes().size() < 9) QVERIFY(stepMillion());
        QCOMPARE(boardTx(), QByteArray("cr\rcrlf\r\n"));
        QCOMPARE(shownBytes(vb::qt::UartConsoleModel::Tx), boardTx());
    }

    void minimumWindowKeepsTerminalUsable() {
        ui_->window->resize(960, 640);
        QTRY_COMPARE(ui_->window->size(), QSize(960, 640));
        QTRY_VERIFY2(geometryProblem().isEmpty(), qPrintable(geometryProblem()));
        QVERIFY(typeAndSend(QStringLiteral("min")));
        QVERIFY(stepMillion());
        QTRY_COMPARE(shownBytes(vb::qt::UartConsoleModel::Tx), QByteArray("min"));
        QVERIFY(capture("uart-terminal-minimum"));
    }

    void actualTimerRunEchoes() {
        QVERIFY2(load(true), qPrintable(ui_->warnings.join('\n')));
        QTRY_VERIFY(ui_->item("uartTerminal") && ui_->item("uartTerminal")->isVisible());
        QVERIFY(typeAndSend(QStringLiteral("timer")));
        QVERIFY(ui_->click("runPauseButton"));
        QTRY_VERIFY_WITH_TIMEOUT(ui_->board.uartTxBytes().size() == 5, 20'000);
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(!controller_->running());
        QTRY_COMPARE(shownBytes(vb::qt::UartConsoleModel::Tx), QByteArray("timer"));
    }

    void singlePinDesignsExplainWhatIsAvailable() {
        for (bool txOnly : {true, false}) {
            SerialPinsEngine engine;
            const auto xdc = txOnly ? "set_property PACKAGE_PIN A18 [get_ports serial_out]\n"
                                    : "set_property PACKAGE_PIN B18 [get_ports serial_in]\n";
            vb::BoardModel board(engine, vb::PinBinding::bind(vb::parseXdc(xdc), engine));
            vb::qt::BoardAdapter adapter(&board);
            QQmlEngine::setObjectOwnership(&adapter, QQmlEngine::CppOwnership);
            QQmlApplicationEngine qml;
            QStringList warnings;
            QObject::connect(&qml, &QQmlEngine::warnings, &qml, [&](const QList<QQmlError>& errors) {
                for (const auto& error : errors) warnings.append(error.toString());
            });
            qml.setInitialProperties({{QStringLiteral("board"), QVariant::fromValue(&adapter)}});
            qml.load(QUrl::fromLocalFile(QStringLiteral(VB_QT_QML_DIR "/Main.qml")));
            QCOMPARE(qml.rootObjects().size(), 1);
            auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
            QVERIFY(window && QTest::qWaitForWindowExposed(window));
            const auto find = [&](const char* name) {
                return boardUiFindItem(window->contentItem(), QString::fromLatin1(name));
            };
            QCOMPARE(window->property("outputIndex").toInt(), 1);
            QTRY_VERIFY(find("uartTerminal")->isVisible());
            QCOMPARE(find("uartEmptyTitle")->property("text").toString(),
                     QStringLiteral("No UART traffic yet"));
            const QString detail = find("uartEmptyDetail")->property("text").toString();
            QCOMPARE(find("uartInput")->isEnabled(), !txOnly);
            QCOMPARE(find("uartLineEnding")->isEnabled(), !txOnly);
            QVERIFY(!find("uartSendButton")->isEnabled());
            if (txOnly) {
                QVERIFY(detail.contains(QStringLiteral("does not bind RsRx (B18)")));
                QCOMPARE(find("uartInput")->property("placeholderText").toString(),
                         QStringLiteral("RsRx (B18) is not bound in this design"));
                QCOMPARE(find("uartStatus")->property("text").toString(), QStringLiteral("TX 0 B"));
            } else {
                QVERIFY(detail.contains(QStringLiteral("does not bind RsTx (A18)")));
                QCOMPARE(find("uartStatus")->property("text").toString(), QStringLiteral("RX 0 B"));
            }
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        }
    }

    void unavailableStatesExplainWhy() {
        for (bool missing : {false, true}) {
            vb::qt::BoardAdapter empty;
            QQmlEngine::setObjectOwnership(&empty, QQmlEngine::CppOwnership);
            QQmlApplicationEngine qml;
            QStringList warnings;
            QObject::connect(&qml, &QQmlEngine::warnings, &qml, [&](const QList<QQmlError>& errors) {
                for (const auto& error : errors) warnings.append(error.toString());
            });
            qml.setInitialProperties({{QStringLiteral("board"),
                                       QVariant::fromValue(missing ? nullptr : &empty)}});
            qml.load(QUrl::fromLocalFile(QStringLiteral(VB_QT_QML_DIR "/Main.qml")));
            QCOMPARE(qml.rootObjects().size(), 1);
            auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
            QVERIFY(window && QTest::qWaitForWindowExposed(window));
            QCOMPARE(window->property("outputIndex").toInt(), 0);
            auto* tab = boardUiFindItem(window->contentItem(), QStringLiteral("uartTab"));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              tab->mapRectToScene(QRectF(0, 0, tab->width(), tab->height())).center().toPoint());
            QTRY_COMPARE(window->property("outputIndex").toInt(), 1);
            auto* title = boardUiFindItem(window->contentItem(), QStringLiteral("outputTitle"));
            auto* detail = boardUiFindItem(window->contentItem(), QStringLiteral("outputDetail"));
            QCOMPARE(title->property("text").toString(), QStringLiteral("UART terminal unavailable"));
            QVERIFY(detail->property("text").toString().contains(
                missing ? QStringLiteral("could not be initialized") : QStringLiteral("No design is loaded")));
            QVERIFY(!boardUiFindItem(window->contentItem(), QStringLiteral("uartTerminal"))->isVisible());
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        }
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    UartUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_uart_ui.moc"
