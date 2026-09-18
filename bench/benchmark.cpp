// Headless BoardModel throughput; each run starts from an independent engine.
#include VB_MODEL_HEADER
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  try {
    const uint64_t cycles = argc > 1 ? std::stoull(argv[1]) : 20'000'000;
    const unsigned runs = argc > 2 ? std::stoul(argv[2]) : 3;
    if (!cycles || !runs) throw std::invalid_argument("cycles and runs must be positive");
    std::ifstream in(VB_XDC);
    if (!in) throw std::runtime_error("cannot read benchmark XDC");
    const std::string xdc((std::istreambuf_iterator<char>(in)), {});
    std::cout << "design,run,cycles,seconds,cycles_per_second,realtime_multiplier,frames,frames_per_second\n";
    for (unsigned run = 1; run <= runs; ++run) {
      auto engine = vb::makeVerilatorEngine<VB_MODEL>({.topModule = VB_DESIGN});
      vb::BoardModel board(*engine, vb::PinBinding::bind(vb::parseXdc(xdc), *engine));
      board.setButton(vb::Button::C, true);
      board.tick(16);
      board.setButton(vb::Button::C, false);
      for (unsigned i = 0; i < 4; ++i) board.setSwitch(i, true);
      board.setButton(vb::Button::U, true); // starts stopwatch after debounce
      if (board.hasUartRx()) board.sendUartText(std::string(512, 'U'));
      board.tick(2'000'000); // warm caches and establish stable monitor timing
      board.setButton(vb::Button::U, false);
      const uint64_t frames = board.vgaCompletedFrames();
      const uint64_t before = board.now();
      const auto start = std::chrono::steady_clock::now();
      board.tick(cycles);
      const double seconds = std::chrono::duration<double>(
          std::chrono::steady_clock::now() - start).count();
      if (board.now() - before != cycles) throw std::runtime_error("cycle accounting failed");
      if (board.hasVga() && !board.vgaOk()) throw std::runtime_error(board.vgaStatus());
      const auto completed = board.vgaCompletedFrames() - frames;
      std::cout << VB_DESIGN << ',' << run << ',' << cycles << ',' << seconds << ','
                << cycles / seconds << ',' << cycles / seconds / 100e6 << ','
                << completed << ',' << completed / seconds << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "benchmark: " << error.what() << '\n';
    return 1;
  }
}
