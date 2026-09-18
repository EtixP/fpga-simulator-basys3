#include "gui/GuiRunner.h"

#include "board/BoardModel.h"
#include "gui/GuiApp.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace vb {

void advanceScripted(BoardModel& board, const GuiOptions& opts,
                     ScriptCursor& cursor, uint64_t cycles) {
  if (cycles > std::numeric_limits<uint64_t>::max() - board.now())
    throw std::overflow_error("scripted advance exceeds the virtual cycle range");
  const uint64_t end = board.now() + cycles;
  while (true) {
    while (cursor.event < opts.stimulus.size() &&
           opts.stimulus[cursor.event].cycle <= board.now()) {
      applyStimulus(board, opts.stimulus[cursor.event]);
      ++cursor.event;
    }
    while (cursor.send < opts.sends.size() &&
           opts.sends[cursor.send].cycle <= board.now()) {
      board.sendUartText(opts.sends[cursor.send].text);
      ++cursor.send;
    }
    if (board.now() == end) break;
    uint64_t target = end;
    if (cursor.event < opts.stimulus.size())
      target = std::min(target, opts.stimulus[cursor.event].cycle);
    if (cursor.send < opts.sends.size())
      target = std::min(target, opts.sends[cursor.send].cycle);
    board.tick(target - board.now());
  }
}

void initializeDemoRun(BoardModel& board, const GuiOptions& opts,
                       ScriptCursor& cursor) {
  if (board.now() != 0 || cursor.event != 0 || cursor.send != 0)
    throw std::logic_error("demo startup requires a new board and script cursor");
  if (!opts.logPath.empty()) board.setLogEnabled(true);
  if (opts.maxFrames == 0) return;

  board.setButton(Button::C, true);
  advanceScripted(board, opts, cursor, 16);
  // Explicit user control of BTNC persists just like any other board input.
  // In particular, a hold starting during startup must not be truncated at 16.
  const bool scriptedReset = std::any_of(
      opts.stimulus.begin(), opts.stimulus.begin() + cursor.event,
      [](const StimulusEvent& event) { return event.name == "BTNC"; });
  if (!scriptedReset) board.setButton(Button::C, false);
}

}  // namespace vb
