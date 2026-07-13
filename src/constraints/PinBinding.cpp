#include "constraints/PinBinding.h"

#include "constraints/Basys3Board.h"

#include <set>
#include <stdexcept>

namespace vb {

PinBinding PinBinding::bind(const XdcDoc& doc, SimEngine& engine, const Options& opts) {
  PinBinding b;
  std::set<std::string> constrainedPorts;
  // With the shipped full-board default, constraints that don't land on this
  // design (missing port, wider bus than the design declares) are the
  // expected case, not a user mistake.
  const char* skipPrefix = opts.builtinDefault ? "note: " : "warning: ";

  const auto describe = [&engine](const BoundSignal& bs) {
    const SignalInfo si = engine.info(bs.id);
    return bs.portWidth > 1 ? si.name + "[" + std::to_string(bs.bit) + "]" : si.name;
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
    const SignalId id = engine.lookup(pc.port.port);
    if (id == kNoSignal) {
      b.diags_.push_back(skipPrefix + ("constraint on port '" + pc.port.port +
                                       "' which the design does not have — skipped"));
      continue;
    }
    const SignalInfo si = engine.info(id);
    if (!pc.port.bit && si.width > 1) {
      // Vivado rejects an unindexed PACKAGE_PIN on a bus; binding bit 0
      // behind the user's back would leave the other bits silently dark.
      b.diags_.push_back("warning: whole-bus constraint on " +
                         std::to_string(si.width) + "-bit port '" + pc.port.port +
                         "' (missing [bit] index?) — skipped");
      continue;
    }
    const uint32_t bit = pc.port.bit.value_or(0);
    if (bit >= si.width) {
      b.diags_.push_back(skipPrefix +
                         ("'" + pc.port.toString() + "' indexes past the " +
                          std::to_string(si.width) + "-bit port — skipped"));
      continue;
    }
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
  for (const SignalInfo& p : engine.ports()) {
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
