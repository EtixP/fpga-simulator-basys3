#include "Vcounter.h"
#include "qt_board_ui_fixture.h"

#include <QStyleHints>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

class CounterBoardUiTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<BoardUiFixture<Vcounter>> ui_;

    QString ledMismatch(uint16_t expected) const {
        for (int index = 0; index < 16; ++index) {
            const QByteArray name = QByteArray("led") + QByteArray::number(index);
            auto* led = ui_->item(name.constData());
            if (!led) return QStringLiteral("Missing LED %1").arg(index);
            if (!led->property("available").toBool())
                return QStringLiteral("Counter LED %1 is unavailable").arg(index);
            if (led->property("active").toBool() != bool(expected & (uint16_t(1) << index)))
                return QStringLiteral("LED %1 does not match arithmetic result %2").arg(index).arg(expected);
        }
        return {};
    }

private slots:
    void init() {
        ui_ = std::make_unique<BoardUiFixture<Vcounter>>(
            "counter", VB_SOURCE_DIR "/examples/counter.xdc");
        QVERIFY2(ui_->loadShell(), qPrintable(ui_->warnings.join('\n')));
        QTRY_VERIFY(ui_->item("sw0"));
        QCOMPARE(ui_->window->size(), QSize(1280, 820));
        QCOMPARE(ui_->board.now(), uint64_t(0));
    }

    void cleanup() {
        if (!ui_) return;
        // Destruction must release a held momentary input while its adapter
        // and BoardModel still exist. It must not invent a reset clock edge.
        const uint64_t cycle = ui_->board.now();
        ui_->closeShell();
        QCOMPARE(ui_->board.now(), cycle);
        QVERIFY(!ui_->board.buttonState(vb::Button::C));
        QVERIFY2(ui_->warnings.isEmpty(), qPrintable(ui_->warnings.join('\n')));
        ui_.reset();
    }

    void actualInputsPreserveCounterArithmeticAndResetEdges() {
        QVERIFY(ui_->click("sw0"));
        QVERIFY(ui_->click("sw2"));
        QTRY_VERIFY(ui_->board.switchState(0) && ui_->board.switchState(2));
        QCOMPARE(ui_->board.now(), uint64_t(0));
        QTRY_VERIFY2(ledMismatch(0).isEmpty(), qPrintable(ledMismatch(0)));

        // Independent RTL oracle: increment = 1 + 4 = 5 for 997 edges.
        ui_->board.tick(997);
        QVERIFY(ui_->adapter.refresh());
        QTRY_VERIFY2(ledMismatch(4985).isEmpty(), qPrintable(ledMismatch(4985)));

        QVERIFY(ui_->keyActivate("sw1"));
        QTRY_VERIFY(ui_->board.switchState(1));
        ui_->board.tick(1003);  // 4985 + (1 + 2 + 4) * 1003 = 12006
        QVERIFY(ui_->adapter.refresh());
        QTRY_VERIFY2(ledMismatch(12006).isEmpty(), qPrintable(ledMismatch(12006)));

        // A controller-side write must remain authoritative after earlier
        // mouse/key input; QML cannot keep a stale local checked state.
        ui_->board.setSwitch(0, false);
        QVERIFY(ui_->adapter.refresh());
        QTRY_VERIFY(!ui_->item("sw0")->property("active").toBool());
        ui_->board.tick(11);  // 12006 + 6 * 11 = 12072
        QVERIFY(ui_->adapter.refresh());
        QTRY_VERIFY2(ledMismatch(12072).isEmpty(), qPrintable(ledMismatch(12072)));

        const uint64_t beforeReset = ui_->board.now();
        QVERIFY(ui_->press("button0"));
        QTRY_VERIFY(ui_->board.buttonState(vb::Button::C));
        QTRY_VERIFY(ui_->item("button0")->property("active").toBool());
        QTest::qWait(30);  // Wall time and QML event delivery never clock RTL.
        QVERIFY(ui_->adapter.refresh());
        QCOMPARE(ui_->board.now(), beforeReset);
        QVERIFY2(ledMismatch(12072).isEmpty(), qPrintable(ledMismatch(12072)));

        ui_->board.tick(1);  // The synchronous reset happens at this edge only.
        QVERIFY(ui_->adapter.refresh());
        QTRY_VERIFY2(ledMismatch(0).isEmpty(), qPrintable(ledMismatch(0)));
        QVERIFY(ui_->release("button0"));
        QTRY_VERIFY(!ui_->board.buttonState(vb::Button::C));
        QCOMPARE(ui_->board.now(), beforeReset + 1);
        ui_->board.tick(10);
        QVERIFY(ui_->adapter.refresh());
        QTRY_VERIFY2(ledMismatch(60).isEmpty(), qPrintable(ledMismatch(60)));
        QVERIFY(ui_->capture("board-counter"));
    }

    void unavailableResourcesRemainVisibleAndCannotDriveInputs() {
        for (int index = 0; index < 16; ++index) {
            const QByteArray name = QByteArray("sw") + QByteArray::number(index);
            auto* control = ui_->item(name.constData());
            QVERIFY(control);
            QVERIFY(control->isVisible());
            QCOMPARE(control->property("resource").toString(), QStringLiteral("SW%1").arg(index));
            QCOMPARE(control->property("available").toBool(), index < 4);
            QCOMPARE(control->isEnabled(), index < 4);
        }
        for (int index = 0; index < 5; ++index) {
            const QByteArray name = QByteArray("button") + QByteArray::number(index);
            auto* control = ui_->item(name.constData());
            QVERIFY(control);
            QVERIFY(control->isVisible());
            QCOMPARE(control->property("available").toBool(), index == 0);
            QCOMPARE(control->isEnabled(), index == 0);
        }
        const auto before = ui_->board.structuredLog();
        for (const char* name : {"sw4", "sw15", "button1", "button4"}) {
            auto* disabled = ui_->item(name);
            QVERIFY(ui_->reveal(disabled));
            QTest::mouseClick(ui_->window, Qt::LeftButton, Qt::NoModifier,
                              ui_->bounds(disabled).center().toPoint());
            QVERIFY(!disabled->property("active").toBool());
        }
        QVERIFY(ui_->board.structuredLog() == before);
        QCOMPARE(ui_->board.now(), uint64_t(0));
        for (int digit = 0; digit < 4; ++digit) {
            const QByteArray name = QByteArray("digit") + QByteArray::number(digit);
            auto* display = ui_->item(name.constData());
            QVERIFY(display);
            QCOMPARE(display->property("segments").toInt(), 0);
            QVERIFY(!display->property("decimalPoint").toBool());
        }
    }

    void destroyingPresentationReleasesHeldResetWithoutStepping() {
        QVERIFY(ui_->click("sw0"));
        ui_->board.tick(7);
        QVERIFY(ui_->adapter.refresh());
        QVERIFY(ui_->press("button0"));
        QTRY_VERIFY(ui_->board.buttonState(vb::Button::C));
        ui_->closeShell();
        QVERIFY(!ui_->board.buttonState(vb::Button::C));
        QCOMPARE(ui_->board.now(), uint64_t(7));
        QVERIFY(ui_->board.ledState(0));  // 7 remains, no implicit reset edge.
        ui_->board.tick(1);
        QVERIFY(ui_->adapter.refresh());
        QVERIFY(ui_->board.ledState(3));  // released reset: next value is 8.
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    CounterBoardUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_board_counter_ui.moc"
