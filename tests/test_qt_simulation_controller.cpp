// Controller timing is checked against explicit cycle/input oracles. The fake
// clock never sleeps; a real counter separately proves equivalent RTL and logs.
#include "Vcounter.h"
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/BoardAdapter.h"
#include "qt/SimulationController.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Controller = vb::qt::SimulationController;

class RecordingEngine final : public vb::SimEngine {
public:
    struct Advance { uint64_t start, count, switches; bool reset; };
    struct Poke { uint64_t cycle; vb::SignalId id; uint64_t value; };
    explicit RecordingEngine(uint64_t initialCycle = 0) : cycle_(initialCycle) {}

    vb::SignalId lookup(std::string_view name) override {
        for (size_t index = 0; index < ports_.size(); ++index)
            if (ports_[index].name == name) return vb::SignalId(index);
        return vb::kNoSignal;
    }
    vb::SignalInfo info(vb::SignalId id) const override { return ports_.at(size_t(id)); }
    std::vector<vb::SignalInfo> ports() const override { return {ports_.begin(), ports_.end()}; }
    uint64_t now() const override { return cycle_; }
    void step(uint64_t cycles) override {
        advances.push_back({cycle_, cycles, values_[3], values_[0] != 0});
        values_[2] = values_[0] ? 0 : (values_[2] + cycles * values_[3]) & 0xffff;
        cycle_ += cycles;
    }
    uint64_t peek(vb::SignalId id) override { return values_.at(size_t(id)); }
    void poke(vb::SignalId id, uint64_t value) override {
        const auto port = info(id);
        if (!port.input || port.name == "clk") throw std::invalid_argument("Not a writable input");
        value &= (uint64_t(1) << port.width) - 1;
        values_.at(size_t(id)) = value;
        pokes.push_back({cycle_, id, value});
    }
    void setTraceFile(std::string_view) override {}
    void trace(bool) override {}

    std::vector<Advance> advances;
    std::vector<Poke> pokes;

private:
    uint64_t cycle_;
    const std::array<vb::SignalInfo, 5> ports_{
        vb::SignalInfo{"btnC", 1, true}, {"clk", 1, true}, {"led", 16, false},
        {"sw", 4, true}, {"uart_rx", 1, true}};
    std::array<uint64_t, 5> values_{};
};

vb::PinBinding recordingBinding(RecordingEngine& engine, bool resetBound) {
    std::string xdc = "set_property PACKAGE_PIN W5 [get_ports clk]\n"
                      "set_property PACKAGE_PIN B18 [get_ports uart_rx]\n";
    if (resetBound) xdc += "set_property PACKAGE_PIN U18 [get_ports btnC]\n";
    constexpr const char* switches[]{"V17", "V16", "W16", "W17"};
    constexpr const char* leds[]{"U16", "E19", "U19", "V19", "W18", "U15", "U14", "V14",
                                 "V13", "V3", "W3", "U3", "P3", "N3", "P1", "L1"};
    for (int index = 0; index < 4; ++index)
        xdc += std::string("set_property PACKAGE_PIN ") + switches[index] +
               " [get_ports {sw[" + std::to_string(index) + "]}]\n";
    for (int index = 0; index < 16; ++index)
        xdc += std::string("set_property PACKAGE_PIN ") + leds[index] +
               " [get_ports {led[" + std::to_string(index) + "]}]\n";
    return vb::PinBinding::bind(vb::parseXdc(xdc), engine);
}

