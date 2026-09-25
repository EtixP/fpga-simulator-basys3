// The Qt inspector and event log against the unchanged counter RTL, through
// actual mouse, keyboard and wheel events. Oracles: the RTL's arithmetic for
// values, and a second counter board's own structured log for events.
#include "Vcounter.h"
#include "qt_board_ui_fixture.h"

#include "qt/EventLogModel.h"
#include "qt/SignalInspectorModel.h"

#include <QQuickView>
#include <QSignalSpy>
#include <QStyleHints>
#include <QWheelEvent>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdlib>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
// One-bit ports clk and in0..in3, and 40 eight-bit internal signals many.s0 ..
// many.s39: enough watches to reach the limit and to overflow a short inspector
// list, with port and watch rows mixed. Every value changes on each cycle.
class ManySignalsEngine final : public vb::SimEngine {
public:
    static constexpr uint32_t kInputBase = 41;
    vb::SignalId lookup(std::string_view name) override {
        if (name == "clk") return vb::SignalId(0);
        if (name.size() == 3 && name.rfind("in", 0) == 0 && name[2] >= '0' && name[2] <= '3')
            return vb::SignalId(kInputBase + uint32_t(name[2] - '0'));
        if (name.rfind("many.s", 0) != 0 || name.size() < 7) return vb::kNoSignal;
        const int n = std::atoi(std::string(name.substr(6)).c_str());
        return n >= 0 && n < 40 ? vb::SignalId(n + 1) : vb::kNoSignal;
    }
    vb::SignalInfo info(vb::SignalId id) const override {
        if (id == vb::SignalId(0)) return {"clk", 1, true};
        if (uint32_t(id) >= kInputBase) return {"in" + std::to_string(uint32_t(id) - kInputBase), 1, true};
        return {"many.s" + std::to_string(uint32_t(id) - 1), 8, false};
    }
    std::vector<vb::SignalInfo> ports() const override {
        return {{"clk", 1, true}, {"in0", 1, true}, {"in1", 1, true}, {"in2", 1, true}, {"in3", 1, true}};
    }
    void step(uint64_t cycles) override { now_ += cycles; }
    uint64_t now() const override { return now_; }
    uint64_t peek(vb::SignalId id) override {
        return (uint32_t(id) + now_) & (uint32_t(id) >= 1 && uint32_t(id) <= 40 ? 0xFF : 1);
    }
    void poke(vb::SignalId, uint64_t) override {}
    void setTraceFile(std::string_view) override {}
    void trace(bool) override {}

private:
    uint64_t now_ = 0;
};
}  // namespace

class InspectorUiTest final : public QObject {
    Q_OBJECT

    std::unique_ptr<BoardUiFixture<Vcounter>> ui_;
    std::unique_ptr<vb::qt::SimulationController> controller_;
    qint64 wallNanoseconds_ = 0;
    quint64 wheelTimestamp_ = 1'000;

    QString text(const char* name) const {
        const auto* item = ui_->item(name);
        return item ? item->property("text").toString() : QString();
    }
    QString text(const QString& name) const { return text(name.toLatin1().constData()); }
    vb::qt::SignalInspectorModel& inspector() const { return *ui_->adapter.inspector(); }
    vb::qt::EventLogModel& events() const { return *ui_->adapter.eventLog(); }
    QQuickItem* logList() const { return ui_->item("logLines"); }

    bool load() {
        ui_->closeShell();
        controller_.reset();
        vb::qt::SimulationController::Options options;
        options.automaticScheduling = false;
        options.nowNanoseconds = [this] { return wallNanoseconds_; };
        controller_ = std::make_unique<vb::qt::SimulationController>(
            ui_->adapter, QStringLiteral("Counter"), std::move(options));
        return ui_->loadShell(controller_.get());
    }

