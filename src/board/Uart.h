#pragma once
#include <cstdint>
#include <deque>
#include <optional>

namespace vb {

// 100 MHz / 9600 baud, rounded — the divisor course designs use.
inline constexpr uint64_t kUartCyclesPerBit = 10'417;

// 8N1 decoder for the DESIGN's transmit line (board resource UART_TX),
// sampled on the observation grid — a SevenSeg-style sibling observer.
//
// Grid arithmetic: the start edge is observed up to one chunk late, and each
// bit-center sample lands up to one chunk after its nominal time, so the
// worst sampling offset from a true bit center is < 2 chunks (20 us) —
// comfortably inside a 9600-baud half bit (5208 cycles ≈ 52 us). Faster
// bauds stay safe down to ~4 chunks/bit (~25 kBd) per the same bound.
class UartTxDecoder {
public:
  explicit UartTxDecoder(uint64_t cyclesPerBit = kUartCyclesPerBit)
      : cyclesPerBit_(cyclesPerBit) {}

  struct Result {
    std::optional<uint8_t> byte;  // set when a frame completed at this sample
    bool framingError = false;    // stop bit sampled low
  };
  // Feed one observation-grid sample of the TX line level.
  Result sample(uint64_t now, bool level);

private:
  uint64_t cyclesPerBit_;
  bool receiving_ = false;
  uint64_t startCycle_ = 0;  // grid cycle where the start bit was first seen
  uint32_t bitIndex_ = 0;    // next data/stop bit to sample (0..8; 8 = stop)
  uint8_t shift_ = 0;
};

// Exact-cycle 8N1 driver for the DESIGN's receive line (board resource
// UART_RX). BoardModel::tick splits stepping at nextEdgeCycle() so every bit
// edge is poked at its precise cycle — RX timing is INPUT, so it is
// exact-cycle like all input events, not grid-quantized.
class UartRxDriver {
public:
  explicit UartRxDriver(uint64_t cyclesPerBit = kUartCyclesPerBit)
      : cyclesPerBit_(cyclesPerBit) {}

  // Queue a byte. The start bit lands at `now` or at the end of the previous
  // frame's stop bit, whichever is later — the line is owned through the
  // stop bit's FULL duration (frameStart + 10*bit), so a send issued during
  // the stop tail defers rather than truncating the frame in flight (an 8N1
  // violation no real host UART can produce). Queued bytes chain
  // back-to-back.
  void send(uint8_t byte, uint64_t now);

  // Next cycle at which the line level must be poked; UINT64_MAX when idle.
  static constexpr uint64_t kIdle = ~0ull;
  uint64_t nextEdgeCycle() const;

  struct Edge {
    bool level = true;
    bool byteStart = false;  // this edge is a start bit -> log the byte event
    uint8_t byte = 0;
  };
  // Consume the edge at `now` (caller guarantees now == nextEdgeCycle()).
  Edge advance(uint64_t now);

  bool idle() const { return queue_.empty() && !shifting_; }

private:
  uint64_t cyclesPerBit_;
  std::deque<uint8_t> queue_;
  bool shifting_ = false;
  uint64_t frameStart_ = 0;
  uint64_t lineFreeCycle_ = 0;  // previous frame's stop bit ends here
  uint32_t edgeIndex_ = 0;      // 0=start, 1..8=data LSB-first, 9=stop
};

}  // namespace vb
