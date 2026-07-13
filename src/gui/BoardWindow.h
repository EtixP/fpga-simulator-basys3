#pragma once

namespace vb {

class BoardModel;

// Draws the virtual Basys 3 window (16 LEDs, 16 switches, cycle readout).
// This layer sees ONLY BoardModel — no SimEngine, no Verilator, no binding —
// so the frontend stays disposable and peripherals slot in as siblings.
void drawBoardWindow(BoardModel& board);

}  // namespace vb