    bool type(const char* field, const QString& value, bool enter = true) {
        auto* input = ui_->item(field);
        if (!ui_->click(field)) return false;
        QTest::keyClick(ui_->window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(ui_->window, Qt::Key_Backspace);
        for (const QChar character : value) QTest::keyClick(ui_->window, character.toLatin1());
        if (input->property("text").toString() != value) return false;
        if (enter) QTest::keyClick(ui_->window, Qt::Key_Return);
        return true;
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

    bool step(const QString& cycles) {
        return editStep(cycles) && ui_->click("stepButton");
    }

    void runBatches(int batches) {
        for (int batch = 0; batch < batches; ++batch) {
            wallNanoseconds_ += 20'000'000;
            controller_->processBatch();
        }
    }

    std::vector<vb::qt::EventLogModel::Event> recorded() const {
        std::vector<vb::qt::EventLogModel::Event> result;
        for (int row = 0; row < events().rowCount(); ++row) {
            const auto index = events().index(row, 0);
            vb::qt::EventLogModel::Event event;
            event.kind = vb::qt::EventLogModel::Kind(index.data(vb::qt::EventLogModel::KindRole).toInt());
            event.cycle = index.data(vb::qt::EventLogModel::CycleRole).toString().toULongLong();
            event.text = index.data(vb::qt::EventLogModel::TextRole).toString();
            result.push_back(event);
        }
        return result;
    }

    void wheel(QQuickItem* target, int angle) {
        const QPointF position = ui_->bounds(target).center();
        QWheelEvent event(position, ui_->window->mapToGlobal(position), QPoint(), QPoint(0, angle),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        wheelTimestamp_ += 500;
        event.setTimestamp(wheelTimestamp_);
        QGuiApplication::sendEvent(ui_->window, &event);
    }

    bool capture(const char* name) const {
        QTest::mouseMove(ui_->window, QPoint(8, 8));
        QTest::qWait(150);
        return ui_->capture(name);
    }

    QString controlsProblem(std::initializer_list<const char*> names) const {
        const QRectF windowBounds(0, 0, ui_->window->width(), ui_->window->height());
        QRectF previous;
        for (const char* name : names) {
            auto* item = ui_->item(name);
            if (!item || !item->isVisible() || item->width() <= 0)
                return QStringLiteral("missing %1").arg(QLatin1String(name));
            const auto rect = ui_->bounds(item);
            if (!windowBounds.contains(rect) || (!previous.isNull() && rect.left() < previous.right()))
                return QStringLiteral("clipped or overlapping %1").arg(QLatin1String(name));
            previous = rect;
        }
        return {};
    }

private slots:
    void init() {
        wallNanoseconds_ = 0;
        ui_ = std::make_unique<BoardUiFixture<Vcounter>>("counter", VB_SOURCE_DIR "/examples/counter.xdc");
        QVERIFY2(load(), qPrintable(ui_->warnings.join('\n')));
        QTRY_VERIFY(ui_->item("inspectorPanel") && ui_->item("inspectorPanel")->isVisible());
    }

    void cleanup() {
        if (!ui_) return;
        if (controller_) controller_->pause();
        ui_->closeShell();
        controller_.reset();
        QVERIFY2(ui_->warnings.isEmpty(), qPrintable(ui_->warnings.join('\n')));
        ui_.reset();
    }

    void inspectorShowsPortsBindingsAndChanges() {
        QCOMPARE(inspector().count(), 4);
        for (const char* name : {"signalRow_btnC", "signalRow_clk", "signalRow_led", "signalRow_sw"})
            QTRY_VERIFY2(ui_->item(name) && ui_->item(name)->isVisible(), name);
        QCOMPARE(text("inspectorSnapshot"), QStringLiteral("Values at cycle 0"));
        QCOMPARE(text("signalValue_led"), QStringLiteral("0x0000"));
        // The master clock is low until the first rising edge, then high at
        // every observation (the frozen engine contract).
        QCOMPARE(text("signalValue_clk"), QStringLiteral("0"));
        const auto binding = [this](const QString& name) {
            const int row = [&] {
                for (int r = 0; r < inspector().rowCount(); ++r)
                    if (inspector().index(r, 0).data(vb::qt::SignalInspectorModel::NameRole) == name) return r;
                return -1;
            }();
            return inspector().index(row, 0).data(vb::qt::SignalInspectorModel::BindingRole).toString();
        };
        QCOMPARE(binding(QStringLiteral("led")), QStringLiteral("LED0–LED15"));
        QCOMPARE(binding(QStringLiteral("sw")), QStringLiteral("SW0–SW3"));
        QCOMPARE(binding(QStringLiteral("btnC")), QStringLiteral("BTNC"));
        QCOMPARE(binding(QStringLiteral("clk")), QStringLiteral("CLK100"));

        // A switch click changes the sw row only; it is highlighted.
        QVERIFY(ui_->click("sw0"));
        QTRY_COMPARE(text("signalValue_sw"), QStringLiteral("0x1"));
        QVERIFY(ui_->item("signalRow_sw")->property("changed").toBool());
        QVERIFY(!ui_->item("signalRow_led")->property("changed").toBool());
        // The rendered highlight, not just the flag.
        QCOMPARE(ui_->item("signalRow_sw")->property("color").value<QColor>(), QColor(QStringLiteral("#3a2f1a")));
        QCOMPARE(ui_->item("signalRow_led")->property("color").value<QColor>().alpha(), 0);
        // One stepped cycle: the counter RTL adds sw to led.
        QVERIFY(step(QStringLiteral("1")));
        QTRY_COMPARE(text("signalValue_led"), QStringLiteral("0x0001"));
        QCOMPARE(text("inspectorSnapshot"), QStringLiteral("Values at cycle 1"));
        QCOMPARE(text("signalValue_clk"), QStringLiteral("1"));
        QVERIFY(ui_->item("signalRow_led")->property("changed").toBool());
        QVERIFY(!ui_->item("signalRow_sw")->property("changed").toBool());
        QVERIFY(ui_->click("sw1"));  // sw = 3
        QVERIFY(step(QStringLiteral("40")));
        QTRY_COMPARE(text("signalValue_led"), QStringLiteral("0x0079"));  // 1 + 40 * 3
        QVERIFY(capture("inspector-counter"));
    }

    void watchesFromTheKeyboard() {
        QVERIFY(ui_->click("sw2"));  // sw = 4
        QVERIFY(step(QStringLiteral("25")));
        QVERIFY(type("watchInput", QStringLiteral("counter.count")));
        QTRY_VERIFY(ui_->item("signalRow_counter.count"));
        QCOMPARE(text("inspectorSnapshot"), QStringLiteral("Values at cycle 25"));
        QTRY_COMPARE(text("signalValue_counter.count"), QStringLiteral("0x0064"));
        QCOMPARE(text("signalValue_led"), QStringLiteral("0x0064"));
        QVERIFY(!ui_->item("watchError")->isVisible());
        QCOMPARE(text("watchInput"), QString());
        QVERIFY(step(QStringLiteral("5")));
        QTRY_COMPARE(text("signalValue_counter.count"), QStringLiteral("0x0078"));
        QCOMPARE(text("signalValue_led"), text("signalValue_counter.count"));
        QVERIFY(capture("inspector-watch"));

        QVERIFY(type("watchInput", QStringLiteral("led")));
        QTRY_VERIFY(ui_->item("watchError")->isVisible());
        QVERIFY(text("watchError").startsWith(QStringLiteral("Ports are listed automatically")));
        QCOMPARE(text("watchInput"), QStringLiteral("led"));  // kept for correction
        QVERIFY(type("watchInput", QStringLiteral("counter.nope")));
        QTRY_VERIFY(text("watchError").contains(QStringLiteral("No readable signal named counter.nope")));
        QVERIFY(type("watchInput", QStringLiteral("counter.count"), false));
        QVERIFY(ui_->click("watchButton"));
        QTRY_VERIFY(text("watchError").contains(QStringLiteral("already listed")));
        QVERIFY(capture("inspector-watch-error"));

        QVERIFY(ui_->click("removeWatch_counter.count"));
        QTRY_VERIFY(!ui_->item("signalRow_counter.count"));
        QCOMPARE(inspector().count(), 4);
        QTRY_VERIFY(!ui_->item("watchError")->isVisible());
    }

    void filterSignalsByName() {
        QVERIFY(type("watchInput", QStringLiteral("counter.count")));
        QTRY_COMPARE(inspector().count(), 5);
        QVERIFY(type("inspectorFilter", QStringLiteral("CO"), false));
        QTRY_COMPARE(ui_->item("inspectorList")->property("count").toInt(), 1);
        QTRY_VERIFY(ui_->item("signalRow_counter.count"));
        QTRY_VERIFY(!ui_->item("signalRow_led"));
        QVERIFY(type("inspectorFilter", QStringLiteral("zz"), false));
        QTRY_VERIFY(ui_->item("inspectorEmptyFilter")->isVisible());
        QCOMPARE(text("inspectorEmptyFilter"), QStringLiteral("No signal matches “zz”."));
        QVERIFY(type("inspectorFilter", QString(), false));
        QTRY_COMPARE(ui_->item("inspectorList")->property("count").toInt(), 5);
    }

    void logRecordsTheBoardsOwnEvents() {
        QVERIFY(ui_->click("logsTab"));
        QTRY_VERIFY(ui_->item("eventLogView")->isVisible());
        QCOMPARE(text("logEmptyTitle"), QStringLiteral("Recording is off"));
        QVERIFY(ui_->click("logRecordButton"));
        QTRY_COMPARE(text("logRecordButton"), QStringLiteral("Stop"));
        QTRY_COMPARE(text("logLineText0"), QStringLiteral("Recording started at cycle 0"));
        QVERIFY(ui_->click("sw0"));
        QVERIFY(ui_->click("sw3"));  // sw = 9
        QVERIFY(step(QStringLiteral("3500")));

        // Independent oracle: another counter board, same stimulus, its own log.
        auto oracleEngine = vb::makeVerilatorEngine<Vcounter>({.topModule = "counter"});
        vb::BoardModel oracle(*oracleEngine, vb::PinBinding::bind(
            vb::parseXdc(boardUiReadFile(VB_SOURCE_DIR "/examples/counter.xdc")), *oracleEngine));
        oracle.setLogEnabled(true);
        oracle.setSwitch(0, true);
        oracle.setSwitch(3, true);
        oracle.tick(3500);
        std::vector<vb::qt::EventLogModel::Event> expected;
        for (const auto& line : oracle.structuredLog()) {
            vb::qt::EventLogModel::Event event;
            if (vb::qt::EventLogModel::parseLine(line, event)) expected.push_back(event);
        }
        QVERIFY(expected.size() > 10);
        const auto rows = recorded();
        QCOMPARE(rows.size(), expected.size() + 1);  // plus the start notice
        for (size_t i = 0; i < expected.size(); ++i) {
            QCOMPARE(rows[i + 1].cycle, expected[i].cycle);
            QCOMPARE(rows[i + 1].text, expected[i].text);
        }
        // The board's own copy stays bounded while the view records.
        QCOMPARE(ui_->board.structuredLog().size(), size_t{6});

        // Categories and text filter, in C++, keep the original order.
        const int all = logList()->property("count").toInt();
        QCOMPARE(all, int(rows.size()));
        QVERIFY(ui_->click("logShowLeds"));
        QTRY_VERIFY(logList()->property("count").toInt() < all);
        const int inputsAndNotice = int(std::count_if(rows.begin(), rows.end(), [](const auto& event) {
            return event.kind != vb::qt::EventLogModel::Leds;
        }));
        QCOMPARE(logList()->property("count").toInt(), inputsAndNotice);
        QVERIFY(ui_->click("logShowLeds"));
        // Filter on an LED that the RTL actually changed (with sw = 9 the
        // counter moves 9,000 per observation, so LED[0] is never seen to change).
        const auto firstLed = std::find_if(rows.begin(), rows.end(), [](const auto& event) {
            return event.kind == vb::qt::EventLogModel::Leds;
        });
        QVERIFY(firstLed != rows.end());
        const QString needle = firstLed->text.section(QLatin1Char(' '), 0, 0);  // e.g. "LED[3]"
        const int matching = int(std::count_if(rows.begin(), rows.end(), [&](const auto& event) {
            return event.text.contains(needle, Qt::CaseInsensitive)
                || QString::number(event.cycle).contains(needle);
        }));
        QVERIFY(type("logFilter", needle.toLower(), false));
        QTRY_COMPARE(logList()->property("count").toInt(), matching);
        for (int row = 0; row < matching; ++row) {
            const auto* line = ui_->item(QStringLiteral("logLineText%1").arg(row).toLatin1().constData());
            if (line) QVERIFY(line->property("text").toString().startsWith(needle));
        }
        // ListView announces count changes at its next layout pass.
        QTRY_COMPARE(text("logStatus"), QStringLiteral("%1 of %2 shown").arg(matching).arg(rows.size()));
        QVERIFY(capture("event-log-filtered"));
        QVERIFY(type("logFilter", QString(), false));

        QVERIFY(type("logFilter", QStringLiteral("zzz"), false));
        QTRY_VERIFY(ui_->item("logEmptyState")->isVisible());
        QCOMPARE(text("logEmptyTitle"), QStringLiteral("No events match the filter"));
        QVERIFY(type("logFilter", QString(), false));
        QTRY_VERIFY(!ui_->item("logEmptyState")->isVisible());

        QVERIFY(ui_->click("logRecordButton"));
        QTRY_COMPARE(text("logRecordButton"), QStringLiteral("Record"));
        QCOMPARE(recorded().back().text, QStringLiteral("Recording stopped at cycle 3500"));
        QCOMPARE(ui_->board.structuredLog().size(), size_t{6});  // Stop took every line
        QVERIFY(ui_->board.logEnabled());  // the fixture's own logging is restored

        // Clear while recording: an empty, still-recording view.
        QVERIFY(ui_->click("logRecordButton"));
        QTRY_COMPARE(text("logRecordButton"), QStringLiteral("Stop"));
        QVERIFY(ui_->click("logClearButton"));
        QTRY_COMPARE(events().count(), 0);
        QTRY_VERIFY(ui_->item("logEmptyState")->isVisible());
        QCOMPARE(text("logEmptyTitle"), QStringLiteral("No events yet"));
        QTRY_COMPARE(text("logStatus"), QStringLiteral("0 of 0 shown"));
        QVERIFY(ui_->click("logRecordButton"));
        QTRY_COMPARE(events().count(), 1);  // the stop notice
        // Stopped: the fixture owns the board log again, and Clear leaves it alone.
        QVERIFY(step(QStringLiteral("3000")));
        const auto owned = ui_->board.structuredLog();
        QVERIFY(owned.size() > 6);
        QVERIFY(ui_->click("logClearButton"));
        QTRY_COMPARE(events().count(), 0);
        QTRY_COMPARE(text("logEmptyTitle"), QStringLiteral("Recording is off"));
        QVERIFY(ui_->board.structuredLog() == owned);
    }

    void followsNewestEventsPastTheLimit() {
        QVERIFY(ui_->click("logsTab"));
        QVERIFY(ui_->click("logRecordButton"));
        for (const char* sw : {"sw0", "sw1", "sw2", "sw3"}) QVERIFY(ui_->click(sw));  // +15 per cycle
        QVERIFY(ui_->click("runPauseButton"));
        while (events().recordedEvents() <= vb::qt::EventLogModel::MaximumEvents + 500) runBatches(5);
        QVERIFY(ui_->click("runPauseButton"));
        QCOMPARE(events().count(), vb::qt::EventLogModel::MaximumEvents);
        QTRY_VERIFY(ui_->item("logTrimmedNotice")->isVisible());
        QTRY_VERIFY(logList()->property("atYEnd").toBool());
        const auto newestVisible = [this] {
            const int last = logList()->property("count").toInt() - 1;
            const auto* line = ui_->item(QStringLiteral("logLineText%1").arg(last).toLatin1().constData());
            return line && ui_->bounds(logList()).contains(ui_->bounds(const_cast<QQuickItem*>(line)).center());
        };
        QTRY_VERIFY(newestVisible());
        // At the limit each batch inserts and trims equally, so the row count
        // and height stay put while the origin moves; following must hold.
        const auto lastText = [this] {
            return events().index(events().rowCount() - 1, 0)
                .data(vb::qt::EventLogModel::CycleRole).toString();
        };
        const QString before = lastText();
        QVERIFY(ui_->click("runPauseButton"));
        runBatches(3);
        QVERIFY(ui_->click("runPauseButton"));
        QVERIFY(lastText() != before);
        QTRY_VERIFY(newestVisible());
        QTRY_VERIFY(logList()->property("atYEnd").toBool());
        // Scrolling back stops following; more events do not move the view.
        wheel(logList(), 240);
        QTRY_VERIFY(!logList()->property("moving").toBool());
        QVERIFY(!logList()->property("following").toBool());
        const qreal contentY = logList()->property("contentY").toReal();
        QVERIFY(ui_->click("runPauseButton"));
        runBatches(3);
        QVERIFY(ui_->click("runPauseButton"));
        QTest::qWait(50);
        QVERIFY(qAbs(logList()->property("contentY").toReal() - contentY) < 1.0);
        QTRY_VERIFY(ui_->item("logLatestButton")->isVisible());
        QVERIFY(ui_->click("logLatestButton"));
        QTRY_VERIFY(newestVisible());
        QVERIFY(capture("event-log-following"));

        // Keyboard, after the list takes focus: pages stop following, End resumes.
        const auto following = [this] { return logList()->property("following").toBool(); };
        const auto atEnd = [this] { return logList()->property("atYEnd").toBool(); };
        logList()->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(ui_->item("logFocusOutline")->isVisible());
        QTest::keyClick(ui_->window, Qt::Key_PageUp);
        QVERIFY(!following());
        QTRY_VERIFY(!atEnd());
        QTRY_VERIFY(ui_->item("logLatestButton")->isVisible());
        const qreal pagedUp = logList()->property("contentY").toReal();
        QTest::keyClick(ui_->window, Qt::Key_Up);
        QTRY_COMPARE(logList()->property("contentY").toReal(), pagedUp - 20);
        QTest::keyClick(ui_->window, Qt::Key_Down);
        QTest::keyClick(ui_->window, Qt::Key_PageDown);  // back at the end: following again
        QTRY_VERIFY(atEnd());
        QVERIFY(following());
        QTest::keyClick(ui_->window, Qt::Key_PageUp);
        QVERIFY(!following());
        QTest::keyClick(ui_->window, Qt::Key_Home);
        QTRY_VERIFY(logList()->property("atYBeginning").toBool());
        QTest::keyClick(ui_->window, Qt::Key_End);
        QVERIFY(following());
        QTRY_VERIFY(newestVisible());

        // Scroll bar: dragging up stops following; dragging to the end resumes.
        auto* bar = ui_->item("logScrollBar");
        QVERIFY2(bar && bar->isVisible() && bar->height() > 40,
                 qPrintable(bar ? QStringLiteral("visible %1 %2x%3 size %4").arg(bar->isVisible())
                                      .arg(bar->width()).arg(bar->height()).arg(bar->property("size").toReal())
                                : QStringLiteral("missing")));
        const QRectF track = ui_->bounds(bar);
        const int x = qRound(track.center().x());
        const QPoint third(x, qRound(track.top() + track.height() / 3));
        const QPoint pastEnd(x, qRound(track.bottom() + 20));
        QTest::mousePress(ui_->window, Qt::LeftButton, Qt::NoModifier, QPoint(x, qRound(track.bottom() - 3)));
        QVERIFY(bar->property("pressed").toBool());
        QVERIFY(!following());
        QTest::mouseMove(ui_->window, third);
        QTest::mouseRelease(ui_->window, Qt::LeftButton, Qt::NoModifier, third);
        QTRY_VERIFY(!atEnd());
        QVERIFY(!following());
        QTest::mousePress(ui_->window, Qt::LeftButton, Qt::NoModifier, third);
        QTest::mouseMove(ui_->window, pastEnd);
        QTest::mouseRelease(ui_->window, Qt::LeftButton, Qt::NoModifier, pastEnd);
        QTRY_VERIFY(atEnd());
        QVERIFY(following());

        // Wheel: scrolling back down to the end resumes following.
        wheel(logList(), 240);
        QTRY_VERIFY(!logList()->property("moving").toBool());
        QVERIFY(!following());
        for (int attempt = 0; attempt < 20 && !atEnd(); ++attempt) {
            wheel(logList(), -1200);
            QTRY_VERIFY(!logList()->property("moving").toBool());
        }
        QVERIFY(atEnd());
        QVERIFY(following());
        QTRY_VERIFY(!ui_->item("logLatestButton")->isVisible());

        // Clear starts over, including the discarded count; recording continues.
        QVERIFY(ui_->click("logClearButton"));
        QTRY_COMPARE(events().count(), 0);
        QCOMPARE(events().trimmedEvents(), 0);
        QTRY_VERIFY(!ui_->item("logTrimmedNotice")->isVisible());
        QCOMPARE(text("logEmptyTitle"), QStringLiteral("No events yet"));
    }

    // The inspector panel alone, over a design with many internal signals.
    void watchLimitAndKeyboardAccess() {
        ui_->closeShell();  // one window, so keyboard focus is unambiguous
        ManySignalsEngine engine;
        vb::BoardModel board(engine, vb::PinBinding::bind(
            vb::parseXdc("set_property PACKAGE_PIN W5 [get_ports clk]\n"), engine));
        vb::qt::BoardAdapter adapter(&board);
        QQmlEngine::setObjectOwnership(&adapter, QQmlEngine::CppOwnership);
        QQuickView view;
        QStringList warnings;
        QObject::connect(view.engine(), &QQmlEngine::warnings, &view, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{QStringLiteral("board"), QVariant::fromValue(&adapter)},
                                   {QStringLiteral("designStem"), QStringLiteral("many")}});
        view.setSource(QUrl::fromLocalFile(QStringLiteral(VB_QT_QML_DIR "/InspectorPanel.qml")));
        QVERIFY2(view.status() == QQuickView::Ready, qPrintable(warnings.join('\n')));
        view.resize(360, 320);
        view.show();
        view.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&view));
        const auto find = [&](const char* name) {
            return boardUiFindItem(view.contentItem(), QString::fromLatin1(name));
        };
        const auto click = [&](const char* name) {
            auto* target = find(name);
            if (!target || !target->isVisible() || !target->isEnabled()) return false;
            QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                              BoardUiFixture<Vcounter>::bounds(target).center().toPoint());
            return true;
        };
        const auto typeInto = [&](const char* field, const QString& text) {
            if (!click(field)) return false;
            QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClick(&view, Qt::Key_Backspace);
            for (const QChar character : text) QTest::keyClick(&view, character.toLatin1());
            return find(field)->property("text").toString() == text;
        };
        const auto typeWatch = [&](const QString& name) { return typeInto("watchInput", name); };
        // Wrapped within the label, never past its edge.
        const auto wrapped = [&](const char* name) {
            auto* label = find(name);
            return label && label->isVisible() && label->property("lineCount").toInt() > 1
                && label->property("contentWidth").toReal() <= label->width() + 0.5;
        };

