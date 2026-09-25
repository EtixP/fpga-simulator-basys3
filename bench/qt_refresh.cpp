// Cost of the adapter's per-refresh work (inspector snapshot, log draining)
// relative to simulation: fixed 100,000-cycle batches with no refresh, with a
// refresh after every batch, with the board's own logging alone (formatted
// then cleared, no adapter), and with the event log recording. The controller
// refreshes at most every ~16 ms, so one refresh per batch overstates the
// running app's refresh rate.
#include VB_MODEL_HEADER
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/BoardAdapter.h"

#include <QCoreApplication>

#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace {
uint64_t positive(const char* text) {
    const std::string_view input(text);
    uint64_t result = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), result);
    if (error != std::errc{} || end != input.data() + input.size() || !result)
        throw std::invalid_argument("cycles and runs must be positive integers");
    return result;
}

void measure(const char* mode, uint64_t cycles, uint64_t run) {
    auto engine = vb::makeVerilatorEngine<VB_MODEL>({.topModule = VB_DESIGN});
    std::ifstream input(VB_XDC);
    if (!input) throw std::runtime_error("cannot read example constraints");
    const std::string xdc((std::istreambuf_iterator<char>(input)), {});
    vb::BoardModel board(*engine, vb::PinBinding::bind(vb::parseXdc(xdc), *engine));
    vb::qt::BoardAdapter adapter(&board);
    // The same stimulus as the controller benchmark: reset, switches, BTNU.
    board.setButton(vb::Button::C, true);
    board.tick(16);
    board.setButton(vb::Button::C, false);
    for (unsigned i = 0; i < 4; ++i) board.setSwitch(i, true);
    board.setButton(vb::Button::U, true);
    if (board.hasUartRx()) adapter.sendUartText(QString(512, QLatin1Char('U')));
    board.tick(2'000'000);
    board.setButton(vb::Button::U, false);
    const std::string_view kind(mode);
    if (kind == "refresh_logging" && !adapter.setLogRecording(true))
        throw std::runtime_error("log recording refused");
    // The board's own logging cost, with no adapter: format and discard.
    if (kind == "board_logging") board.setLogEnabled(true);
    adapter.refresh();

    const auto start = std::chrono::steady_clock::now();
    uint64_t refreshes = 0;
    for (uint64_t done = 0; done < cycles; done += 100'000) {
        board.tick(100'000);
        if (kind == "board_logging") {
            board.clearLog();
        } else if (kind != "tick") {
            adapter.refresh();
            ++refreshes;
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << VB_DESIGN << ',' << mode << ',' << run << ',' << cycles << ',' << seconds << ','
              << cycles / seconds << ',' << refreshes << ',' << adapter.inspector()->count() << ','
              << adapter.eventLog()->recordedEvents() << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        if (argc > 3) throw std::invalid_argument("usage: benchmark_qt_refresh_* [cycles] [runs]");
        const uint64_t cycles = argc > 1 ? positive(argv[1]) : 20'000'000;
        const uint64_t runs = argc > 2 ? positive(argv[2]) : 3;
        if (cycles > 1'000'000'000 || runs > 100 || cycles % 100'000)
            throw std::invalid_argument("cycles: a multiple of 100000 up to 1 billion; runs <= 100");
        std::cout << "design,mode,run,cycles,seconds,cycles_per_second,refreshes,inspected_signals,"
                     "logged_events\n"
                  << std::setprecision(9);
        const char* modes[] = {"tick", "refresh", "board_logging", "refresh_logging"};
        for (uint64_t run = 1; run <= runs; ++run)
            for (unsigned order = 0; order < 4; ++order)
                measure(modes[(order + run - 1) % 4], cycles, run);
    } catch (const std::exception& error) {
        std::cerr << "benchmark: " << error.what() << '\n';
        return 1;
    }
}
