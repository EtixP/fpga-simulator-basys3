#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

// A monitor model for 640x480@60 VGA. Like real glass it sees ONLY the sync
// and color signals — never the design's clocks — and reconstructs frames
// from sync edges. Engine-free: it consumes packed per-cycle samples (from
// SimEngine::stepCapture) and is unit-testable with synthetic waveforms.
//
// Timing truth (VESA 640x480@60, both syncs active-low): line = 800 pixel
// clocks, frame = 525 lines. From a sync FALLING edge (start of pulse) the
// visible region is offset by (sync + back porch): 144 pixel clocks
// horizontally (96 + 48), 35 lines vertically (2 + 33). The pixel clock is
// RTL-derived (a clock-enable divider off the master, R1) and NEVER assumed:
// cyclesPerPixel is MEASURED from the hsync period / 800.
//
// A frame is delimited by successive Vsync falling edges; "one completed
// frame = one full vsync-to-vsync period". Startup partiality is excluded by
// construction (nothing before the first falling edge is a frame). Frame 0 =
// between falling edges 1 and 2; frame 1 = edges 2->3; and so on.
class VgaFrameAssembler {
public:
  static constexpr uint32_t kWidth = 640;
  static constexpr uint32_t kHeight = 480;
  static constexpr uint32_t kHTotal = 800;   // visible 640 + fp 16 + sync 96 + bp 48
  static constexpr uint32_t kVTotal = 525;   // visible 480 + fp 10 + sync 2 + bp 33
  static constexpr uint32_t kHVisibleOffsetPix = 144;  // hsync fall -> visible px 0 (96+48)
  static constexpr uint32_t kVBlankLines = 35;         // vsync fall -> visible row 0 (2+33)

  // Bit position of each VGA signal within the packed sample word. The board
  // fills these from its pin-keyed bindings (a color channel's four bits may
  // be non-contiguous or reordered), so the assembler extracts each bit
  // individually and assumes no port shape.
  struct Lanes {
    uint32_t hsync = 0;
    uint32_t vsync = 0;
    uint32_t red[4] = {0, 0, 0, 0};
    uint32_t green[4] = {0, 0, 0, 0};
    uint32_t blue[4] = {0, 0, 0, 0};
  };

  explicit VgaFrameAssembler(const Lanes& lanes);

  // Feed a contiguous run of packed samples, one per cycle, starting at
  // absolute cycle `startCycle`. Segment-agnostic: state carries across calls,
  // so BoardModel's grid/UART-split tick loop can hand it arbitrary-length
  // runs and frame stamps stay exact.
  void consume(uint64_t startCycle, const uint64_t* samples, uint32_t count);

  // --- completed-frame accessors ------------------------------------------
  uint64_t completedFrames() const { return completedFrames_; }
  // Framebuffer of the latest completed frame: kWidth*kHeight*3 bytes, RGB888,
  // row 0 = TOP visible line, column 0 = LEFT. Empty (all zero) until the
  // first frame completes.
  const std::vector<uint8_t>& framebuffer() const { return completed_; }
  uint64_t lastFrameCycle() const { return lastFrameCycle_; }  // vsync-fall cycle
  uint32_t measuredCyclesPerPixel() const { return cpp_; }

  // --- status (surfaced in the GUI, asserted clean in the golden) ----------
  // Reflects the health of the MOST RECENTLY COMPLETED frame — not a permanent
  // latch. Like a real monitor, a startup transient (e.g. the power-on
  // sync-asserted state before the design is reset) clears once a clean frame
  // is produced. ok() is true when the latest completed frame was faithfully
  // rendered.
  bool ok() const { return errors_.empty(); }
  const std::string& status() const { return errors_; }

private:
  void onHsyncFall(uint64_t cycle);
  void onVsyncFall(uint64_t cycle);
  void finalizeFrame(uint64_t cycle);
  uint32_t extractColor(uint64_t word, const uint32_t bits[4]) const;
  void fail(const std::string& msg);

  Lanes lanes_;

  bool prevHsync_ = true;  // idle HIGH (active-low) -> no phantom edge at t=0
  bool prevVsync_ = true;

  uint32_t cpp_ = 0;                 // measured cycles per pixel
  uint64_t prevHsyncFall_ = 0;
  bool haveHsyncFall_ = false;
  uint64_t hsyncLowCycles_ = 0;      // duration of the last hsync-low pulse
  bool haveHsyncLow_ = false;

  uint64_t vsyncFallCycle_ = 0;
  bool haveVsyncFall_ = false;       // a period has started
  uint32_t lineCounter_ = 0;         // hsync falls since the vsync fall
  uint32_t hsyncFallsThisPeriod_ = 0;

  // Current visible row being filled (if any).
  bool inVisibleRow_ = false;
  uint32_t row_ = 0;
  uint32_t col_ = 0;
  uint64_t nextPixelCycle_ = 0;
  uint32_t pixelsThisFrame_ = 0;

  std::vector<uint8_t> working_;     // frame under construction
  std::vector<uint8_t> completed_;   // latest fully-observed frame
  uint64_t completedFrames_ = 0;
  uint64_t lastFrameCycle_ = 0;
  std::string periodErrors_;  // issues seen in the period under construction
  std::string errors_;        // health of the latest COMPLETED frame
};

}  // namespace vb
