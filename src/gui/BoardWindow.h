#pragma once
#include <cstdint>

namespace vb {

class BoardModel;

// What the GUI runner hands the draw layer for the VGA panel: an opaque
// texture handle (an id<MTLTexture> reinterpreted as ImTextureID; the draw
// code never sees ObjC) plus the honest, UNSMOOTHED per-frame speed readout.
// The banner numbers are the raw measured values for the frame just rendered
// — never a rolling max or average — so the indicator can never oversell
// (R1: "Never silently pretend to be real-time").
struct VgaView {
  uint64_t textureId = 0;  // 0 = no VGA / no frame yet
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t frameIndex = 0;      // latest completed VGA frame
  double simMHz = 0.0;          // measured sim speed this frame
  double realtimeMultiplier = 0.0;
  double fps = 0.0;             // measured wall fps this frame
};

// Draws the virtual Basys 3 window. This layer sees ONLY BoardModel and the
// opaque VgaView — no SimEngine, no Verilator, no ObjC — so the frontend
// stays disposable and peripherals slot in as siblings.
void drawBoardWindow(BoardModel& board, const VgaView& vga);

}  // namespace vb
