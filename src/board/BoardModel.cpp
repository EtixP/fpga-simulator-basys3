#include "board/BoardModel.h"

#include <string>

namespace vb {

BoardModel::BoardModel(SimEngine& engine, PinBinding binding)
    : engine_(engine), binding_(std::move(binding)) {
  // Resolve the per-resource bindings once; find() pointers stay valid for
  // the life of binding_ (owned by this object, never mutated after this).
  for (uint32_t i = 0; i < kSwitchCount; ++i)
    switches_[i] = binding_.find("SW" + std::to_string(i));
  for (uint32_t i = 0; i < kLedCount; ++i)
    leds_[i] = binding_.find("LED" + std::to_string(i));
}

void BoardModel::tick(uint64_t cycles) { engine_.step(cycles); }

bool BoardModel::hasSwitch(uint32_t i) const {
  return i < kSwitchCount && switches_[i] != nullptr;
}

bool BoardModel::switchState(uint32_t i) const {
  return i < kSwitchCount && switchState_[i];
}

void BoardModel::setSwitch(uint32_t i, bool on) {
  if (!hasSwitch(i)) return;
  switchState_[i] = on;
  binding_.setPin(engine_, "SW" + std::to_string(i), on);
}

bool BoardModel::hasLed(uint32_t i) const {
  return i < kLedCount && leds_[i] != nullptr;
}

bool BoardModel::ledState(uint32_t i) const {
  if (!hasLed(i)) return false;
  return binding_.getPin(engine_, "LED" + std::to_string(i));
}

}  // namespace vb
