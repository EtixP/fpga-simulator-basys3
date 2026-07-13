#pragma once
#include "constraints/PinBinding.h"
#include "engine/SimEngine.h"

#include <array>
#include <cstdint>

namespace vb {

// The virtual board the GUI renders: owns the pin binding and hides the
// engine behind board-level state (switch positions, LED levels). This is a
// second, softer wall behind the SimEngine rule — draw code sees only
// BoardModel, so later peripherals (seven-seg, UART, VGA) slot in here as
// siblings and the frontend stays disposable.
//
// Milestone 1.3 scope: 16 slide switches, 16 LEDs.
class BoardModel {
public:
  static constexpr uint32_t kSwitchCount = 16;
  static constexpr uint32_t kLedCount = 16;

  // The engine must outlive the model. Resources the XDC doesn't bind for
  // this design (e.g. SW4..SW15 when the design's sw bus is 4 bits wide)
  // simply don't exist on the virtual board: hasSwitch/hasLed return false,
  // setSwitch is ignored, ledState reads 0.
  BoardModel(SimEngine& engine, PinBinding binding);
  // Non-copyable: switches_/leds_ cache pointers into binding_'s map nodes;
  // a copy would deep-copy the map but keep pointers into the source's.
  BoardModel(const BoardModel&) = delete;
  BoardModel& operator=(const BoardModel&) = delete;

  // Advance virtual time. The GUI calls this once per frame with a fixed
  // cycle count — never with "cycles until N milliseconds elapsed" (R1: wall
  // clock must not influence virtual time; honest pacing is phase 2).
  void tick(uint64_t cycles);

  bool hasSwitch(uint32_t i) const;
  bool switchState(uint32_t i) const;   // last position set (false if unbound)
  void setSwitch(uint32_t i, bool on);  // takes effect at the next eval

  bool hasLed(uint32_t i) const;
  bool ledState(uint32_t i) const;  // settles comb logic via peek if needed

  uint64_t now() const { return engine_.now(); }
  const PinBinding& binding() const { return binding_; }

private:
  SimEngine& engine_;
  PinBinding binding_;
  std::array<const BoundSignal*, kSwitchCount> switches_{};
  std::array<const BoundSignal*, kLedCount> leds_{};
  std::array<bool, kSwitchCount> switchState_{};
};

}  // namespace vb