struct Fixture {
    explicit Fixture(uint64_t batch = 100'000, uint64_t initialCycle = 0, bool resetBound = true)
        : engine(initialCycle), board(engine, recordingBinding(engine, resetBound)), adapter(&board),
          controller(adapter, QStringLiteral("Recorded board"),
                     Controller::Options{batch, false, [this] { return wall; }}) {
        board.setLogEnabled(true);
        engine.advances.clear();
        engine.pokes.clear();
    }
    qint64 wall = 0;
    RecordingEngine engine;
    vb::BoardModel board;
    vb::qt::BoardAdapter adapter;
    Controller controller;
};

vb::XdcDoc counterConstraints() {
    std::ifstream file(VB_SOURCE_DIR "/examples/counter.xdc");
    if (!file.good()) throw std::runtime_error("Cannot read counter constraints");
    return vb::parseXdc(std::string(std::istreambuf_iterator<char>(file), {}));
}

struct Counter {
    explicit Counter(const vb::XdcDoc& constraints, uint64_t batch = 37)
        : engine(vb::makeVerilatorEngine<Vcounter>({.topModule = "counter"})),
          board(*engine, vb::PinBinding::bind(constraints, *engine)), adapter(&board),
          controller(adapter, QStringLiteral("Counter"),
                     Controller::Options{batch, false, [this] { return wall; }}) {
        board.setLogEnabled(true);
    }
    qint64 wall = 0;
    std::unique_ptr<vb::SimEngine> engine;
    vb::BoardModel board;
    vb::qt::BoardAdapter adapter;
    Controller controller;
};

uint16_t cachedLeds(const vb::qt::BoardAdapter& adapter) {
    uint16_t value = 0;
    auto* model = adapter.leds();
    for (int index = 0; index < 16; ++index)
        if (model->data(model->index(index), vb::qt::BoardIoModel::ActiveRole).toBool())
            value |= uint16_t(1u << index);
    return value;
}

bool hasLog(const vb::BoardModel& board, const std::string& needle) {
    const auto& lines = board.structuredLog();
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(needle) != std::string::npos;
    });
}
} // namespace

class SimulationControllerTest final : public QObject {
    Q_OBJECT

    enum class Delivery { Direct, Steps, Batches };

    void schedule(Counter& c, Delivery delivery) {
        const auto inputSwitch = [&](int index, bool value) {
            if (delivery == Delivery::Direct) c.board.setSwitch(index, value);
            else QVERIFY(c.adapter.setSwitch(index, value));
        };
        const auto inputReset = [&](bool value) {
            if (delivery == Delivery::Direct) c.board.setButton(vb::Button::C, value);
            else QVERIFY(c.adapter.setButton(0, value));
        };
        const auto advance = [&](uint64_t cycles) {
            const uint64_t target = c.board.now() + cycles;
            if (delivery == Delivery::Direct) {
                c.board.tick(cycles);
            } else if (delivery == Delivery::Steps) {
                constexpr uint32_t sizes[]{1, 7, 991, 2, 37};
                size_t index = 0;
                while (cycles) {
                    const auto chunk = uint32_t(std::min<uint64_t>(cycles, sizes[index++ % 5]));
                    QVERIFY(c.controller.step(chunk));
                    QVERIFY(c.adapter.refresh());
                    cycles -= chunk;
                }
            } else {
                c.controller.setRealtime(true);
                QVERIFY(c.controller.run());
                size_t index = 0;
                constexpr qint64 delays[]{21'000'000, 1'000'000, 500'000'000};
                while (cycles >= 37) {
                    const uint64_t before = c.board.now();
                    c.wall += delays[index++ % 3]; // varying render/event-loop delays
                    c.controller.processBatch();
                    QCOMPARE(c.board.now(), before + 37);
                    cycles -= 37;
                }
                QVERIFY(c.controller.pause());
                if (cycles) QVERIFY(c.controller.step(uint32_t(cycles)));
            }
            QCOMPARE(c.board.now(), target);
        };

        inputSwitch(0, true);
        inputSwitch(2, true); // increment 5
        advance(997);
        inputSwitch(1, true); // increment 7
        advance(1003);
        QVERIFY(c.adapter.refresh());
        QCOMPARE(cachedLeds(c.adapter), uint16_t(5 * 997 + 7 * 1003));
        inputReset(true);
        advance(1);
        inputReset(false);
        inputSwitch(0, false); // increment 6
        advance(1010);
        QVERIFY(c.adapter.refresh());
        QCOMPARE(cachedLeds(c.adapter), uint16_t(6060));
        if (delivery == Delivery::Direct) {
            c.board.setButton(vb::Button::C, true);
            c.board.tick(16);
            c.board.setButton(vb::Button::C, false);
        } else {
            QVERIFY(c.controller.reset());
        }
        advance(73);
        QVERIFY(c.adapter.refresh());
        QCOMPARE(cachedLeds(c.adapter), uint16_t(438));
        QCOMPARE(c.board.now(), uint64_t(3100));
    }

private slots:
    void pausedCommandsHaveExactCycleBudgets() {
        Fixture f;
        QVERIFY(f.controller.connected());
        QVERIFY(f.controller.canReset());
        QCOMPARE(f.controller.board(), &f.adapter);
        QCOMPARE(f.controller.cycleText(), QStringLiteral("0"));
        QVERIFY(!f.controller.running());
        QVERIFY(!f.controller.speedAvailable());
        QVERIFY(!f.controller.virtualTimeText().isEmpty());
        QVERIFY(f.controller.metaObject()->indexOfMethod("processBatch()") < 0);
        QVERIFY(f.adapter.setSwitch(0, true));
        QVERIFY(f.adapter.setSwitch(1, true));
        QVERIFY(!f.controller.step(0));
        QVERIFY(!f.controller.step(Controller::MaximumStepCycles + 1));
        QCOMPARE(f.board.now(), uint64_t(0));
        QVERIFY(f.controller.step());
        QCOMPARE(f.board.now(), uint64_t(1));
        QCOMPARE(cachedLeds(f.adapter), uint16_t(3));
        QCOMPARE(f.controller.cycleText(), QStringLiteral("1"));
        QVERIFY(f.controller.step(999));
        QCOMPARE(f.board.now(), uint64_t(1000));
        QCOMPARE(cachedLeds(f.adapter), uint16_t(3000));
        QVERIFY(f.controller.step(Controller::MaximumStepCycles));
        QCOMPARE(f.board.now(), uint64_t(1'001'000));
        QCOMPARE(cachedLeds(f.adapter), uint16_t((3ull * 1'001'000) & 0xffff));

        const uint64_t before = f.board.now();
        f.controller.pause();
        f.controller.processBatch();
        for (int repeat = 0; repeat < 3; ++repeat) {
            (void)f.controller.cycleText();
            (void)f.controller.virtualTimeText();
            (void)f.controller.cyclesPerSecond();
            (void)f.controller.realtimeMultiplier();
            QVERIFY(f.adapter.refresh());
            QCoreApplication::processEvents();
        }
        QCOMPARE(f.board.now(), before);
        QVERIFY(!f.controller.speedAvailable());
    }

