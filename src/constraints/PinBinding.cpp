#include "constraints/PinBinding.h"

#include "constraints/Basys3Board.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace vb {

PinBinding PinBinding::bind(const XdcDoc& doc, SimEngine& engine, const Options& opts) {
  PinBinding b;
  std::set<std::string> constrainedPorts;
  const auto ports = engine.ports();
  // With the shipped full-board default, constraints that don't land on this
  // design (missing port, wider bus than the design declares) are the
  // expected case, not a user mistake.
  const char* skipPrefix = opts.builtinDefault ? "note: " : "warning: ";

  const auto describe = [&engine](const BoundSignal& bs) {
    const SignalInfo si = engine.info(bs.id);
    if (!si.packedRange) return si.name;
    const auto [left, right] = *si.packedRange;
    const int64_t index = left >= right ? int64_t{right} + bs.bit : int64_t{right} - bs.bit;
    return si.name + "[" + std::to_string(index) + "]";
  };

  for (const PinConstraint& pc : doc.pins) {
    constrainedPorts.insert(pc.port.port);
    if (!pc.packagePin) {
      b.diags_.push_back("warning: port '" + pc.port.toString() +
                         "' has constraints but no PACKAGE_PIN");
      continue;
    }
    const char* resource = basys3::resourceForPin(*pc.packagePin);
    if (!resource) {
      b.diags_.push_back("warning: '" + *pc.packagePin +
                         "' is not a user-accessible Basys 3 pin (port '" +
                         pc.port.toString() + "')");
      continue;
    }
    // lookup() also resolves debugger-only hierarchical internal signals;
    // get_ports must never bind those as physical top-level ports.
    const auto port = std::find_if(ports.begin(), ports.end(), [&](const SignalInfo& p) {
      return p.name == pc.port.port;
    });
    if (port == ports.end()) {
      b.diags_.push_back(skipPrefix + ("constraint on port '" + pc.port.port +
                                       "' which the design does not have — skipped"));
      continue;
    }
    const SignalId id = engine.lookup(pc.port.port);
    const SignalInfo& si = *port;
    if (!pc.port.bit && si.packedRange) {
      // Vivado rejects an unindexed PACKAGE_PIN on a bus; binding bit 0
      // behind the user's back would leave the other bits silently dark.
      b.diags_.push_back("warning: whole-bus constraint on " +
                         std::to_string(si.width) + "-bit port '" + pc.port.port +
                         "' (missing [bit] index?) — skipped");
      continue;
    }
    if (pc.port.bit && !si.packedRange) {
      b.diags_.push_back(skipPrefix + ("indexed constraint on scalar port '" +
                                      pc.port.port + "' — skipped"));
      continue;
    }
    // Numeric HDL indices are not storage offsets: [7:4] index 4 is bit 0,
    // [0:3] index 0 is bit 3, and signed indices are legal Verilog too.
    int64_t offset = 0;
    if (pc.port.bit && si.packedRange) {
      const auto [left, right] = *si.packedRange;
      const int64_t index = *pc.port.bit;
      offset = left >= right ? index - right : int64_t{right} - index;
    }
    if (offset < 0 || offset >= si.width) {
      b.diags_.push_back(skipPrefix +
                         ("'" + pc.port.toString() + "' indexes outside the " +
                          std::to_string(si.width) + "-bit port — skipped"));
      continue;
    }
    const uint32_t bit = static_cast<uint32_t>(offset);
    if (basys3::resourceDrivesDesign(resource) && !si.input) {
      b.diags_.push_back("warning: board resource " + std::string(resource) +
                         " drives the design but port '" + pc.port.port +
                         "' is not an input — skipped");
      continue;
    }
    const BoundSignal bound{id, bit, si.width, si.input};
    if (const BoundSignal* prev = b.find(resource)) {
      // Duplicated pins are a classic hand-edit error; Vivado rejects them
      // at placement. Last write wins, loudly.
      b.diags_.push_back("warning: pin " + *pc.packagePin + " (" + resource +
                         ") bound to both '" + describe(*prev) + "' and '" +
                         pc.port.toString() + "' — using '" + pc.port.toString() +
                         "'");
    }
    b.map_[resource] = bound;
  }

  // Reverse diagnostic: a design port nothing constrains is usually a typo'd
  // XDC (the design goes dark with no other symptom).
  for (const SignalInfo& p : ports) {
    if (!constrainedPorts.count(p.name))
      b.diags_.push_back("warning: design port '" + p.name +
                         "' has no pin constraint in this XDC");
  }
  return b;
}

const BoundSignal* PinBinding::find(std::string_view resource) const {
  const auto it = map_.find(resource);
  return it == map_.end() ? nullptr : &it->second;
}

void PinBinding::setPin(SimEngine& engine, std::string_view resource, bool level) const {
  const BoundSignal* bs = find(resource);
  if (!bs)
    throw std::invalid_argument("setPin: resource '" + std::string(resource) +
                                "' is not bound");
  const uint64_t cur = engine.peek(bs->id);
  const uint64_t mask = 1ull << bs->bit;
  engine.poke(bs->id, level ? (cur | mask) : (cur & ~mask));
}

bool PinBinding::getPin(SimEngine& engine, std::string_view resource) const {
  const BoundSignal* bs = find(resource);
  if (!bs)
    throw std::invalid_argument("getPin: resource '" + std::string(resource) +
                                "' is not bound");
  return (engine.peek(bs->id) >> bs->bit) & 1;
}

}  // namespace vb
