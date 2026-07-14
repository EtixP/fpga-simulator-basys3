#pragma once
#include "gui/Stimulus.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vb {

class BoardModel;

struct GuiOptions {
  // Fixed virtual-time budget per rendered frame (R1: virtual time is never
  // tied to wall clock — "step until 16 ms elapsed" would make turbo and
  // realtime diverge; honest pacing is phase 2). Real-time at 60 fps would
  // be ~1'666'667; the default is deliberately slower so fast-counting
  // designs stay watchable. Demos may tweak.
  uint64_t cyclesPerFrame = 100'000;
  long maxFrames = -1;          // exit after N rendered frames (smoke/screenshot)
  std::string screenshotPath;   // BMP dump of the final frame
  std::string logPath;          // enables the structured log; written at exit
  std::vector<StimulusEvent> stimulus;  // cycle-indexed, sorted by cycle
  std::string windowTitle = "VirtualBasys";
};

// Shared CLI for the per-demo executables:
//   --xdc PATH  --frames N  --screenshot out.bmp  --log out.log
//   --at CYCLE:NAME=V (repeatable)   --switches 0111 (sugar for --at 0:SWn=1)
struct DemoArgs {
  std::string xdcPath;
  GuiOptions gui;
  std::vector<std::string> errors;
};
DemoArgs parseDemoArgs(int argc, char** argv, std::string defaultXdc);

// Run the SDL2/Metal/ImGui board window until closed or maxFrames rendered.
// Defined in GuiApp.mm (GUI builds only); everything above is pure C++.
int runBoardGui(BoardModel& board, const GuiOptions& opts);

}  // namespace vb
