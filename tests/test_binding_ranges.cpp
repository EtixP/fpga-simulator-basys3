#include "Vbinding_ranges.h"
#include "check.h"
#include "constraints/PinBinding.h"
#include "engine/VerilatorEngine.h"

#include <string>

int main() {
  auto engine = vb::makeVerilatorEngine<Vbinding_ranges>({.topModule = "binding_ranges"});
  const auto doc = vb::parseXdc(
      "set_property PACKAGE_PIN W5 [get_ports clk]\n"
      "set_property PACKAGE_PIN V17 [get_ports {sw_off[4]}]\n"
      "set_property PACKAGE_PIN V16 [get_ports {sw_up[0]}]\n"
      "set_property PACKAGE_PIN W16 [get_ports {sw_neg[-2]}]\n"
      "set_property PACKAGE_PIN W17 [get_ports {sw_one[5]}]\n"
      "set_property PACKAGE_PIN W15 [get_ports scalar]\n"
      "set_property PACKAGE_PIN U16 [get_ports {led[0]}]\n"
      "set_property PACKAGE_PIN E19 [get_ports {led[1]}]\n"
      "set_property PACKAGE_PIN U19 [get_ports {led[2]}]\n"
      "set_property PACKAGE_PIN V19 [get_ports {led[3]}]\n"
      "set_property PACKAGE_PIN W18 [get_ports {led[4]}]\n");
  CHECK(doc.warnings.empty());
  const auto binding = vb::PinBinding::bind(doc, *engine);
  CHECK(binding.diagnostics().empty());
  CHECK_EQ(binding.size(), 11);
  const char* ports[] = {"sw_off", "sw_up", "sw_neg", "sw_one", "scalar"};
  const uint64_t values[] = {1, 8, 4, 1, 1};
  for (unsigned i = 0; i < 5; ++i) {
    const std::string resource = "SW" + std::to_string(i);
    binding.setPin(*engine, resource, true);
    CHECK_EQ(engine->peek(engine->lookup(ports[i])), values[i]);
    // Independent RTL expression identifies the HDL bit, not the binder.
    CHECK_EQ(engine->peek(engine->lookup("led")), 1ull << i);
    CHECK(binding.getPin(*engine, "LED" + std::to_string(i)));
    binding.setPin(*engine, resource, false);
    CHECK_EQ(engine->peek(engine->lookup("led")), 0);
  }
  CHECK(engine->lookup("multi") == vb::kNoSignal);
  for (const auto& info : engine->ports()) CHECK(info.name != "multi");

  const auto duplicate = vb::PinBinding::bind(vb::parseXdc(
      "set_property PACKAGE_PIN V17 [get_ports {sw_one[5]}]\n"
      "set_property PACKAGE_PIN V17 [get_ports {sw_up[0]}]\n"), *engine);
  bool accurateDiagnostic = false;
  for (const auto& message : duplicate.diagnostics())
    if (message.find("both 'sw_one[5]' and 'sw_up[0]'") != std::string::npos)
      accurateDiagnostic = true;
  CHECK(accurateDiagnostic);

  for (const char* target : {"sw_off[0]", "sw_up[4]", "sw_neg[-5]", "sw_one",
                             "sw_one[0]", "scalar[0]", "binding_ranges.led[0]",
                             "multi[0]"}) {
    const auto bad = vb::parseXdc(std::string("set_property PACKAGE_PIN U16 [get_ports {") +
                                target + "}]\n");
    const auto skipped = vb::PinBinding::bind(bad, *engine);
    CHECK_EQ(skipped.size(), 0);
    CHECK(!skipped.diagnostics().empty());
  }
  std::puts("test_binding_ranges: PASS");
}
