#include "Vstopwatch.h"
#include "qt_board_ui_fixture.h"

#include <QStyleHints>
#include <QtQml/qqmlextensionplugin.h>

#include <array>
#include <limits>
#include <sstream>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

class StopwatchBoardUiTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<BoardUiFixture<Vstopwatch>> ui_;

    void runTo(uint64_t cycle) {
        QVERIFY(cycle >= ui_->board.now());
        ui_->board.tick(cycle - ui_->board.now());
        QVERIFY(ui_->adapter.refresh());
        QCOMPARE(ui_->board.now(), cycle);
    }

    QString displayMismatch(const std::array<char, 4>& characters,
                            const std::array<int, 4>& masks) const {
        qreal previousX = std::numeric_limits<qreal>::max();
        for (int index = 0; index < 4; ++index) {
            const QByteArray name = QByteArray("digit") + QByteArray::number(index);
            auto* digit = ui_->item(name.constData());
            if (!digit) return QStringLiteral("Missing digit %1").arg(index);
            if (digit->property("digit").toInt() != index)
                return QStringLiteral("Wrong physical anode on digit %1").arg(index);
            if (digit->property("character").toString() != QLatin1Char(characters[index]) ||
                digit->property("segments").toInt() != masks[index])
                return QStringLiteral("Digit %1 does not show expected '%2'/mask %3")
                    .arg(index).arg(QLatin1Char(characters[index])).arg(masks[index], 0, 16);
            if (digit->property("decimalPoint").toBool() != (index == 2))
                return QStringLiteral("SS.CC decimal point is wrong on AN%1").arg(index);
            const qreal x = ui_->bounds(digit).center().x();
            if (x >= previousX)
                return QStringLiteral("Physical order must be AN3 AN2 AN1 AN0 from left to right");
            previousX = x;
            // Inspect the rendered segment state as well as delegate inputs.
            // Masks are independent constants for decimal glyphs, not a copy
            // of the backend's decoder or values read back from the adapter.
            for (int segment = 0; segment < 7; ++segment) {
                auto* shape = boardUiFindItem(digit, QStringLiteral("segment%1").arg(segment));
                if (!shape || shape->property("lit").toBool() != bool(masks[index] & (1 << segment)))
                    return QStringLiteral("Incorrect lit geometry at AN%1 segment %2").arg(index).arg(segment);
            }
            auto* point = boardUiFindItem(digit, QStringLiteral("decimalPoint"));
            if (!point || point->property("lit").toBool() != (index == 2))
                return QStringLiteral("Incorrect decimal-point geometry at AN%1").arg(index);
        }
        return {};
    }

