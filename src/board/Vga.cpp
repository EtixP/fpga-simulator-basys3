#include "board/Vga.h"

namespace vb {

VgaFrameAssembler::VgaFrameAssembler(const Lanes& lanes)
    : lanes_(lanes),
      working_(kWidth * kHeight * 3, 0),
      completed_(kWidth * kHeight * 3, 0) {}

void VgaFrameAssembler::fail(const std::string& msg) {
  // First issue of the current period; published as status() when the period
  // finalizes, so it does not outlive the frame it describes.
  if (periodErrors_.empty()) periodErrors_ = msg;
}

uint32_t VgaFrameAssembler::extractColor(uint64_t word, const uint32_t bits[4]) const {
  uint32_t v = 0;
  for (uint32_t i = 0; i < 4; ++i) v |= ((word >> bits[i]) & 1u) << i;
  return v * 17u;  // 4-bit -> 8-bit replication (0x0->0x00, 0xF->0xFF)
}

void VgaFrameAssembler::consume(uint64_t startCycle, const uint64_t* samples,
                                uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t cycle = startCycle + i;
    const uint64_t word = samples[i];
    const bool hs = (word >> lanes_.hsync) & 1u;
    const bool vs = (word >> lanes_.vsync) & 1u;

    if (prevVsync_ && !vs) onVsyncFall(cycle);
    if (prevHsync_ && !hs) onHsyncFall(cycle);
    if (!prevHsync_ && hs && haveHsyncFall_) {  // hsync rising edge
      hsyncLowCycles_ = cycle - prevHsyncFall_;
      haveHsyncLow_ = true;
    }
    if (!prevVsync_ && vs && haveVsyncFall_) {
      vsyncLowCycles_ = cycle - vsyncFallCycle_;
      haveVsyncLow_ = true;
    }

    // Sample the visible pixels of the current row at their reconstructed
    // centers. >= + while is robust to any cpp; samples are contiguous so it
    // normally advances one column per cpp cycles.
    while (inVisibleRow_ && col_ < kWidth && cycle >= nextPixelCycle_) {
      const uint32_t r = extractColor(word, lanes_.red);
      const uint32_t g = extractColor(word, lanes_.green);
      const uint32_t b = extractColor(word, lanes_.blue);
      const size_t idx = (static_cast<size_t>(row_) * kWidth + col_) * 3;
      working_[idx + 0] = static_cast<uint8_t>(r);
      working_[idx + 1] = static_cast<uint8_t>(g);
      working_[idx + 2] = static_cast<uint8_t>(b);
      ++col_;
      ++pixelsThisFrame_;
      nextPixelCycle_ += cpp_;
    }

    prevHsync_ = hs;
    prevVsync_ = vs;
  }
}

void VgaFrameAssembler::onHsyncFall(uint64_t cycle) {
  // Measure cycles-per-pixel from the hsync period; error on non-integer or
  // inconsistent measurements rather than rendering wrong.
  if (haveHsyncFall_) {
    const uint64_t period = cycle - prevHsyncFall_;
    if (period == 0 || period % kHTotal != 0) {
      fail("hsync period " + std::to_string(period) +
           " is not a whole multiple of 800 pixel clocks");
    } else {
      const uint32_t measured = static_cast<uint32_t>(period / kHTotal);
      if (cpp_ != 0 && measured != cpp_)
        fail("cycles-per-pixel changed between lines (" + std::to_string(cpp_) +
             " -> " + std::to_string(measured) + ")");
      cpp_ = measured;
    }
    // Polarity: active-low hsync is low ~12% of the period (96/800). A mostly-
    // low pulse means the design drives hsync active-HIGH — which the
    // falling-edge model would render as a horizontally shifted image, so
    // flag it rather than sample garbage silently (the VGA analog of the
    // seven-seg active-low convention).
    if (haveHsyncLow_ && period != 0 && hsyncLowCycles_ * 2 > period)
      fail("hsync appears active-HIGH (low " + std::to_string(hsyncLowCycles_) +
           " of " + std::to_string(period) + " cycles); this model expects "
           "active-low sync");
  }
  prevHsyncFall_ = cycle;
  haveHsyncFall_ = true;

  if (!haveVsyncFall_) return;  // not inside a frame period yet
  ++hsyncFallsThisPeriod_;

  inVisibleRow_ = false;
  if (cpp_ != 0) {
    const uint64_t firstPixel =
        cycle + static_cast<uint64_t>(kHVisibleOffsetPix) * cpp_ + cpp_ / 2;
    // Locate the reconstructed pixel within the Vsync-relative scanline,
    // rather than counting Hsync edges inclusively. Vsync can fall at the
    // start of visible/front-porch timing (the shipped demo) or coincide
    // with Hsync. Counting their coincident falls as line 1 introduces a
    // one-row shift while passing all period checks.
    const uint64_t line =
        (firstPixel - vsyncFallCycle_) / (static_cast<uint64_t>(kHTotal) * cpp_);
    if (line >= kVBlankLines && line < kVBlankLines + kHeight) {
      inVisibleRow_ = true;
      row_ = static_cast<uint32_t>(line - kVBlankLines);
      col_ = 0;
      nextPixelCycle_ = firstPixel;
    }
  }
}

void VgaFrameAssembler::onVsyncFall(uint64_t cycle) {
  if (haveVsyncFall_) finalizeFrame(cycle);  // the period that just ended is complete
  // Start a new period with a clean error slate (a startup transient clears
  // once a good frame follows).
  periodErrors_.clear();
  haveVsyncFall_ = true;
  vsyncFallCycle_ = cycle;
  haveVsyncLow_ = false;
  hsyncFallsThisPeriod_ = 0;
  inVisibleRow_ = false;
  pixelsThisFrame_ = 0;
}

void VgaFrameAssembler::finalizeFrame(uint64_t cycle) {
  // Structure checks — a malformed period is reported, not shipped as a golden.
  if (hsyncFallsThisPeriod_ + 1 < kVTotal || hsyncFallsThisPeriod_ > kVTotal + 1)
    fail("expected ~525 hsync periods per frame, saw " +
         std::to_string(hsyncFallsThisPeriod_));
  if (pixelsThisFrame_ != kWidth * kHeight)
    fail("frame filled " + std::to_string(pixelsThisFrame_) + " of " +
         std::to_string(kWidth * kHeight) + " pixels");
  const uint64_t period = cycle - vsyncFallCycle_;
  if (haveVsyncLow_ && vsyncLowCycles_ > period / 2)
    fail("vsync appears active-HIGH (low " + std::to_string(vsyncLowCycles_) +
         " of " + std::to_string(period) + " cycles); this model expects "
         "active-low sync");
  if (cpp_ != 0 && period != static_cast<uint64_t>(kHTotal) * kVTotal * cpp_)
    fail("frame period " + std::to_string(period) + " != " +
         std::to_string(static_cast<uint64_t>(kHTotal) * kVTotal * cpp_));

  completed_ = working_;      // publish the just-finished frame
  errors_ = periodErrors_;    // its health becomes the public status
  ++completedFrames_;
  lastFrameCycle_ = cycle;
}

}  // namespace vb
