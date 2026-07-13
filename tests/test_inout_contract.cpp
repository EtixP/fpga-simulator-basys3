// SimEngine contract: inout ports are unsupported in phase 1 (R5 defers
// tri-state; a 2-state engine cannot service a Z-capable pin). They must be
// omitted from ports() and unresolvable via lookup() — the R4 checker diffs
// the ports() enumeration, so a half-serviced port there would poison it.
#include "Vioport.h"
#include "check.h"
#include "engine/VerilatorEngine.h"

int main() {
  auto engine = vb::makeVerilatorEngine<Vioport>({.topModule = "ioport"});

  const auto ports = engine->ports();
  CHECK_EQ(ports.size(), 2);
  CHECK(ports[0].name == "clk");
  CHECK(ports[1].name == "led");
  CHECK(engine->lookup("io") == vb::kNoSignal);

  engine->step(3);  // the rest of the engine still works around the inout
  CHECK_EQ(engine->now(), 3);
  CHECK_EQ(engine->peek(engine->lookup("led")), 0);

  std::puts("test_inout_contract: PASS");
  return 0;
}