private slots:
    void init() {
        ui_ = std::make_unique<BoardUiFixture<Vstopwatch>>(
            "stopwatch", VB_SOURCE_DIR "/examples/stopwatch.xdc");
        QVERIFY2(ui_->loadShell(), qPrintable(ui_->warnings.join('\n')));
        QTRY_VERIFY(ui_->item("button1"));
        QCOMPARE(ui_->board.now(), uint64_t(0));
    }

    void cleanup() {
        if (!ui_) return;
        const uint64_t cycle = ui_->board.now();
        ui_->closeShell();
        QCOMPARE(ui_->board.now(), cycle);
        QVERIFY(!ui_->board.buttonState(vb::Button::C));
        QVERIFY(!ui_->board.buttonState(vb::Button::U));
        QVERIFY2(ui_->warnings.isEmpty(), qPrintable(ui_->warnings.join('\n')));
        ui_.reset();
    }

    void qmlPressesReplayUnchangedThreeSecondGolden() {
        QVERIFY(ui_->item("button0")->property("available").toBool());
        QVERIFY(ui_->item("button1")->property("available").toBool());
        QVERIFY(!ui_->item("button2")->isEnabled());
        QVERIFY(!ui_->item("sw0")->isEnabled());
        QVERIFY(ui_->item("led0")->property("available").toBool());
        QVERIFY(!ui_->item("led1")->property("available").toBool());

        // This is exactly the established golden's stimulus timeline. Only
        // its input delivery changes: every press/release is a QML mouse
        // event; no test calls the adapter's or board's input setters.
        runTo(100'000);
        QVERIFY(ui_->press("button1"));
        QTRY_VERIFY(ui_->board.buttonState(vb::Button::U));
        runTo(600'000);
        QVERIFY(ui_->release("button1"));
        QTRY_VERIFY(!ui_->board.buttonState(vb::Button::U));
        runTo(1'700'000);
        QVERIFY(!ui_->board.ledState(0)); // short press is correctly debounced away
        const std::array<char, 4> zeroes{'0', '0', '0', '0'};
        const std::array<int, 4> zeroMasks{0x3f, 0x3f, 0x3f, 0x3f};
        QTRY_VERIFY2(displayMismatch(zeroes, zeroMasks).isEmpty(),
                     qPrintable(displayMismatch(zeroes, zeroMasks)));
        QVERIFY(ui_->board.digitLastLit(2) < ui_->board.now()); // fused, not current-anode-only

        runTo(2'000'000);
        QVERIFY(ui_->press("button1"));
        QTRY_VERIFY(ui_->board.buttonState(vb::Button::U));
        runTo(4'000'000);
        QVERIFY(ui_->release("button1"));
        QTRY_VERIFY(!ui_->board.buttonState(vb::Button::U));
        runTo(155'000'000);
        // 151 centisecond ticks since the debounced start: 01.51.
        const std::array<char, 4> first{'1', '5', '1', '0'};
        const std::array<int, 4> firstMasks{0x06, 0x6d, 0x06, 0x3f};
        QTRY_VERIFY2(displayMismatch(first, firstMasks).isEmpty(),
                     qPrintable(displayMismatch(first, firstMasks)));
        QTRY_VERIFY(ui_->item("led0")->property("active").toBool());
        QVERIFY(ui_->capture("board-stopwatch-0151"));

        QVERIFY(ui_->press("button0"));
        QTRY_VERIFY(ui_->board.buttonState(vb::Button::C));
        runTo(155'500'000);
        QVERIFY(ui_->release("button0"));
        QTRY_VERIFY(!ui_->board.buttonState(vb::Button::C));
        runTo(157'000'000);
        QVERIFY(ui_->press("button1"));
        QTRY_VERIFY(ui_->board.buttonState(vb::Button::U));
        runTo(159'000'000);
        QVERIFY(ui_->release("button1"));
        QTRY_VERIFY(!ui_->board.buttonState(vb::Button::U));
        runTo(300'000'000);

        // 141 centisecond ticks after restart: 01.41. The physical leftmost
        // digit is AN3 and the point after AN2 supplies the SS.CC separator.
        const std::array<char, 4> last{'1', '4', '1', '0'};
        const std::array<int, 4> lastMasks{0x06, 0x66, 0x06, 0x3f};
        QTRY_VERIFY2(displayMismatch(last, lastMasks).isEmpty(),
                     qPrintable(displayMismatch(last, lastMasks)));
        QTRY_VERIFY(ui_->item("led0")->property("active").toBool());
        QVERIFY(ui_->capture("board-stopwatch-0141"));

        std::string actual;
        for (const auto& line : ui_->board.structuredLog()) actual += line + '\n';
        const std::string expected = boardUiReadFile(VB_SOURCE_DIR "/tests/golden/stopwatch_3s.log");
        QString mismatch;
        if (actual != expected) {
            std::istringstream actualLines(actual), expectedLines(expected);
            std::string actualLine, expectedLine;
            for (size_t line = 1;; ++line) {
                const bool haveActual = bool(std::getline(actualLines, actualLine));
                const bool haveExpected = bool(std::getline(expectedLines, expectedLine));
                if (haveActual != haveExpected || actualLine != expectedLine) {
                    mismatch = QStringLiteral("Golden mismatch at line %1: actual %2; expected %3")
                        .arg(line)
                        .arg(haveActual ? QString::fromStdString(actualLine) : QStringLiteral("<eof>"))
                        .arg(haveExpected ? QString::fromStdString(expectedLine) : QStringLiteral("<eof>"));
                    break;
                }
                if (!haveActual) break;
            }
        }
        QVERIFY2(actual == expected, qPrintable(mismatch));
        const uint64_t finished = ui_->board.now();
        QTest::qWait(30);
        QVERIFY(ui_->adapter.refresh());
        QCOMPARE(ui_->board.now(), finished);
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    StopwatchBoardUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_board_stopwatch_ui.moc"
