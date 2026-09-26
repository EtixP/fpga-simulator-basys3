#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

class BoardModel;

// One scripted input event. CYCLE-indexed, deliberately not frame-indexed:
// cycles are the engine's native, pacing-invariant unit (R1), so scripts
// keep their meaning across cyclesPerFrame tweaks and phase-2 realtime
// pacing. An event at cycle N is applied when now()==N (poked before further
// stepping), so the design first samples it at edge N+1 — matching the
// structured log's input semantics.
struct StimulusEvent {
  uint64_t cycle = 0;
  std::string name;  // SW0..SW15, BTNC, BTNU, BTNL, BTNR, BTND
  bool value = false;
};

struct StimulusParse {
  // Sorted by cycle; stable, so same-cycle events keep argv order
  // (last-wins per name follows naturally at application time).
  std::vector<StimulusEvent> events;
  std::vector<std::string> errors;
};

// Parse "--at" payloads of the form "CYCLE:NAME=V" (e.g. "2000000:BTNU=1").
StimulusParse parseStimulus(const std::vector<std::string>& specs);

// Apply one event through the same BoardModel entry points the GUI widgets
// use. Returns false if the name is not an input resource on this board.
bool applyStimulus(BoardModel& board, const StimulusEvent& e);

}  // namespace vb
