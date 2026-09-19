// Real Verilator integration with independent arithmetic and cadence oracles.
#include "Vcounter.h"
#include "board/BoardModel.h"
#include "check.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/BoardAdapter.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>

namespace {
struct Counter {
    explicit Counter(const vb::XdcDoc& xdc)
        : engine(vb::makeVerilatorEngine<Vcounter>({.topModule = "counter"})),
          board(*engine, vb::PinBinding::bind(xdc, *engine)), adapter(&board) {
        board.setLogEnabled(true);
    }
    std::unique_ptr<vb::SimEngine> engine;
    vb::BoardModel board;
    vb::qt::BoardAdapter adapter;
};

uint16_t cachedLeds(const vb::qt::BoardAdapter& adapter) {
    uint16_t value = 0;
    auto* leds = adapter.leds();
    for (int i = 0; i < 16; ++i)
        if (leds->data(leds->index(i), vb::qt::BoardIoModel::ActiveRole).toBool())
            value |= uint16_t(1u << i);
    return value;
}

void runSchedule(Counter& counter, bool viaAdapter) {
    auto setSwitch = [&](int i, bool value) {
        if (viaAdapter) CHECK(counter.adapter.setSwitch(i, value));
        else counter.board.setSwitch(i, value);
    };
    auto setReset = [&](bool value) {
        if (viaAdapter) CHECK(counter.adapter.setButton(0, value));
        else counter.board.setButton(vb::Button::C, value);
    };
    auto advance = [&](uint64_t cycles) {
        if (!viaAdapter) {
            counter.board.tick(cycles);
            return;
        }
        constexpr std::array<uint64_t, 5> chunks{1, 7, 991, 2, 37};
        size_t chunk = 0;
        while (cycles) {
            const auto count = std::min(cycles, chunks[chunk++ % chunks.size()]);
            counter.board.tick(count);
            CHECK(counter.adapter.refresh());
            // UI reads and extra refreshes cannot affect the simulation schedule.
            (void)cachedLeds(counter.adapter);
            CHECK(counter.adapter.refresh());
            cycles -= count;
        }
    };
    setSwitch(0, true);
    setSwitch(2, true);       // count += 5
    advance(997);
    setSwitch(1, true);       // count += 7
    advance(1003);
    setReset(true);
    advance(1);              // synchronous reset at edge 2001
    setReset(false);
    setSwitch(0, false);      // count += 6
    advance(1010);
    CHECK(counter.adapter.refresh());
    CHECK_EQ(counter.board.now(), 3011);
    CHECK_EQ(cachedLeds(counter.adapter), 6060);
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    CHECK(argc == 2);
    std::ifstream file(argv[1]);
    CHECK(file.good());
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    const auto xdc = vb::parseXdc(text);

    Counter counter(xdc);
    CHECK(counter.adapter.connected());
    CHECK(!counter.adapter.hasDisplay());
    CHECK(counter.adapter.setSwitch(0, true));
    CHECK(counter.adapter.setSwitch(1, true));
    CHECK_EQ(counter.board.now(), 0);
    counter.board.tick(1001);
    CHECK_EQ(cachedLeds(counter.adapter), 0);  // explicit cache publication
    CHECK(counter.adapter.refresh());
    CHECK_EQ(cachedLeds(counter.adapter), 3003);
    CHECK_EQ(counter.board.now(), 1001);
    CHECK(counter.adapter.setButton(0, true));
    CHECK_EQ(cachedLeds(counter.adapter), 3003); // no invented reset edge
    counter.board.tick(1);
    CHECK(counter.adapter.refresh());
    CHECK_EQ(cachedLeds(counter.adapter), 0);

    Counter direct(xdc);
    Counter adapted(xdc);
    runSchedule(direct, false);
    runSchedule(adapted, true);
    CHECK(direct.board.structuredLog() == adapted.board.structuredLog());
    CHECK_EQ(direct.engine->now(), adapted.engine->now());
    std::puts("test_qt_counter: PASS");
}
