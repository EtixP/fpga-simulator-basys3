#pragma once
// Shared body for the per-demo GUI executables (one verilated design per
// executable, per the decision log). The including main provides the
// generated VModel type; everything past construction goes through
// BoardModel.
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "gui/GuiApp.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace vb {

inline std::string demoReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.good()) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

template <class VModel>
int runDemo(int argc, char** argv, const char* topModule, const char* title,
            const char* defaultXdc, uint64_t cyclesPerFrame = 100'000) {
  DemoArgs args = parseDemoArgs(argc, argv, defaultXdc);
  for (const auto& e : args.errors) std::fprintf(stderr, "error: %s\n", e.c_str());
  if (!args.errors.empty()) return 1;
  args.gui.windowTitle = title;
  args.gui.cyclesPerFrame = cyclesPerFrame;  // demo-local (VGA uses ~1 frame/tick)

  const std::string xdcText = demoReadFile(args.xdcPath);
  if (xdcText.empty()) {
    std::fprintf(stderr, "error: cannot read XDC '%s'\n", args.xdcPath.c_str());
    return 1;
  }
  auto engine = makeVerilatorEngine<VModel>({.topModule = topModule});
  const XdcDoc xdc = parseXdc(xdcText);
  for (const auto& w : xdc.warnings) std::fprintf(stderr, "xdc %s\n", w.c_str());
  PinBinding binding = PinBinding::bind(xdc, *engine);
  for (const auto& d : binding.diagnostics())
    std::fprintf(stderr, "bind %s\n", d.c_str());
  BoardModel board(*engine, std::move(binding));
  // R6: every shipped example resets via btnC. Pulse it at startup so the
  // design leaves power-on 2-state state (e.g. sync outputs zero-init to their
  // ASSERTED level) before its output is observed — the headless tests do the
  // same. No-op for a design without btnC. Shifts the demo's time origin to
  // now()=16, so a cycle-indexed --at/--switches event scheduled at cycle <=16
  // lands at 16 (imperceptible; no test uses this path).
  board.setButton(Button::C, true);
  board.tick(16);
  board.setButton(Button::C, false);
  return runBoardGui(board, args.gui);
}

}  // namespace vb
