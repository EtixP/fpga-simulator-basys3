#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace vb {

class BoardModel;
struct RunOptions;

// The scheduler is pure C++ and sees only BoardModel. These cursors belong
// to one run and persist across startup and every later advance.
struct ScriptCursor {
  size_t event = 0;
  size_t send = 0;
};

// How a scripted --send reaches the board. Empty: BoardModel::sendUartText.
// A frontend that shows sent bytes may route the text through its own record,
// provided it queues every byte exactly as sendUartText does, at the same cycle.
using ScriptSend = std::function<void(std::string_view text)>;

// Advance a fixed virtual-time budget, splitting around sorted script events.
// Events at the final cycle are applied before returning; their first
// sampling edge is the following cycle. Same-cycle switches/buttons precede
// UART sends, retaining argv order within each event type.
void advanceScripted(BoardModel& board, const RunOptions& opts,
                     ScriptCursor& cursor, uint64_t cycles,
                     const ScriptSend& send = {});

// Initialize a new scripted run at cycle 0, enabling logging before any inputs.
// For positive/unlimited runs, automatic reset spans cycles [0,16), while
// scripts retain their absolute cycles. An explicit BTNC event at/before 16
// takes ownership of reset, suppressing the automatic release. --frames 0
// applies no inputs and advances no simulation time. Positive finite runs
// consequently end at 16 + frames*cyclesPerFrame, as before.
void initializeScriptedRun(BoardModel& board, const RunOptions& opts,
                           ScriptCursor& cursor, const ScriptSend& send = {});

}  // namespace vb
