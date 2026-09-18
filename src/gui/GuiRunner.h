#pragma once
#include <cstddef>
#include <cstdint>

namespace vb {

class BoardModel;
struct GuiOptions;

// The scheduler is pure C++ and sees only BoardModel. These cursors belong
// to one run and persist across startup and every GUI frame.
struct ScriptCursor {
  size_t event = 0;
  size_t send = 0;
};

// Advance a fixed virtual-time budget, splitting around sorted script events.
// Events at the final cycle are applied before returning; their first
// sampling edge is the following cycle. Same-cycle switches/buttons precede
// UART sends, retaining argv order within each event type.
void advanceScripted(BoardModel& board, const GuiOptions& opts,
                     ScriptCursor& cursor, uint64_t cycles);

// Initialize a new demo run at cycle 0, enabling logging before any inputs.
// For positive/unlimited runs, automatic reset spans cycles [0,16), while
// scripts retain their absolute cycles. An explicit BTNC event at/before 16
// takes ownership of reset, suppressing the automatic release. --frames 0
// applies no inputs and advances no simulation time. Positive finite runs
// consequently end at 16 + frames*cyclesPerFrame, as before.
void initializeDemoRun(BoardModel& board, const GuiOptions& opts,
                       ScriptCursor& cursor);

}  // namespace vb