    void realtimeWaitsWithoutChangingFixedBatchBudget() {
        Fixture f;
        f.controller.setRealtime(true);
        QVERIFY(f.controller.realtime());
        QVERIFY(f.controller.run());
        QCOMPARE(f.board.now(), uint64_t(0));
        QVERIFY(!f.controller.step(1)); // a manual edge requires Pause first
        f.controller.processBatch();
        QCOMPARE(f.board.now(), uint64_t(100'000)); // one millisecond virtual
        f.controller.processBatch();
        QCOMPARE(f.board.now(), uint64_t(100'000));
        f.wall = 999'999;
        f.controller.processBatch();
        QCOMPARE(f.board.now(), uint64_t(100'000));
        f.wall = 1'000'000;
        f.controller.processBatch();
        QCOMPARE(f.board.now(), uint64_t(200'000));
        f.wall = 1'000'000'000; // late callback does not enlarge its cycle budget
        f.controller.processBatch();
        QCOMPARE(f.board.now(), uint64_t(300'000));
        QVERIFY(f.controller.pause());
        QCOMPARE(f.controller.cycleText(), QStringLiteral("300000"));

        f.controller.setRealtime(false);
        QVERIFY(f.controller.run());
        f.controller.processBatch();
        f.controller.processBatch(); // turbo ignores the unchanged wall clock
        QCOMPARE(f.board.now(), uint64_t(500'000));
        QVERIFY(f.controller.pause());
    }

    void pauseInvalidatesAQueuedBatch() {
        Fixture f(41);
        QVERIFY(f.controller.run());
        QVERIFY(QMetaObject::invokeMethod(&f.controller, [&] { f.controller.processBatch(); },
                                          Qt::QueuedConnection));
        QVERIFY(f.controller.pause());
        QCoreApplication::processEvents();
        QCOMPARE(f.board.now(), uint64_t(0));
        QVERIFY(!f.controller.running());
        QVERIFY(!f.controller.speedAvailable());
    }

    void nestedEventDeliveryDuringInputPublicationKeepsRunScheduled() {
        Fixture f;
        Controller automatic(f.adapter, QStringLiteral("Nested event delivery"));
        bool delivered = false;
        connect(f.adapter.switches(), &QAbstractItemModel::dataChanged, &automatic,
                [&] {
                    delivered = true;
                    // Consume the armed single-shot timer while the adapter is
                    // publishing. It must defer work and remain scheduled.
                    QTest::qWait(5);
                    QCOMPARE(f.board.now(), uint64_t(0));
                });
        QVERIFY(automatic.run());
        QVERIFY(f.adapter.setSwitch(0, true));
        QVERIFY(delivered);
        QVERIFY(automatic.running());
        QTRY_VERIFY_WITH_TIMEOUT(f.board.now() > 0, 250);
        QVERIFY(automatic.pause());
        const auto paused = f.board.now();
        QTest::qWait(20);
        QCOMPARE(f.board.now(), paused);
    }

    void metricsMeasureActualElapsedTimeAndExcludePausedTime() {
        Fixture f;
        QSignalSpy changed(&f.controller, &Controller::stateChanged);
        QVERIFY(f.controller.run());
        changed.clear();
        f.controller.processBatch();
        QCOMPARE(f.board.now(), uint64_t(100'000));
        QVERIFY(!f.controller.speedAvailable());
        QCOMPARE(changed.count(), 0);
        f.wall = 15'999'999;
        f.controller.processBatch();
        QCOMPARE(changed.count(), 0);
        f.wall = 16'000'000;
        f.controller.processBatch();
        QVERIFY(changed.count() > 0);
        QCOMPARE(f.controller.cycleText(), QStringLiteral("300000"));
        QVERIFY(!f.controller.speedAvailable());

        f.wall = 250'000'000;
        f.controller.processBatch();
        QVERIFY(f.controller.speedAvailable());
        QCOMPARE(f.controller.cyclesPerSecond(), 1'600'000.0); // 400k cycles / .25 seconds
        QCOMPARE(f.controller.realtimeMultiplier(), 0.016);
        f.wall = 500'000'000;
        f.controller.processBatch();
        QCOMPARE(f.controller.cyclesPerSecond(), 400'000.0); // only 100k more over .25 seconds
        QCOMPARE(f.controller.realtimeMultiplier(), 0.004);
        QVERIFY(f.controller.pause());
        QVERIFY(!f.controller.speedAvailable());
        QCOMPARE(f.controller.cyclesPerSecond(), 0.0);

        f.wall = 20'000'000'000;
        QVERIFY(f.controller.run());
        f.wall += 250'000'000;
        f.controller.processBatch();
        QVERIFY(f.controller.speedAvailable());
        QCOMPARE(f.controller.cyclesPerSecond(), 400'000.0);
        QVERIFY(f.controller.pause());
    }

    void resetPulsesSixteenEdgesAndPreservesExistingState() {
        Fixture f(100'000, 995);
        QVERIFY(f.adapter.setSwitch(0, true));
        QVERIFY(f.adapter.setSwitch(1, true));
        QVERIFY(f.controller.step(2));
        QCOMPARE(cachedLeds(f.adapter), uint16_t(6));
        const auto prefix = f.board.structuredLog();
        f.engine.advances.clear();
        QVERIFY(f.controller.run());
        QVERIFY(f.controller.reset());
        QCOMPARE(f.board.now(), uint64_t(1013));
        QVERIFY(!f.controller.running());
        QVERIFY(!f.controller.speedAvailable());
        QVERIFY(!f.board.buttonState(vb::Button::C));
        QVERIFY(f.board.switchState(0) && f.board.switchState(1));
        QCOMPARE(cachedLeds(f.adapter), uint16_t(0));
        uint64_t resetEdges = 0;
        for (const auto& advance : f.engine.advances) {
            QVERIFY(advance.reset);
            QCOMPARE(advance.switches, uint64_t(3));
            resetEdges += advance.count;
        }
        QCOMPARE(resetEdges, uint64_t(16));
        QVERIFY(f.board.structuredLog().size() >= prefix.size());
        QVERIFY(std::equal(prefix.begin(), prefix.end(), f.board.structuredLog().begin()));
        QVERIFY(hasLog(f.board, "[cycle 997] BTNC 0->1"));
        QVERIFY(hasLog(f.board, "[cycle 1013] BTNC 1->0"));

        QVERIFY(f.adapter.setButton(0, true));
        QVERIFY(f.controller.reset());
        QCOMPARE(f.board.now(), uint64_t(1029));
        QVERIFY(f.board.buttonState(vb::Button::C)); // existing user's hold survives
        QVERIFY(f.adapter.buttons()->data(f.adapter.buttons()->index(0),
                    vb::qt::BoardIoModel::ActiveRole).toBool());
        QVERIFY(f.adapter.setButton(0, false));
        QVERIFY(f.controller.step(1));
        QCOMPARE(cachedLeds(f.adapter), uint16_t(3));
    }

    void resetKeepsQueuedUartInput() {
        Fixture f;
        f.board.sendUart(0xa5);
        f.board.sendUart(0x3c);
        QVERIFY(f.controller.reset());
        QCOMPARE(f.board.now(), uint64_t(16));
        QVERIFY(f.controller.step(208'340 - 16)); // two 8N1 frames at 10417 cycles/bit
        QCOMPARE(f.board.now(), uint64_t(208'340));
        QVERIFY(hasLog(f.board, "[cycle 0] UART RX 0xA5"));
        QVERIFY(hasLog(f.board, "[cycle 104170] UART RX 0x3C"));
        const auto rx = f.engine.lookup("uart_rx");
        QVERIFY(std::any_of(f.engine.pokes.begin(), f.engine.pokes.end(), [&](const auto& poke) {
            return poke.id == rx && poke.cycle == 10417 && poke.value == 1;
        }));
        QCOMPARE(f.engine.peek(rx), uint64_t(1));
    }

    void disconnectedAndUnavailableResetRejectCommands() {
        vb::qt::BoardAdapter disconnected;
        Controller controller(disconnected, QStringLiteral("Preview"),
                              Controller::Options{100'000, false, [] { return qint64(0); }});
        QVERIFY(!controller.connected());
        QVERIFY(!controller.canReset());
        QVERIFY(!controller.run());
        QVERIFY(!controller.pause());
        QVERIFY(!controller.step());
        QVERIFY(!controller.reset());
        controller.setRealtime(true);
        controller.processBatch();
        QVERIFY(!controller.running());
        QVERIFY(!controller.realtime());
        QVERIFY(!controller.speedAvailable());

        Fixture noReset(100'000, 0, false);
        QVERIFY(noReset.controller.connected());
        QVERIFY(!noReset.controller.canReset());
        QVERIFY(!noReset.controller.reset());
        QCOMPARE(noReset.board.now(), uint64_t(0));
    }

    void wideCycleTextAndOverflowAreExact() {
        constexpr uint64_t large = (uint64_t(1) << 53) + 1;
        Fixture wide(100'000, large);
        QCOMPARE(wide.controller.cycleText(), QStringLiteral("9007199254740993"));
        QVERIFY(wide.controller.step(1));
        QCOMPARE(wide.board.now(), large + 1);
        QCOMPARE(wide.controller.cycleText(), QStringLiteral("9007199254740994"));

        const uint64_t nearEnd = std::numeric_limits<uint64_t>::max() - 8;
        Fixture overflow(100'000, nearEnd);
        QVERIFY(!overflow.controller.step(9));
        QCOMPARE(overflow.board.now(), nearEnd);
        QVERIFY(overflow.engine.advances.empty());
        QVERIFY(!overflow.controller.errorString().isEmpty());
        QVERIFY(!overflow.controller.reset());
        QCOMPARE(overflow.board.now(), nearEnd);
        QVERIFY(overflow.engine.advances.empty());
        QVERIFY(overflow.engine.pokes.empty()); // reset overflow checked before asserting BTNC
        QVERIFY(!overflow.controller.running());
        QCOMPARE(overflow.controller.cycleText(), QString::number(nearEnd));

        // The frozen board's next-grid arithmetic cannot service the final
        // partial uint64 interval. The controller must contain that backend
        // limit before any step, reset input write, or scheduled batch.
        const uint64_t lastGrid = std::numeric_limits<uint64_t>::max() / 1000 * 1000;
        Fixture atBoundary(100'000, lastGrid - 1);
        QVERIFY(atBoundary.controller.step(1));
        QCOMPARE(atBoundary.board.now(), lastGrid);
        atBoundary.engine.advances.clear();
        atBoundary.engine.pokes.clear();
        QVERIFY(!atBoundary.controller.step(1));
        QVERIFY(!atBoundary.controller.reset());
        QCOMPARE(atBoundary.board.now(), lastGrid);
        QVERIFY(atBoundary.engine.advances.empty());
        QVERIFY(atBoundary.engine.pokes.empty());
        QVERIFY(!atBoundary.controller.running());
        QVERIFY(!atBoundary.controller.errorString().isEmpty());

        Fixture runAtBoundary(100'000, lastGrid);
        QVERIFY(runAtBoundary.controller.run());
        runAtBoundary.controller.processBatch();
        QCOMPARE(runAtBoundary.board.now(), lastGrid);
        QVERIFY(runAtBoundary.engine.advances.empty());
        QVERIFY(runAtBoundary.engine.pokes.empty());
        QVERIFY(!runAtBoundary.controller.running());
        QVERIFY(!runAtBoundary.controller.errorString().isEmpty());
    }

    void reentrantControllerAndAdapterPublicationCannotStep() {
        Fixture f(41);
        QVERIFY(f.adapter.setSwitch(0, true));
        int attempted = 0;
        bool rejected = true;
        const auto attempts = [&] {
            ++attempted;
            const uint64_t before = f.board.now();
            const bool wasRealtime = f.controller.realtime();
            rejected &= !f.controller.run();
            rejected &= !f.controller.pause();
            rejected &= !f.controller.step();
            rejected &= !f.controller.reset();
            f.controller.setRealtime(!wasRealtime);
            f.controller.processBatch();
            rejected &= f.controller.realtime() == wasRealtime && f.board.now() == before;
        };
        auto connection = connect(f.adapter.leds(), &QAbstractItemModel::dataChanged,
                                  this, attempts);
        QVERIFY(f.controller.step()); // LED publication happens inside controller.advance
        QVERIFY(attempted > 0);
        QVERIFY(rejected);
        QCOMPARE(f.board.now(), uint64_t(1));
        disconnect(connection);

        attempted = 0;
        connection = connect(&f.controller, &Controller::stateChanged, this, attempts);
        QVERIFY(f.controller.run());
        QVERIFY(attempted > 0);
        QVERIFY(rejected);
        QVERIFY(f.controller.running());
        QCOMPARE(f.board.now(), uint64_t(1));
        disconnect(connection);

        attempted = 0;
        connection = connect(f.adapter.switches(), &QAbstractItemModel::dataChanged,
                             this, attempts);
        QVERIFY(f.adapter.setSwitch(1, true)); // controller idle, adapter actively publishing
        QVERIFY(attempted > 0);
        QVERIFY(rejected);
        QVERIFY(f.controller.running());
        QCOMPARE(f.board.now(), uint64_t(1));
        disconnect(connection);
        QVERIFY(f.controller.pause());
    }

    void wrongThreadMutationsAreRejected() {
        Fixture f;
        std::array<bool, 4> results{};
        std::thread worker([&] {
            results = {f.controller.run(), f.controller.pause(),
                       f.controller.step(), f.controller.reset()};
            f.controller.setRealtime(true);
            f.controller.processBatch();
        });
        worker.join();
        for (bool result : results) QVERIFY(!result);
        QCOMPARE(f.board.now(), uint64_t(0));
        QVERIFY(!f.controller.running());
        QVERIFY(!f.controller.realtime());
        QVERIFY(f.engine.advances.empty());
        QVERIFY(f.engine.pokes.empty());
    }

    void realCounterSchedulesAreChunkAndPacingInvariant() {
        const auto constraints = counterConstraints();
        QVERIFY(constraints.warnings.empty());
        Counter direct(constraints), steps(constraints), batches(constraints);
        schedule(direct, Delivery::Direct);
        schedule(steps, Delivery::Steps);
        schedule(batches, Delivery::Batches);
        QVERIFY(direct.board.structuredLog() == steps.board.structuredLog());
        QVERIFY(direct.board.structuredLog() == batches.board.structuredLog());
        QCOMPARE(direct.board.now(), steps.board.now());
        QCOMPARE(direct.board.now(), batches.board.now());
        QCOMPARE(cachedLeds(direct.adapter), cachedLeds(steps.adapter));
        QCOMPARE(cachedLeds(direct.adapter), cachedLeds(batches.adapter));
    }
};

QTEST_GUILESS_MAIN(SimulationControllerTest)
#include "test_qt_simulation_controller.moc"
