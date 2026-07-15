#include "board/BoardModel.h"

#include <algorithm>
#include <string>

namespace vb {

BoardModel::BoardModel(SimEngine& engine, PinBinding binding)
    : engine_(engine), binding_(std::move(binding)) {
  // Resolve per-resource bindings once; find() pointers stay valid for the
  // life of binding_ (owned by this object, never mutated after this).
  for (uint32_t i = 0; i < kSwitchCount; ++i)
    switches_[i] = binding_.find("SW" + std::to_string(i));
  for (uint32_t i = 0; i < kLedCount; ++i)
    leds_[i] = binding_.find("LED" + std::to_string(i));
  for (uint32_t i = 0; i < 5; ++i) buttons_[i] = binding_.find(kButtonNames[i]);
  for (uint32_t i = 0; i < 7; ++i)
    segPins_[i] = binding_.find("SEG" + std::to_string(i));
  for (uint32_t i = 0; i < 4; ++i) anPins_[i] = binding_.find("AN" + std::to_string(i));
  dpPin_ = binding_.find("DP");
  displayBound_ = std::any_of(anPins_.begin(), anPins_.end(),
                              [](const BoundSignal* p) { return p != nullptr; });
  uartTxPin_ = binding_.find("UART_TX");
  uartRxPin_ = binding_.find("UART_RX");
  // A real serial line idles HIGH (host-side pull-up); the engine's 2-state
  // zero-init would otherwise present a spurious start bit at power-on.
  // (Designs should still btnC-reset their synchronizers per R6 — the
  // engine settles t=0 with all inputs low before this poke lands.)
  if (uartRxPin_) binding_.setPin(engine_, "UART_RX", true);
  for (uint32_t i = 0; i < kDigitCount; ++i) prevDigitChar_[i] = ' ';
}

namespace {
std::string byteRepr(uint8_t b) {
  char buf[16];
  const char c = (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
  std::snprintf(buf, sizeof buf, "0x%02X '%c'", b, c);
  return buf;
}
}  // namespace

void BoardModel::sendUart(uint8_t byte) {
  if (!hasUartRx()) return;
  uartRx_.send(byte, engine_.now());
}

void BoardModel::sendUartText(std::string_view text) {
  for (const char c : text) sendUart(static_cast<uint8_t>(c));
}

bool BoardModel::readPin(const BoundSignal* bs) const {
  return bs && ((engine_.peek(bs->id) >> bs->bit) & 1);
}

void BoardModel::tick(uint64_t cycles) {
  const uint64_t end = engine_.now() + cycles;
  // Apply any UART RX edge scheduled for the current cycle before stepping
  // (e.g. a sendUart() issued between ticks starts its start bit here).
  applyUartRxEdges();
  while (engine_.now() < end) {
    const uint64_t nextGrid =
        (engine_.now() / kSampleChunkCycles + 1) * kSampleChunkCycles;
    const uint64_t target =
        std::min({nextGrid, end, uartRx_.nextEdgeCycle()});
    engine_.step(target - engine_.now());
    // Same-cycle order at a grid crossing: outputs first (sampled state),
    // then input edges — matching the log's documented same-stamp tiebreak.
    if (engine_.now() == nextGrid) sampleAtGridCrossing();
    applyUartRxEdges();
  }
}

void BoardModel::applyUartRxEdges() {
  // <= (not ==): if a caller advances the engine directly (bypassing tick, a
  // misuse the engine's exposure permits), pending edges become overdue —
  // apply them late-but-deterministically instead of letting the tick loop's
  // min() target fall below now() and underflow into an infinite step().
  while (uartRx_.nextEdgeCycle() <= engine_.now()) {
    const UartRxDriver::Edge e = uartRx_.advance(engine_.now());
    binding_.setPin(engine_, "UART_RX", e.level);
    if (e.byteStart)
      log_.event(engine_.now(), "UART RX " + byteRepr(e.byte));
  }
}

void BoardModel::sampleAtGridCrossing() {
  const uint64_t now = engine_.now();

  // Canonical emission order (frozen — see SimLog header): SSEG digits 0..3
  // with the content line before the .dp line, then LEDs ascending.
  if (displayBound_) {
    uint8_t anBits = 0, segBits = 0;
    for (uint32_t i = 0; i < 4; ++i)
      anBits |= static_cast<uint8_t>((anPins_[i] ? readPin(anPins_[i]) : true) << i);
    for (uint32_t i = 0; i < 7; ++i)
      segBits |= static_cast<uint8_t>((segPins_[i] ? readPin(segPins_[i]) : true) << i);
    sevenSeg_.sample(now, anBits, segBits, readPin(dpPin_) || dpPin_ == nullptr);

    for (uint32_t i = 0; i < kDigitCount; ++i) {
      const char c = decodeSevenSeg(sevenSeg_.segments(now, i));
      if (c != prevDigitChar_[i]) {
        log_.event(now, "SSEG[" + std::to_string(i) + "] '" +
                            std::string(1, prevDigitChar_[i]) + "'->'" +
                            std::string(1, c) + "'");
        prevDigitChar_[i] = c;
      }
      const bool dp = sevenSeg_.dp(now, i);
      if (dp != prevDigitDp_[i]) {
        log_.event(now, "SSEG[" + std::to_string(i) + "].dp " +
                            (prevDigitDp_[i] ? "1" : "0") + "->" + (dp ? "1" : "0"));
        prevDigitDp_[i] = dp;
      }
    }
  }

  for (uint32_t i = 0; i < kLedCount; ++i) {
    if (!leds_[i]) continue;
    const bool on = readPin(leds_[i]);
    if (on != prevLed_[i]) {
      log_.event(now, "LED[" + std::to_string(i) + "] " + (prevLed_[i] ? "1" : "0") +
                          "->" + (on ? "1" : "0"));
      prevLed_[i] = on;
    }
  }

  if (uartTxPin_) {
    const UartTxDecoder::Result r = uartTx_.sample(now, readPin(uartTxPin_));
    if (r.byte) {
      uartTxBytes_.push_back(*r.byte);
      log_.event(now, "UART TX " + byteRepr(*r.byte));
    }
    if (r.framingError) log_.event(now, "UART TX framing error");
  }
}

bool BoardModel::hasSwitch(uint32_t i) const {
  return i < kSwitchCount && switches_[i] != nullptr;
}

bool BoardModel::switchState(uint32_t i) const {
  return i < kSwitchCount && switchState_[i];
}

void BoardModel::setSwitch(uint32_t i, bool on) {
  if (!hasSwitch(i) || switchState_[i] == on) return;
  switchState_[i] = on;
  binding_.setPin(engine_, "SW" + std::to_string(i), on);
  log_.event(engine_.now(),
             "SW" + std::to_string(i) + (on ? " 0->1" : " 1->0"));
}

bool BoardModel::hasButton(Button b) const {
  return buttons_[static_cast<uint32_t>(b)] != nullptr;
}

bool BoardModel::buttonState(Button b) const {
  return buttonState_[static_cast<uint32_t>(b)];
}

void BoardModel::setButton(Button b, bool pressed) {
  const uint32_t i = static_cast<uint32_t>(b);
  if (!hasButton(b) || buttonState_[i] == pressed) return;
  buttonState_[i] = pressed;
  binding_.setPin(engine_, kButtonNames[i], pressed);
  log_.event(engine_.now(),
             std::string(kButtonNames[i]) + (pressed ? " 0->1" : " 1->0"));
}

bool BoardModel::hasLed(uint32_t i) const {
  return i < kLedCount && leds_[i] != nullptr;
}

bool BoardModel::ledState(uint32_t i) const {
  return i < kLedCount && readPin(leds_[i]);
}

uint8_t BoardModel::digitSegments(uint32_t i) const {
  return sevenSeg_.segments(engine_.now(), i);
}

bool BoardModel::digitDp(uint32_t i) const { return sevenSeg_.dp(engine_.now(), i); }

char BoardModel::digitChar(uint32_t i) const {
  return decodeSevenSeg(digitSegments(i));
}

uint64_t BoardModel::digitLastLit(uint32_t i) const { return sevenSeg_.lastLit(i); }

}  // namespace vb
