// Milestone 1.1-1.2 acceptance: headless test pokes sw[3:0], steps 1000
// cycles, peeks led, golden values match — driven through the XDC-derived
// pin binding so 1.1 (engine) and 1.2 (constraints) are exercised together.
//
// argv[1] = examples/counter.xdc
// argv[2] = tests/data/counter_swapped.xdc (sw bound to SW4..SW7 pins)
// argv[3] = src/constraints/data/Basys3_Master.xdc (shipped full-board default)
#include "Vcounter.h"
#include "check.h"
#include "constraints/PinBinding.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"

#include <fstream>
#include <sstream>
#include <string>

using vb::makeVerilatorEngine;
using vb::parseXdc;
using vb::PinBinding;
using vb::SimEngine;
using vb::XdcDoc;

static std::string readFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static bool hasWarning(const std::vector<std::string>& diags) {
  for (const auto& d : diags)
    if (d.rfind("warning:", 0) == 0) return true;
  return false;
}

int main(int argc, char** argv) {
  CHECK(argc >= 4);

  auto engine = makeVerilatorEngine<Vcounter>({.topModule = "counter"});

  // t=0 contract: constructed, initialized, settled, no clock edge yet.
  CHECK_EQ(engine->now(), 0);
  const vb::SignalId led = engine->lookup("led");
  CHECK(led != vb::kNoSignal);
  CHECK_EQ(engine->peek(led), 0);

  // Bind the example XDC. Everything the design has is constrained and every
  // pin is a real board pin, so a clean bind has zero warning-level diags.
  const XdcDoc doc = parseXdc(readFile(argv[1]));
  CHECK_EQ(doc.warnings.size(), 0);
  CHECK_EQ(doc.pins.size(), 22);  // clk + btnC + sw[3:0] + led[15:0]
  const PinBinding bind = PinBinding::bind(doc, *engine);
  CHECK(!hasWarning(bind.diagnostics()));
  CHECK_EQ(bind.size(), 22);
  CHECK(bind.find("SW0") && bind.find("SW3") && bind.find("LED15"));
  CHECK(bind.find("BTNC") && bind.find("CLK100"));

  // --- The acceptance sequence, driven through board resources ------------
  // Reset: hold btnC across two rising edges, then release.
  bind.setPin(*engine, "BTNC", true);
  engine->step(2);
  bind.setPin(*engine, "BTNC", false);

  // sw = 3 (SW0|SW1). Golden derivation: count advances by sw on every rising
  // edge after reset released, and led = count combinationally, so after N
  // counting edges led == 3*N.
  bind.setPin(*engine, "SW0", true);
  bind.setPin(*engine, "SW1", true);

  engine->step(1);  // 1 counting edge
  CHECK_EQ(engine->peek(led), 3);
  CHECK_EQ(bind.getPin(*engine, "LED0"), true);
  CHECK_EQ(bind.getPin(*engine, "LED1"), true);
  CHECK_EQ(bind.getPin(*engine, "LED2"), false);

  engine->step(9);  // 10 counting edges
  CHECK_EQ(engine->peek(led), 30);

  engine->step(990);  // 1000 counting edges — the acceptance golden
  CHECK_EQ(engine->peek(led), 3000);
  // now() == 1002, not 1000: the 2 reset cycles above also advanced time.
  CHECK_EQ(engine->now(), 1002);

  // --- Whole-port poke path (not through the per-bit binding) -------------
  const vb::SignalId sw = engine->lookup("sw");
  const vb::SignalId btnC = engine->lookup("btnC");
  engine->poke(btnC, 1);
  engine->step(2);
  engine->poke(btnC, 0);
  engine->poke(sw, 0xF);
  engine->step(10);
  CHECK_EQ(engine->peek(led), 150);  // 15 * 10 edges
  CHECK_EQ(engine->now(), 1014);     // 1002 + 2 reset + 10 counting

  // --- Pin-keyed (not name-keyed) binding is real -------------------------
  // Same design, but the XDC places sw[3:0] on the SW4..SW7 pins. If binding
  // were secretly keyed on port names, SW0 would still exist and SW4 would
  // not drive anything.
  auto engine2 = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  const XdcDoc swapped = parseXdc(readFile(argv[2]));
  CHECK_EQ(swapped.warnings.size(), 0);
  const PinBinding bind2 = PinBinding::bind(swapped, *engine2);
  CHECK(!hasWarning(bind2.diagnostics()));
  CHECK(bind2.find("SW4") != nullptr);
  CHECK(bind2.find("SW0") == nullptr);

  bind2.setPin(*engine2, "BTNC", true);
  engine2->step(2);
  bind2.setPin(*engine2, "BTNC", false);
  bind2.setPin(*engine2, "SW4", true);  // -> design sw[0]
  engine2->step(5);
  CHECK_EQ(engine2->peek(engine2->lookup("led")), 5);

  // --- Shipped full-board default binds a small design without noise ------
  // 105 constraints against counter's 4 ports: 22 bind (clk, btnC, sw[0..3],
  // led[0..15]); the rest — missing ports and sw[4..15] beyond the 4-bit bus
  // — are the expected case under builtinDefault and must stay note-level.
  {
    const XdcDoc def = parseXdc(readFile(argv[3]));
    CHECK_EQ(def.warnings.size(), 0);
    const PinBinding defBind =
        PinBinding::bind(def, *engine, PinBinding::Options{.builtinDefault = true});
    CHECK_EQ(defBind.size(), 22);
    CHECK(!hasWarning(defBind.diagnostics()));
  }

  // --- Binding diagnostics: every skip/conflict is loud (R2: never silent) --
  {
    const XdcDoc bad = parseXdc(
        "set_property PACKAGE_PIN Z99 [get_ports clk]\n"       // not a board pin
        "set_property PACKAGE_PIN V17 [get_ports {sw[9]}]\n"   // past 4-bit width
        "set_property PACKAGE_PIN V16 [get_ports sw]\n"        // whole-bus, no index
        "set_property PACKAGE_PIN W16 [get_ports {led[0]}]\n"  // switch pin -> output
        "set_property PACKAGE_PIN W17 [get_ports {sw[0]}]\n"
        "set_property PACKAGE_PIN W17 [get_ports btnC]\n");    // duplicate pin
    CHECK_EQ(bad.warnings.size(), 0);  // all parse; the BINDER must complain
    const PinBinding badBind = PinBinding::bind(bad, *engine);
    size_t warns = 0;
    for (const auto& d : badBind.diagnostics())
      if (d.rfind("warning:", 0) == 0) ++warns;
    CHECK_EQ(warns, 5);
    CHECK_EQ(badBind.size(), 1);  // only W17 (SW3) survives...
    CHECK(badBind.find("SW3") != nullptr);
    CHECK(badBind.find("SW3")->id == engine->lookup("btnC"));  // ...last write wins
    CHECK_THROWS(badBind.setPin(*engine, "SW9", true), std::invalid_argument);
    CHECK_THROWS(badBind.getPin(*engine, "LED0"), std::invalid_argument);
  }

  // --- R4 pattern: two live engines, the OLDER one destroyed first ---------
  // Verilator's scope teardown resolves the thread-local context; without the
  // engine's bindThreadContext() discipline this ordering corrupts/crashes.
  // The survivor must keep simulating.
  {
    auto a = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
    auto b = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
    const vb::SignalId swA = a->lookup("sw");
    const vb::SignalId swB = b->lookup("sw");
    a->poke(swA, 1);
    b->poke(swB, 2);
    a->step(3);
    b->step(4);
    CHECK_EQ(a->peek(a->lookup("led")), 3);
    CHECK_EQ(b->peek(b->lookup("led")), 8);
    a.reset();  // non-LIFO: first-constructed engine dies while b lives
    b->step(1);
    CHECK_EQ(b->peek(b->lookup("led")), 10);
    CHECK_EQ(b->now(), 5);
  }

  std::puts("test_counter_headless: PASS");
  return 0;
}
