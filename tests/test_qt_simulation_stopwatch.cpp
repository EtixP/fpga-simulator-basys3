// The controller replays the frozen three-second RTL golden while changing
// pacing modes and splitting time at non-grid batch boundaries.
#include "Vstopwatch.h"
#include "board/BoardModel.h"
#include "check.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/SimulationController.h"

#include <QCoreApplication>

#include <array>
#include <fstream>
#include <iterator>

namespace {
std::string readFile(const char* name) {
    std::ifstream file(name, std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto engine = vb::makeVerilatorEngine<Vstopwatch>({.topModule = "stopwatch"});
    vb::BoardModel board(*engine, vb::PinBinding::bind(vb::parseXdc(
        readFile(VB_SOURCE_DIR "/examples/stopwatch.xdc")), *engine));
    board.setLogEnabled(true);
    vb::qt::BoardAdapter adapter(&board);
    qint64 wall = 0;
    constexpr uint64_t batch = 99'713;
    vb::qt::SimulationController controller(adapter, QStringLiteral("Stopwatch"),
        {.batchCycles = batch, .automaticScheduling = false,
         .nowNanoseconds = [&] { return wall; }});

    const auto runTo = [&](uint64_t target) {
        CHECK(target >= board.now());
        CHECK(controller.run());
        while (target - board.now() >= batch) {
            // The fake host outruns 100 MHz, exercising actual throttle waits
            // in realtime mode. Turbo uses the exact same cycle batch.
            wall += 230'000;
            controller.processBatch();
        }
        CHECK(controller.pause());
        if (target > board.now())
            CHECK(controller.step(static_cast<uint32_t>(target - board.now())));
        CHECK_EQ(board.now(), target);
        CHECK(controller.cycleText() == QString::number(target));
    };
    struct Input { uint64_t cycle; vb::Button button; bool pressed; };
    constexpr std::array<Input, 8> events{{
        {100'000, vb::Button::U, true}, {600'000, vb::Button::U, false},
        {2'000'000, vb::Button::U, true}, {4'000'000, vb::Button::U, false},
        {155'000'000, vb::Button::C, true}, {155'500'000, vb::Button::C, false},
        {157'000'000, vb::Button::U, true}, {159'000'000, vb::Button::U, false},
    }};
    bool realtime = false;
    for (const auto& event : events) {
        controller.setRealtime(realtime = !realtime);
        runTo(event.cycle);
        CHECK(adapter.setButton(static_cast<int>(event.button), event.pressed));
    }
    controller.setRealtime(true);
    runTo(300'000'000);
    CHECK(controller.virtualTimeText() == QStringLiteral("3.000000000 s"));
    std::string actual;
    for (const auto& line : board.structuredLog()) actual += line + '\n';
    CHECK(actual == readFile(VB_SOURCE_DIR "/tests/golden/stopwatch_3s.log"));
    const std::array<char, 4> expected{'1', '4', '1', '0'};
    std::array<uint8_t, 4> masks{};
    for (unsigned i = 0; i < 4; ++i) {
        CHECK_EQ(board.digitChar(i), expected[i]);
        masks[i] = board.digitSegments(i);
    }
    CHECK(board.digitDp(2));
    CHECK(board.ledState(0));

    // Toolbar reset is a real 16-cycle pulse, not a new engine/session. The
    // cached display persists until RTL multiplexing samples its reset digits.
    CHECK(controller.run());
    CHECK(controller.reset());
    CHECK(!controller.running());
    CHECK_EQ(board.now(), 300'000'016);
    CHECK(!board.buttonState(vb::Button::C));
    CHECK(!board.ledState(0));
    for (unsigned i = 0; i < 4; ++i) CHECK_EQ(board.digitSegments(i), masks[i]);
    CHECK(controller.step(200'000));
    for (unsigned i = 0; i < 4; ++i) CHECK_EQ(board.digitChar(i), '0');
    CHECK(board.digitDp(2));
    CHECK(!board.ledState(0));
    CHECK(!controller.speedAvailable());
    std::puts("test_qt_simulation_stopwatch: PASS (unchanged 300M-cycle golden)");
}
