#include "engine/SimEngine.h"

#include <stdexcept>

namespace vb {

uint64_t SimEngine::packSample(const std::vector<SignalId>& ids) {
  uint64_t word = 0;
  uint32_t offset = 0;
  for (const SignalId id : ids) {
    const uint32_t width = info(id).width;  // throws on invalid id
    if (offset + width > 64)
      throw std::invalid_argument(
          "stepCapture: packed width of the watch set exceeds 64 bits");
    const uint64_t mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
    word |= (peek(id) & mask) << offset;
    offset += width;
  }
  return word;
}

void SimEngine::stepCapture(uint64_t cycles, const std::vector<SignalId>& ids,
                            uint64_t* out) {
  // Validate the watch set once up front (empty run still rejects a bad set),
  // so an override and this default agree on when they throw.
  if (!ids.empty()) {
    uint32_t offset = 0;
    for (const SignalId id : ids) offset += info(id).width;
    if (offset > 64)
      throw std::invalid_argument(
          "stepCapture: packed width of the watch set exceeds 64 bits");
  }
  for (uint64_t i = 0; i < cycles; ++i) {
    step(1);
    out[i] = packSample(ids);
  }
}

}  // namespace vb
