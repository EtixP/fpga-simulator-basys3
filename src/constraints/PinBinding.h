#pragma once
#include "constraints/Xdc.h"
#include "engine/SimEngine.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vb {

// Where a board resource landed in the design: XDC pins are per-bit, engine
// signals are whole ports, so a resource addresses one bit of one port.
struct BoundSignal {
  SignalId id{kNoSignal};
  uint32_t bit = 0;
  uint32_t portWidth = 0;
  bool input = false;
};

// R2 port binding: joins an XdcDoc (port -> pin) with the static Basys 3
// table (pin -> resource) and the engine's ports, yielding
// resource -> {SignalId, bit}. Binding is keyed on PACKAGE PINS, never on
// port names — a design may call its switch input anything.
class PinBinding {
public:
  struct Options {
    // The shipped full-board default constrains every board resource, so
    // constraints on ports the design doesn't have are expected: note-level.
    // In a user-supplied XDC the same situation is probably a typo: warning.
    bool builtinDefault = false;
  };

  static PinBinding bind(const XdcDoc& doc, SimEngine& engine, const Options& opts);
  static PinBinding bind(const XdcDoc& doc, SimEngine& engine) {
    return bind(doc, engine, Options{});
  }

  const BoundSignal* find(std::string_view resource) const;

  // Drive/read one bit of the bound port. setPin is read-modify-write via
  // peek/poke (peek of an input returns the last poked value). Throws
  // std::invalid_argument for unbound resources; poking CLK100 or an output
  // resource throws from the engine's poke contract.
  void setPin(SimEngine& engine, std::string_view resource, bool level) const;
  bool getPin(SimEngine& engine, std::string_view resource) const;

  // "note: ..." entries are expected skips (builtin default on a small
  // design); "warning: ..." entries deserve user attention (typo'd port,
  // unconstrained design port, non-input switch binding, unknown pin).
  const std::vector<std::string>& diagnostics() const { return diags_; }
  size_t size() const { return map_.size(); }

private:
  std::map<std::string, BoundSignal, std::less<>> map_;
  std::vector<std::string> diags_;
};

}  // namespace vb
