#pragma once
#include <array>
#include <cstdint>

namespace vb {

// Decode a lit-segment pattern (bit0=CA .. bit6=CG, 1 = segment lit) to a
// display character: '0'..'9' and hex 'A','b','C','d','E','F', ' ' for all
// dark, '?' for anything unrecognized. Free function: testable with no
// engine and no verilated design.
char decodeSevenSeg(uint8_t litSegments);

// Persistence-of-vision observer for the 4-digit multiplexed display.
// The hardware shares one set of cathodes across four digits and strobes the
// anodes; the eye fuses the strobes. This class is the eye: it latches each
// digit's content whenever its anode is sampled active and keeps it "lit"
// for a decay window of VIRTUAL time (deterministic — wall clock never
// enters, R1).
class SevenSeg {
public:
  static constexpr uint32_t kDigits = 4;

  // Decay window. Must exceed the worst full-refresh period we accept: the
  // Basys 3 reference manual recommends driving all four digits once every
  // 1..16 ms; 20 ms keeps a compliant display rock-solid. The window is
  // INCLUSIVE: content is still lit at exactly lastLit + kPersistCycles and
  // goes dark at the first observation after that — i.e. a truly stopped
  // display blanks at 20 ms + one sample chunk, worst case.
  static constexpr uint64_t kPersistCycles = 2'000'000;  // 20 ms @ 100 MHz

  // Sample the RAW pin values at an observation-grid crossing. All display
  // pins are ACTIVE LOW on this board (anBits bit i low = digit i driven;
  // segBits bit i low = cathode CA+i lit; dpPin low = point lit).
  void sample(uint64_t nowCycles, uint8_t anBits, uint8_t segBits, bool dpPin);

  // The fused (eye-emulated) view at virtual time nowCycles: latched content
  // if the digit was strobed within the decay window, else dark.
  uint8_t segments(uint64_t nowCycles, uint32_t digit) const;  // lit mask; 0 = dark
  bool dp(uint64_t nowCycles, uint32_t digit) const;

  // Cycle of the digit's most recent active-anode sample (kNeverLit if none).
  // Exposed for tests and the structured log: the mux-freshness assertion
  // needs a bound far tighter than the decay window.
  static constexpr uint64_t kNeverLit = ~0ull;
  uint64_t lastLit(uint32_t digit) const;

private:
  struct Digit {
    uint8_t seg = 0;   // lit mask as latched
    bool dp = false;
    uint64_t lastLit = kNeverLit;
  };
  std::array<Digit, kDigits> digits_{};
};

}  // namespace vb
