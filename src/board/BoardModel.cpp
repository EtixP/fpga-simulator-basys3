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
  for (uint32_t i = 0; i < kDigitCount; ++i) prevDigitChar_[i] = ' ';
}

bool BoardModel::readPin(const BoundSignal* bs) const {
  return bs && ((engine_.peek(bs->id) >> bs->bit) & 1);
}

void BoardModel::tick(uint64_t cycles) {
  const uint64_t end = engine_.now() + cycles;
  while (engine_.now() < end) {
    const uint64_t nextGrid =
        (engine_.now() / kSampleChunkCycles + 1) * kSampleChunkCycles;
    const uint64_t target = std::min(nextGrid, end);
    engine_.step(target - engine_.now());
    if (engine_.now() == nextGrid) sampleAtGridCrossing();
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
