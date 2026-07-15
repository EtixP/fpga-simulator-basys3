#include "board/Uart.h"

#include <algorithm>

namespace vb {

UartTxDecoder::Result UartTxDecoder::sample(uint64_t now, bool level) {
  Result r;
  if (!receiving_) {
    if (!level) {  // start bit
      receiving_ = true;
      startCycle_ = now;
      bitIndex_ = 0;
      shift_ = 0;
    }
    return r;
  }
  // Sample data bit n at start + (1.5 + n) * bit, stop at start + 9.5 * bit —
  // i.e. the first grid crossing at/after each nominal center.
  const uint64_t target =
      startCycle_ + cyclesPerBit_ + cyclesPerBit_ / 2 + bitIndex_ * cyclesPerBit_;
  if (now < target) return r;
  if (bitIndex_ < 8) {
    shift_ = static_cast<uint8_t>(shift_ | (level ? 1u << bitIndex_ : 0u));
    ++bitIndex_;
    return r;
  }
  // Stop bit.
  receiving_ = false;
  if (level) r.byte = shift_;
  else r.framingError = true;
  return r;
}

void UartRxDriver::send(uint8_t byte, uint64_t now) {
  queue_.push_back(byte);
  if (!shifting_) {
    shifting_ = true;
    // The previous frame owns the line through its stop bit's full duration.
    frameStart_ = std::max(now, lineFreeCycle_);
    edgeIndex_ = 0;
  }
}

uint64_t UartRxDriver::nextEdgeCycle() const {
  if (!shifting_) return kIdle;
  return frameStart_ + edgeIndex_ * cyclesPerBit_;
}

UartRxDriver::Edge UartRxDriver::advance(uint64_t /*now*/) {
  Edge e;
  const uint8_t byte = queue_.front();
  if (edgeIndex_ == 0) {
    e.level = false;
    e.byteStart = true;
    e.byte = byte;
  } else if (edgeIndex_ <= 8) {
    e.level = (byte >> (edgeIndex_ - 1)) & 1;
  } else {  // stop bit
    e.level = true;
  }
  if (++edgeIndex_ > 9) {
    // Frame complete after the stop bit's full duration; chain the next byte
    // back-to-back at frameStart + 10 * bit.
    queue_.pop_front();
    lineFreeCycle_ = frameStart_ + 10 * cyclesPerBit_;
    if (!queue_.empty()) {
      frameStart_ = lineFreeCycle_;
      edgeIndex_ = 0;
    } else {
      shifting_ = false;
    }
  }
  return e;
}

}  // namespace vb
