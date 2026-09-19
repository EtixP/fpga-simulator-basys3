// Paired BoardModel versus BoardModel + Qt snapshot throughput; no rendering.
#include "Vcounter.h"
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/BoardAdapter.h"

#include <QCoreApplication>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr uint64_t kWarmCycles = 2'000'000;
constexpr uint64_t kChunkCycles = 100'000;

uint64_t positiveArgument(std::string_view text) {
  uint64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0)
    throw std::invalid_argument("cycles and runs must be positive integers");
  return value;
}

double measure(const std::string& xdc, uint64_t cycles, bool withAdapter) {
  auto engine = vb::makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  vb::BoardModel board(*engine, vb::PinBinding::bind(vb::parseXdc(xdc), *engine));
  board.setButton(vb::Button::C, true);
  board.tick(16);
  board.setButton(vb::Button::C, false);
  for (unsigned i = 0; i < 4; ++i) board.setSwitch(i, true);
  std::unique_ptr<vb::qt::BoardAdapter> adapter;
  if (withAdapter) adapter = std::make_unique<vb::qt::BoardAdapter>(&board);

  const auto advance = [&](uint64_t count) {
    while (count) {
      const uint64_t chunk = std::min(count, kChunkCycles);
      board.tick(chunk);
      if (adapter && !adapter->refresh()) throw std::runtime_error("adapter refresh rejected");
      count -= chunk;
    }
  };
  advance(kWarmCycles);
  const uint64_t before = board.now();
  const auto start = std::chrono::steady_clock::now();
  advance(cycles);
  const double seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  if (before != 16 + kWarmCycles || board.now() - before != cycles)
    throw std::runtime_error("cycle accounting failed");

  // Reduce before multiplying so the oracle also handles large cycle arguments.
  const uint64_t expected = (15 * ((kWarmCycles + cycles) % 65'536)) % 65'536;
  for (unsigned i = 0; i < vb::BoardModel::kLedCount; ++i) {
    const bool bit = ((expected >> i) & 1) != 0;
    if (board.ledState(i) != bit) throw std::runtime_error("counter arithmetic failed");
    if (adapter && adapter->leds()->data(adapter->leds()->index(i, 0),
        vb::qt::BoardIoModel::ActiveRole).toBool() != bit)
      throw std::runtime_error("adapter LED snapshot mismatch");
  }
  return seconds;
}

} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    if (argc > 3) throw std::invalid_argument("usage: benchmark_qt_adapter [cycles] [runs]");
    const uint64_t cycles = argc > 1 ? positiveArgument(argv[1]) : 20'000'000;
    const uint64_t runs = argc > 2 ? positiveArgument(argv[2]) : 3;
    if (cycles > std::numeric_limits<uint64_t>::max() - kWarmCycles - 16)
      throw std::invalid_argument("cycle count exceeds the virtual-time range");
    std::ifstream in(VB_XDC);
    if (!in) throw std::runtime_error("cannot read benchmark XDC");
    const std::string xdc((std::istreambuf_iterator<char>(in)), {});
    std::cout << "mode,run,cycles,seconds,cycles_per_second\n" << std::setprecision(9);
    for (uint64_t run = 0; run < runs; ++run) {
      for (unsigned order = 0; order < 2; ++order) {
        const bool withAdapter = ((run + order) % 2) != 0;
        const double seconds = measure(xdc, cycles, withAdapter);
        std::cout << (withAdapter ? "adapter" : "board") << ',' << run + 1 << ','
                  << cycles << ',' << seconds << ',' << cycles / seconds << '\n';
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "benchmark: " << error.what() << '\n';
    return 1;
  }
}
