// Milestone 1.2 acceptance: the XDC parser round-trips Basys3_Master.xdc —
// plus losslessness assertions that keep the round-trip honest (a parser that
// silently drops lines would otherwise round-trip perfectly).
//
// argv[1] = shipped default src/constraints/data/Basys3_Master.xdc (uncommented)
// argv[2] = pristine Digilent Basys-3-Master.xdc (all pin lines commented)
#include "check.h"
#include "constraints/Xdc.h"

#include <fstream>
#include <sstream>
#include <string>

using vb::ClockConstraint;
using vb::parseXdc;
using vb::PinConstraint;
using vb::PortRef;
using vb::semanticallyEqual;
using vb::writeXdc;
using vb::XdcDoc;

static std::string readFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static const PinConstraint* findPin(const XdcDoc& doc, const std::string& port,
                                    std::optional<uint32_t> bit) {
  for (const auto& p : doc.pins)
    if (p.port == PortRef{port, bit}) return &p;
  return nullptr;
}

static void checkShippedDefault(const std::string& text) {
  const XdcDoc doc = parseXdc(text);

  // Losslessness: exact counts from the real Digilent file. A silent drop of
  // any line class (Pmod ";#" comments, PULLUP dict keys, current_design)
  // changes one of these.
  CHECK_EQ(doc.warnings.size(), 0);
  CHECK_EQ(doc.pins.size(), 105);
  CHECK_EQ(doc.clocks.size(), 1);
  CHECK_EQ(doc.designProps.size(), 5);

  // Spot checks across every syntactic variant in the file.
  const PinConstraint* sw0 = findPin(doc, "sw", 0);
  CHECK(sw0 && sw0->packagePin && *sw0->packagePin == "V17");
  const PinConstraint* led0 = findPin(doc, "led", 0);
  CHECK(led0 && led0->packagePin && *led0->packagePin == "U16");
  const PinConstraint* clk = findPin(doc, "clk", std::nullopt);  // unbraced get_ports
  CHECK(clk && clk->packagePin && *clk->packagePin == "W5");
  CHECK(clk->iostandard && *clk->iostandard == "LVCMOS33");
  const PinConstraint* ja0 = findPin(doc, "JA", 0);  // trailing ";#Sch name = JA1"
  CHECK(ja0 && ja0->packagePin && *ja0->packagePin == "J1");
  const PinConstraint* ps2 = findPin(doc, "PS2Clk", std::nullopt);  // extra dict key
  CHECK(ps2 != nullptr);
  CHECK_EQ(ps2->extraProps.size(), 1);
  CHECK(ps2->extraProps[0].first == "PULLUP" && ps2->extraProps[0].second == "true");

  const ClockConstraint& cc = doc.clocks[0];
  CHECK(cc.add);
  CHECK(cc.name && *cc.name == "sys_clk_pin");
  CHECK_EQ(cc.periodNs, 10.0);
  CHECK(cc.waveformNs && cc.waveformNs->first == 0.0 && cc.waveformNs->second == 5.0);
  CHECK(cc.port == (PortRef{"clk", std::nullopt}));

  bool sawCfgbvs = false;
  for (const auto& [k, v] : doc.designProps)
    if (k == "CFGBVS") { sawCfgbvs = true; CHECK(v == "VCCO"); }
  CHECK(sawCfgbvs);

  // The acceptance round-trip: parse -> serialize -> reparse -> equal, and the
  // serializer is idempotent (canonical form), so future golden diffs can rely
  // on it byte-for-byte.
  const std::string once = writeXdc(doc);
  const XdcDoc reparsed = parseXdc(once);
  CHECK_EQ(reparsed.warnings.size(), 0);
  CHECK(semanticallyEqual(doc, reparsed));
  CHECK(writeXdc(reparsed) == once);
}

static void checkPristine(const std::string& text) {
  // The upstream artifact exactly as Digilent ships it: every pin line is a
  // comment, only the five [current_design] properties are live.
  const XdcDoc doc = parseXdc(text);
  CHECK_EQ(doc.warnings.size(), 0);
  CHECK_EQ(doc.pins.size(), 0);
  CHECK_EQ(doc.clocks.size(), 0);
  CHECK_EQ(doc.designProps.size(), 5);
}