        // Long names have no spaces; messages that quote them still wrap.
        const QString longName = QStringLiteral("many.") + QString(120, QLatin1Char('x'));
        QVERIFY(typeWatch(longName));
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(find("watchError")->isVisible());
        QVERIFY(find("watchError")->property("text").toString().contains(longName));
        QTRY_VERIFY(wrapped("watchError"));
        QVERIFY(typeInto("inspectorFilter", QString(90, QLatin1Char('z'))));
        QTRY_VERIFY(wrapped("inspectorEmptyFilter"));
        QVERIFY(typeInto("inspectorFilter", QString()));
        QTRY_COMPARE(find("inspectorList")->property("count").toInt(), 5);

        // The limit: the 32nd watch is accepted, then Watch is disabled and
        // Return explains why.
        for (int n = 0; n < vb::qt::SignalInspectorModel::MaximumWatches - 1; ++n)
            QVERIFY(adapter.addWatch(QStringLiteral("many.s%1").arg(n)));
        QVERIFY(typeWatch(QStringLiteral("many.s31")));
        QVERIFY(find("watchButton")->isEnabled());
        QVERIFY(click("watchButton"));
        QTRY_COMPARE(adapter.inspector()->watchCount(), vb::qt::SignalInspectorModel::MaximumWatches);
        QCOMPARE(find("watchInput")->property("text").toString(), QString());
        QVERIFY(typeWatch(QStringLiteral("many.s32")));
        QTRY_VERIFY(!find("watchButton")->isEnabled());
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(find("watchError")->isVisible());
        QCOMPARE(find("watchError")->property("text").toString(),
                 QStringLiteral("At most 32 watches can be added."));
        QCOMPARE(adapter.inspector()->count(), 5 + vb::qt::SignalInspectorModel::MaximumWatches);

