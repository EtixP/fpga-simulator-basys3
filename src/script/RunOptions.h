#pragma once
#include "script/Stimulus.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vb {

// Scripted UART send: TEXT is queued to the board at CYCLE (bits then drive
// the RX line at exact cycles from there).
struct SendEvent {
  uint64_t cycle = 0;
  std::string text;
};

// A scripted run, as the launchers and tests describe it. Virtual time is never
// tied to wall clock (R1): a run's length is a fixed cycle budget, frames *
// cyclesPerFrame after the 16-cycle startup reset, however fast it is shown.
struct RunOptions {
  // Fixed virtual-time budget per frame. Real-time at 60 fps would be
  // ~1'666'667; the default is deliberately slower so fast-counting designs
  // stay watchable. Launchers may tweak (VGA uses one VGA frame per frame).
  uint64_t cyclesPerFrame = 100'000;
  long maxFrames = -1;          // end the run after N frames (smoke/screenshot)
  std::string screenshotPath;   // image of the window when a finite run ends
  std::string logPath;          // enables the structured log; written at exit
  std::vector<StimulusEvent> stimulus;  // cycle-indexed, sorted by cycle
  std::vector<SendEvent> sends;         // cycle-indexed, sorted by cycle
};

// Shared CLI for the launchers:
//   --xdc PATH  --frames N  --screenshot FILE  --log FILE
//   --at CYCLE:NAME=V (repeatable)   --switches 0111 (sugar for --at 0:SWn=1)
//   --send CYCLE:TEXT (repeatable; queues TEXT to the UART at CYCLE)
struct RunArgs {
  std::string xdcPath;
  RunOptions run;
  std::vector<std::string> errors;
};
RunArgs parseRunArgs(int argc, char** argv, std::string defaultXdc);

}  // namespace vb
