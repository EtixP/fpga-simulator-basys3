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
  setupVga();
}

void BoardModel::setupVga() {
  // Canonical resource order; the packed word lays out distinct SignalIds in
  // first-seen order, each masked to its port width. Binding is pin-keyed, so
  // a color channel's four bits may live in one bus, four scalars, or bits of
  // a shared port — we locate each bit by (its port's packed offset + its
  // BoundSignal.bit), never assuming a port shape.
  struct Res { const char* name; uint32_t* lane; };
  VgaFrameAssembler::Lanes lanes;
  const Res order[] = {
      {"VGA_R0", &lanes.red[0]},   {"VGA_R1", &lanes.red[1]},
      {"VGA_R2", &lanes.red[2]},   {"VGA_R3", &lanes.red[3]},
      {"VGA_G0", &lanes.green[0]}, {"VGA_G1", &lanes.green[1]},
      {"VGA_G2", &lanes.green[2]}, {"VGA_G3", &lanes.green[3]},
      {"VGA_B0", &lanes.blue[0]},  {"VGA_B1", &lanes.blue[1]},
      {"VGA_B2", &lanes.blue[2]},  {"VGA_B3", &lanes.blue[3]},
      {"VGA_HS", &lanes.hsync},    {"VGA_VS", &lanes.vsync},
  };
  std::vector<SignalId> ids;
  std::vector<uint32_t> offsets;  // packed offset per distinct id (parallel to ids)
  uint32_t total = 0;
  for (const Res& r : order) {
    const BoundSignal* bs = binding_.find(r.name);
    if (!bs) return;  // any VGA pin missing -> no VGA on this board
    // Locate (or add) this port's distinct id and its packed base offset.
    uint32_t base = 0;
    bool found = false;
    for (size_t k = 0; k < ids.size(); ++k)
      if (ids[k] == bs->id) { base = offsets[k]; found = true; break; }
    if (!found) {
      if (total + bs->portWidth > 64) return;  // width sum busts the pack word
      base = total;
      ids.push_back(bs->id);
      offsets.push_back(total);
      total += bs->portWidth;
    }
    *r.lane = base + bs->bit;
  }
  vgaIds_ = std::move(ids);
  vgaBuf_.assign(kSampleChunkCycles, 0);
  vga_ = std::make_unique<VgaFrameAssembler>(lanes);
}

uint64_t BoardModel::vgaCompletedFrames() const {
  return vga_ ? vga_->completedFrames() : 0;
}
const std::vector<uint8_t>& BoardModel::vgaFramebuffer() const {
  static const std::vector<uint8_t> empty;
  return vga_ ? vga_->framebuffer() : empty;
}
uint64_t BoardModel::vgaLastFrameCycle() const {
  return vga_ ? vga_->lastFrameCycle() : 0;
}
uint32_t BoardModel::vgaCyclesPerPixel() const {
  return vga_ ? vga_->measuredCyclesPerPixel() : 0;
}
bool BoardModel::vgaOk() const { return vga_ ? vga_->ok() : true; }
const std::string& BoardModel::vgaStatus() const {
  static const std::string none;
  return vga_ ? vga_->status() : none;
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
    const uint64_t segStart = engine_.now();
    const uint64_t seg = target - segStart;
    if (vga_) {
      // Pixel-rate tap: capture the sync/color pins every cycle of this
      // (<= 1000-cycle) segment and feed the monitor model. Same time
      // advance as step() — grid/UART observers below are unaffected.
      engine_.stepCapture(seg, vgaIds_, vgaBuf_.data());
      vga_->consume(segStart, vgaBuf_.data(), static_cast<uint32_t>(seg));
    } else {
      engine_.step(seg);
    }
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