static void checkGrammarVariants() {
  // Two-line classic style merges into one constraint; last write wins.
  {
    const XdcDoc doc = parseXdc(
        "set_property PACKAGE_PIN V17 [get_ports {sw[0]}]\r\n"
        "\tset_property IOSTANDARD LVCMOS18 [get_ports {sw[0]}]\n"
        "set_property IOSTANDARD LVCMOS33 [get_ports {sw[0]}]\n");
    CHECK_EQ(doc.warnings.size(), 0);
    CHECK_EQ(doc.pins.size(), 1);
    CHECK(doc.pins[0].packagePin && *doc.pins[0].packagePin == "V17");
    CHECK(doc.pins[0].iostandard && *doc.pins[0].iostandard == "LVCMOS33");
  }
  // R2: unknown commands and malformed lines warn and skip — never crash, and
  // never take out the good lines around them.
  {
    const XdcDoc doc = parseXdc(
        "set_property PACKAGE_PIN V17 [get_ports {sw[0]}]\n"
        "set_false_path -from [get_ports clk]\n"                 // unknown command
        "set_property -dict { PACKAGE_PIN } [get_ports clk]\n"   // odd dict tokens
        "set_property PACKAGE_PIN A1 [get_nets {foo}]\n"         // unsupported target
        "set_property PACKAGE_PIN A2 [get_ports {led[15:0]}]\n"  // range: no PortRef
        "set_property PACKAGE_PIN A3 [get_ports {broken\n"       // unbalanced brace
        "set_property PACKAGE_PIN V16 [get_ports {sw[1]}]\n");
    CHECK_EQ(doc.pins.size(), 2);  // the two good lines survive
    CHECK_EQ(doc.warnings.size(), 5);
  }
  // create_clock edge cases: missing -period kills the clock; a >2-edge
  // waveform is dropped but the clock is kept.
  {
    const XdcDoc doc = parseXdc(
        "create_clock -name x [get_ports clk]\n"
        "create_clock -period 20 -waveform {0 5 10} [get_ports clk]\n");
    CHECK_EQ(doc.clocks.size(), 1);
    CHECK_EQ(doc.clocks[0].periodNs, 20.0);
    CHECK(!doc.clocks[0].waveformNs);
    CHECK(!doc.clocks[0].name);
    CHECK_EQ(doc.warnings.size(), 2);
  }
  // Values that only survive serialization braced (spaces, empty), plus a
  // clock period needing full double precision: the round-trip contract must
  // hold for all of them.
  {
    const XdcDoc doc = parseXdc(
        "set_property PACKAGE_PIN C17 [get_ports io]\n"
        "set_property FOO {a b c} [get_ports io]\n"
        "set_property BAR {} [get_ports io]\n"
        "set_property USR_ACCESS {DEAD BEEF} [current_design]\n"
        "create_clock -name {my clk} -period 3.333333333333 [get_ports clk]\n");
    CHECK_EQ(doc.warnings.size(), 0);
    CHECK_EQ(doc.pins.size(), 1);
    CHECK_EQ(doc.pins[0].extraProps.size(), 2);
    CHECK(doc.pins[0].extraProps[0].second == "a b c");
    CHECK(doc.pins[0].extraProps[1].second == "");
    const std::string out = writeXdc(doc);
    const XdcDoc re = parseXdc(out);
    CHECK_EQ(re.warnings.size(), 0);
    CHECK(semanticallyEqual(doc, re));
    CHECK(writeXdc(re) == out);
  }
  // Multi-port get_ports lists (Tcl applies to each port; we don't support
  // that): warn and skip, never store a bogus one-port constraint.
  {
    const XdcDoc doc =
        parseXdc("set_property IOSTANDARD LVCMOS33 [get_ports {btnU btnD}]\n");
    CHECK_EQ(doc.pins.size(), 0);
    CHECK_EQ(doc.warnings.size(), 1);
  }
  // Order-independent create_clock flags (Vivado emits various orders).
  {
    const XdcDoc doc =
        parseXdc("create_clock -period 6.25 -add [get_ports clk] -name fast\n");
    CHECK_EQ(doc.clocks.size(), 1);
    CHECK(doc.clocks[0].add);
    CHECK(doc.clocks[0].name && *doc.clocks[0].name == "fast");
    CHECK_EQ(doc.clocks[0].periodNs, 6.25);
    const std::string out = writeXdc(doc);
    CHECK(semanticallyEqual(doc, parseXdc(out)));
  }
}