        auto* list = find("inspectorList");
        const auto atEnd = [&] {
            return qFuzzyCompare(list->property("contentY").toReal() + list->height(),
                                 list->property("contentHeight").toReal());
        };
        // Every row that overlaps the view, even partly, shows the snapshot: value,
        // "= decimal" and highlight. Returns how many rows it checked, or -1.
        using Roles = vb::qt::SignalInspectorModel;
        const auto item = [&](const char* prefix, const QString& name) {
            return find(QStringLiteral("%1%2").arg(QLatin1String(prefix), name).toLatin1().constData());
        };
        const auto visibleRowsCurrent = [&] {
            const QRectF viewport = BoardUiFixture<Vcounter>::bounds(list);
            const auto* model = adapter.inspector();
            int checked = 0;
            for (int r = 0; r < model->rowCount(); ++r) {
                const auto index = model->index(r, 0);
                const QString name = index.data(Roles::NameRole).toString();
                auto* row = item("signalRow_", name);
                if (!row || !viewport.intersects(BoardUiFixture<Vcounter>::bounds(row))) continue;
                ++checked;
                const QString value = index.data(Roles::ValueRole).toString();
                const QString detail = item("signalDetail_", name)->property("text").toString();
                const bool highlighted = row->property("color").value<QColor>().alpha() != 0;
                if (item("signalValue_", name)->property("text").toString() != value
                    || (value.startsWith(QStringLiteral("0x"))
                        && !detail.contains(QStringLiteral("= %1").arg(index.data(Roles::DecimalRole).toString())))
                    || highlighted != index.data(Roles::ChangedRole).toBool()) {
                    qWarning() << "stale row" << name << value << detail;
                    return -1;
                }
            }
            return checked;
        };
        const auto shown = [&](const QString& name) {
            return item("signalValue_", name)->property("text").toString();
        };
        const auto step = [&] {
            board.tick(1);
            return adapter.refresh();
        };
        // Rows are positioned at the next layout pass, before anything is drawn.
        const auto nextFrame = [&] {
            QSignalSpy swapped(&view, &QQuickWindow::frameSwapped);
            view.update();
            return swapped.wait(2000);
        };
        QVERIFY(nextFrame());
        // Partly visible rows at both edges.
        list->setProperty("contentY", 50);
        QVERIFY(step());
        QVERIFY(visibleRowsCurrent() > 0);
        // A refresh lays out only visible rows: one far below keeps its text.
        const QString hidden = shown(QStringLiteral("many.s31"));
        QVERIFY(step());
        QVERIFY(visibleRowsCurrent() > 0);
        QCOMPARE(shown(QStringLiteral("many.s31")), hidden);
        // Scrolled into view, it catches up at once.
        list->setProperty("contentY", list->property("contentHeight").toReal() - list->height());
        QVERIFY(visibleRowsCurrent() > 0);
        QVERIFY(shown(QStringLiteral("many.s31")) != hidden);
        // Rows that a taller view reveals are current too.
        const int shortRows = visibleRowsCurrent();
        list->setProperty("contentY", 0);
        view.resize(360, 720);
        QTRY_VERIFY(list->height() > 500);
        QVERIFY(nextFrame());
        QVERIFY(step());
        QVERIFY(visibleRowsCurrent() > shortRows);
        view.resize(360, 320);
        QTRY_VERIFY(list->height() < 250);
        QVERIFY(nextFrame());
        list->setProperty("contentY", 0);

