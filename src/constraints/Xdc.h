#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vb {

// One get_ports target: "clk" -> {clk, nullopt}; "{sw[3]}" -> {sw, 3}.
struct PortRef {
  std::string port;
  std::optional<int32_t> bit;

  bool operator==(const PortRef&) const = default;
  std::string toString() const;
  // Rejects ranges/wildcards ("led[15:0]", "led[*]") — callers warn-skip.
  static std::optional<PortRef> parse(std::string_view text);
};

// Accumulated set_property constraints for one port (both the -dict form and
// the classic one-property-per-line form merge here, last-write-wins per key).
struct PinConstraint {
  PortRef port;
  std::optional<std::string> packagePin;   // PACKAGE_PIN
  std::optional<std::string> iostandard;   // IOSTANDARD
  // Anything else, verbatim (e.g. {"PULLUP", "true"}), preserved on write.
  std::vector<std::pair<std::string, std::string>> extraProps;

  bool operator==(const PinConstraint&) const = default;
};

// create_clock: stored for the phase-3 pseudo-STA; unused by simulation.
struct ClockConstraint {
  std::optional<std::string> name;  // -name (Vivado may omit it)
  double periodNs = 0.0;            // -period
  std::optional<std::pair<double, double>> waveformNs;  // -waveform {rise fall}
  bool add = false;                 // -add
  PortRef port;

  bool operator==(const ClockConstraint&) const = default;
};

struct XdcDoc {
  std::vector<PinConstraint> pins;     // in first-appearance order
  std::vector<ClockConstraint> clocks;
  // set_property <K> <V> [current_design], verbatim (CONFIG_VOLTAGE, CFGBVS,
  // BITSTREAM.* ...) — stored and re-emitted so round-trips are lossless.
  std::vector<std::pair<std::string, std::string>> designProps;
  // R2: unknown/unsupported commands warn and skip, never crash. Warnings are
  // diagnostics, NOT document content — excluded from semanticallyEqual().
  std::vector<std::string> warnings;
};

// Literal Tcl subset: braces/quotes/escapes, command separators and line
// continuations. Only get_ports/current_design command targets are resolved;
// variables, arbitrary command substitutions and scripts warn and skip.
XdcDoc parseXdc(std::string_view text);

// Canonical serialization: parse(writeXdc(doc)) is semantically equal to doc,
// and writeXdc is idempotent byte-for-byte across that round-trip.
std::string writeXdc(const XdcDoc& doc);

bool semanticallyEqual(const XdcDoc& a, const XdcDoc& b);

}  // namespace vb
