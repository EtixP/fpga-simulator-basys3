#include "board/SevenSeg.h"

namespace vb {

char decodeSevenSeg(uint8_t litSegments) {
  switch (litSegments) {
    case 0x00: return ' ';
    case 0x3F: return '0';
    case 0x06: return '1';
    case 0x5B: return '2';
    case 0x4F: return '3';
    case 0x66: return '4';
    case 0x6D: return '5';
    case 0x7D: return '6';
    case 0x07: return '7';
    case 0x7F: return '8';
    case 0x6F: return '9';
    case 0x77: return 'A';
    case 0x7C: return 'b';
    case 0x39: return 'C';
    case 0x5E: return 'd';
    case 0x79: return 'E';
    case 0x71: return 'F';
    case 0x40: return '-';
    default: return '?';
  }
}

void SevenSeg::sample(uint64_t nowCycles, uint8_t anBits, uint8_t segBits, bool dpPin) {
  const uint8_t lit = static_cast<uint8_t>(~segBits) & 0x7F;
  for (uint32_t i = 0; i < kDigits; ++i) {
    if ((anBits >> i) & 1) continue;  // anode high = digit not driven
    digits_[i].seg = lit;
    digits_[i].dp = !dpPin;
    digits_[i].lastLit = nowCycles;
  }
}

uint8_t SevenSeg::segments(uint64_t nowCycles, uint32_t digit) const {
  if (digit >= kDigits) return 0;
  const Digit& d = digits_[digit];
  if (d.lastLit == kNeverLit || nowCycles - d.lastLit > kPersistCycles) return 0;
  return d.seg;
}

bool SevenSeg::dp(uint64_t nowCycles, uint32_t digit) const {
  if (digit >= kDigits) return false;
  const Digit& d = digits_[digit];
  if (d.lastLit == kNeverLit || nowCycles - d.lastLit > kPersistCycles) return false;
  return d.dp;
}

uint64_t SevenSeg::lastLit(uint32_t digit) const {
  return digit < kDigits ? digits_[digit].lastLit : kNeverLit;
}

}  // namespace vb