        // Keyboard scrolling once Tab reaches the list.
        QVERIFY(click("inspectorFilter"));
        QTest::keyClick(&view, Qt::Key_Tab);
        QTRY_VERIFY(list->hasActiveFocus());  // the list, or its current row within it
        QTRY_VERIFY(find("inspectorFocusOutline")->isVisible());
        // The error line shortened the list; a person's key press comes after
        // the frame whose layout pass applies the new height.
        QSignalSpy frame(&view, &QQuickWindow::frameSwapped);
        view.update();
        QVERIFY(frame.wait(2000));
        QTest::keyClick(&view, Qt::Key_End);
        QTRY_VERIFY(atEnd());  // exactly the end, not past it
        QTest::keyClick(&view, Qt::Key_Home);
        QTRY_VERIFY(list->property("atYBeginning").toBool());
        QTest::keyClick(&view, Qt::Key_PageDown);
        QTRY_VERIFY(!list->property("atYBeginning").toBool());
        const qreal paged = list->property("contentY").toReal();
        QTest::keyClick(&view, Qt::Key_Down);
        QTRY_VERIFY(list->property("contentY").toReal() > paged);
        QTest::keyClick(&view, Qt::Key_Up);
        QTRY_COMPARE(list->property("contentY").toReal(), paged);
        QTest::keyClick(&view, Qt::Key_PageUp);
        QTRY_VERIFY(list->property("atYBeginning").toBool());
        QTest::keyClick(&view, Qt::Key_End);
        QTRY_VERIFY(list->property("atYEnd").toBool());
        QTest::keyClick(&view, Qt::Key_Home);
        QTRY_VERIFY(list->property("atYBeginning").toBool());