static void checkLiteralSyntaxAndValidation() {
  const XdcDoc doc = parseXdc(
      "set_property PACKAGE_PIN \"V17\" [get_ports \"sw\\[0\\]\"]; "
      "set_property IOSTANDARD LVCMOS33 [get_ports {sw[0]}]\n"
      "set_property -dict {\n PACKAGE_PIN V16\n IOSTANDARD LVCMOS33\n} "
      "[get_ports {sw[1]}]\n"
      "set_property PACKAGE_PIN \\\n V15 [get_ports {sw[-2]}]\n"
      "create_clock -name \"system clock\" -period 10 \\\n"
      " -waveform {0 5} [get_ports clk];# trailing comment\n");
  CHECK_EQ(doc.warnings.size(), 0);
  CHECK_EQ(doc.pins.size(), 3);
  CHECK(doc.pins[0].port == (PortRef{"sw", 0}));
  CHECK(doc.pins[0].packagePin == "V17");
  CHECK(doc.pins[0].iostandard == "LVCMOS33");
  CHECK(doc.pins[1].packagePin == "V16");
  CHECK(doc.pins[2].port.toString() == "sw[-2]");
  CHECK_EQ(doc.clocks.size(), 1);
  CHECK(doc.clocks[0].name == "system clock");
  CHECK(semanticallyEqual(doc, parseXdc(writeXdc(doc))));

  // Tcl removes grouping delimiters before command option interpretation.
  const XdcDoc groupedOptions = parseXdc(
      "set_property {-dict} {PACKAGE_PIN V17 IOSTANDARD LVCMOS33} [get_ports sw]\n"
      "create_clock {-add} {-name} clock {-period} 10 {-waveform} {7 2} [get_ports clk]\n");
  CHECK(groupedOptions.warnings.empty());
  CHECK_EQ(groupedOptions.pins.size(), 1);
  CHECK(groupedOptions.pins[0].packagePin == "V17");
  CHECK(groupedOptions.pins[0].iostandard == "LVCMOS33");
  CHECK(groupedOptions.pins[0].extraProps.empty());
  CHECK_EQ(groupedOptions.clocks.size(), 1);
  CHECK(groupedOptions.clocks[0].add);
  CHECK(groupedOptions.clocks[0].name == "clock");
  CHECK(groupedOptions.clocks[0].waveformNs == (std::pair{7.0, 2.0}));

  // Literal values must survive exactly, including significant whitespace,
  // Tcl metacharacters and escaped unmatched braces. No Tcl is evaluated.
  const XdcDoc literals = parseXdc(
      "set_property A {A#B} [current_design]\n"
      "set_property B {A[B]} [current_design]\n"
      "set_property C { A } [current_design]\n"
      "set_property D {$not_a_variable; \\\\ literal} [current_design]\n"
      "set_property E \"a\\{b\\}c\\[d\\]\\$e\\\\f\\\"\" [current_design]\n"
      "set_property F {line one\nline two} [current_design]\n"
      "set_property G a#b [current_design]\n");
  CHECK_EQ(literals.warnings.size(), 0);
  CHECK_EQ(literals.designProps.size(), 7);
  CHECK(literals.designProps[2].second == " A ");
  CHECK(literals.designProps[4].second == "a{b}c[d]$e\\f\"");
  const std::string serialized = writeXdc(literals);
  const XdcDoc re = parseXdc(serialized);
  CHECK_EQ(re.warnings.size(), 0);
  CHECK(semanticallyEqual(literals, re));
  CHECK(writeXdc(re) == serialized);

  const XdcDoc delimiters = parseXdc(
      "set_property OPEN \"a\\{\" [get_ports clk]\n"
      "set_property CLOSE \"b\\}\" [get_ports clk]\n"
      "set_property SLASH \"c\\\\\" [get_ports clk]\n"
      "create_clock -name \"c\\{\\\\\" -period 10 [get_ports clk]\n");
  CHECK(delimiters.warnings.empty());
  CHECK(semanticallyEqual(delimiters, parseXdc(writeXdc(delimiters))));

  // Reject evaluation-dependent forms as a whole, without applying the
  // literal parts and pretending the resulting constraints are complete.
  for (const char* source : {
           "set_property PACKAGE_PIN $pin [get_ports clk]\n",
           "set_property PACKAGE_PIN [list W5] [get_ports clk]\n",
           "set_property PACKAGE_PIN W5 [get_ports $clock]\n",
           "set_property PACKAGE_PIN W5 [get_ports \"$clock\"]\n",
           "set_property PACKAGE_PIN W5 [get_ports \"[list clk]\"]\n",
           "set_property PACKAGE_PIN W5 [get_ports {bus[0][1]}]\n",
           "set_property PACKAGE_PIN W5 [get_ports {a\nb}]\n",
           "set_property PACKAGE_PIN W5 [get_ports {sw*}]\n",
           "set_property PACKAGE_PIN W5 [get_ports {?}]\n"}) {
    const XdcDoc unsupported = parseXdc(source);
    CHECK(unsupported.pins.empty());
    CHECK(!unsupported.warnings.empty());
  }
  for (const char* value : {"nan", "inf", "-inf", "0", "-10", "1e999"}) {
    const XdcDoc bad = parseXdc(std::string("create_clock -period ") + value +
                              " [get_ports clk]\n");
    CHECK(bad.clocks.empty());
    CHECK(!bad.warnings.empty());
  }
  for (const char* waveform : {"nan 5", "0 inf", "-1 4", "5 5", "0 11"}) {
    const XdcDoc bad = parseXdc(std::string("create_clock -period 10 -waveform {") +
                              waveform + "} [get_ports clk]\n");
    CHECK_EQ(bad.clocks.size(), 1);
    CHECK(!bad.clocks[0].waveformNs);
    CHECK(!bad.warnings.empty());
  }
  // AMD UG835 explicitly permits falling edges earlier than rising edges.
  const XdcDoc reversed = parseXdc("create_clock -period 10 -waveform {7 2} [get_ports clk]\n");
  CHECK(reversed.warnings.empty());
  CHECK_EQ(reversed.clocks.size(), 1);
  CHECK(reversed.clocks[0].waveformNs == (std::pair{7.0, 2.0}));
}

int main(int argc, char** argv) {
  CHECK(argc >= 3);
  checkShippedDefault(readFile(argv[1]));
  checkPristine(readFile(argv[2]));
  checkGrammarVariants();
  checkLiteralSyntaxAndValidation();
  std::puts("test_xdc: PASS");
  return 0;
}