        // Tabbing through the remove buttons keeps each focused one in view.
        const QRectF listBounds = BoardUiFixture<Vcounter>::bounds(list).adjusted(-0.5, -0.5, 0.5, 0.5);
        QStringList visited;
        QStringList expected;
        for (int n = 0; n < vb::qt::SignalInspectorModel::MaximumWatches; ++n)
            expected << QStringLiteral("removeWatch_many.s%1").arg(n);
        for (int press = 0; press < 60; ++press) {
            QTest::keyClick(&view, Qt::Key_Tab);
            auto* focused = view.activeFocusItem();
            if (!focused || !focused->objectName().startsWith(QStringLiteral("removeWatch_"))) break;
            visited << focused->objectName();
            QTRY_VERIFY2(listBounds.contains(BoardUiFixture<Vcounter>::bounds(focused)),
                         qPrintable(focused->objectName()));
        }
        QCOMPARE(visited, expected);  // every watch, in row order
        QVERIFY(list->property("contentY").toReal() > 0);  // focus scrolled the list
        QCOMPARE(view.activeFocusItem(), find("watchInput"));
        // Shift+Tab goes back through every one of them, in reverse.
        QTest::keyClick(&view, Qt::Key_End);  // in the field: moves the cursor only
        list->setProperty("contentY", 0);
        QStringList back;
        for (int press = 0; press < 60; ++press) {
            QTest::keyClick(&view, Qt::Key_Backtab, Qt::ShiftModifier);
            auto* focused = view.activeFocusItem();
            if (!focused || !focused->objectName().startsWith(QStringLiteral("removeWatch_"))) break;
            back.prepend(focused->objectName());
            QTRY_VERIFY2(listBounds.contains(BoardUiFixture<Vcounter>::bounds(focused)),
                         qPrintable(focused->objectName()));
        }
        QCOMPARE(back, expected);
        QCOMPARE(view.activeFocusItem(), list);
        QTest::keyClick(&view, Qt::Key_Tab);  // on to the first remove button
        QTest::keyClick(&view, Qt::Key_Space);
        QTRY_COMPARE(adapter.inspector()->watchCount(), vb::qt::SignalInspectorModel::MaximumWatches - 1);
        QTRY_VERIFY(!find("signalRow_many.s0"));
        QCOMPARE(view.activeFocusItem(), list);  // focus stays in the list
        QTest::keyClick(&view, Qt::Key_Tab);
        QCOMPARE(view.activeFocusItem(), find("removeWatch_many.s1"));
        // A mouse removal does not turn on the keyboard outline.
        QVERIFY(click("watchInput"));
        QVERIFY(click("removeWatch_many.s1"));
        QTRY_VERIFY(!find("signalRow_many.s1"));
        QVERIFY(view.activeFocusItem() != list);
        QVERIFY(!find("inspectorFocusOutline")->isVisible());
        QTRY_VERIFY(!find("inspectorFocusOutline")->isVisible());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void minimumWindowAndUnavailableStates() {
        ui_->window->resize(960, 640);
        QTRY_COMPARE(ui_->window->size(), QSize(960, 640));
        QTRY_VERIFY(ui_->reveal(ui_->item("logsTab")));  // resize lays out asynchronously
        QVERIFY(ui_->click("logsTab"));
        QTRY_VERIFY(ui_->item("logRecordButton")->isVisible());
        QVERIFY(ui_->click("logRecordButton"));  // populate every control, incl. the status
        QTRY_VERIFY(!text("logStatus").isEmpty());
        QTRY_VERIFY2(controlsProblem({"logRecordButton", "logShowDisplay", "logShowLeds", "logShowUart",
                                      "logShowInputs", "logFilter", "logStatus", "logClearButton"}).isEmpty(),
                     qPrintable(controlsProblem({"logRecordButton", "logShowDisplay", "logShowLeds",
                                                 "logShowUart", "logShowInputs", "logFilter",
                                                 "logStatus", "logClearButton"})));
        QTRY_VERIFY2(controlsProblem({"watchInput", "watchButton"}).isEmpty(),
                     qPrintable(controlsProblem({"watchInput", "watchButton"})));
        QVERIFY(capture("inspector-log-minimum"));

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
            const auto find = [&](const char* name) {
                return boardUiFindItem(window->contentItem(), QString::fromLatin1(name));
            };
            QVERIFY(!find("inspectorPanel") || !find("inspectorPanel")->isVisible());
            window->setProperty("outputIndex", 2);
            QTRY_VERIFY(find("outputTitle"));
            QCOMPARE(find("outputTitle")->property("text").toString(), QStringLiteral("Log view unavailable"));
            QVERIFY(find("outputDetail")->property("text").toString().contains(
                missing ? QStringLiteral("could not be initialized") : QStringLiteral("No design is loaded")));
            QVERIFY(!find("eventLogView") || !find("eventLogView")->isVisible());
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        }
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    InspectorUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_inspector_ui.moc"
