// Locks the SimEngine contract onto VerilatorEngine: t=0 semantics, the
// engine-owned clock, poke/peek rules, name resolution tiers, the
// shadow-copy invariant, and the trace lifecycle. NetlistEngine (phase 3)
// must pass the engine-generic parts of this file unchanged.
//
// argv[1] = scratch path for the VCD written by the trace test
#include "Vcounter.h"
#include "check.h"
#include "engine/VerilatorEngine.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using vb::kNoSignal;
using vb::makeVerilatorEngine;
using vb::SignalId;
using vb::SignalInfo;
using vb::VerilatorEngineOptions;

static std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void checkConstructionErrors() {
  CHECK_THROWS(makeVerilatorEngine<Vcounter>({.topModule = ""}), std::invalid_argument);
  CHECK_THROWS(makeVerilatorEngine<Vcounter>({.topModule = "TOP"}), std::invalid_argument);
  CHECK_THROWS(makeVerilatorEngine<Vcounter>({.topModule = "nope"}), std::invalid_argument);
  CHECK_THROWS(makeVerilatorEngine<Vcounter>({.topModule = "counter", .clockName = "nope"}),
               std::invalid_argument);
}

int main(int argc, char** argv) {
  CHECK(argc >= 2);
  checkConstructionErrors();

  auto engine = makeVerilatorEngine<Vcounter>({.topModule = "counter"});

  // --- ports(): all supported top-level ports, sorted by name -------------
  const std::vector<SignalInfo> ports = engine->ports();
  CHECK_EQ(ports.size(), 4);
  CHECK(ports[0].name == "btnC" && ports[0].width == 1 && ports[0].input);
  CHECK(ports[1].name == "clk" && ports[1].width == 1 && ports[1].input);
  CHECK(ports[2].name == "led" && ports[2].width == 16 && !ports[2].input);
  CHECK(ports[3].name == "sw" && ports[3].width == 4 && ports[3].input);

  // --- name resolution tiers ----------------------------------------------
  const SignalId sw = engine->lookup("sw");
  const SignalId led = engine->lookup("led");
  const SignalId clk = engine->lookup("clk");
  const SignalId btnC = engine->lookup("btnC");
  CHECK(sw != kNoSignal && led != kNoSignal && clk != kNoSignal && btnC != kNoSignal);
  CHECK(engine->lookup("nosuch") == kNoSignal);
  // Plain names never resolve to internals; the hierarchical tier does.
  CHECK(engine->lookup("count") == kNoSignal);
  const SignalId count = engine->lookup("counter.count");
  CHECK(count != kNoSignal);
  CHECK_EQ(engine->info(count).width, 16);
  CHECK(!engine->info(count).input);

  // --- invalid ids and poke restrictions -----------------------------------
  CHECK_THROWS(engine->info(kNoSignal), std::invalid_argument);
  CHECK_THROWS(engine->peek(kNoSignal), std::invalid_argument);
  CHECK_THROWS(engine->poke(kNoSignal, 0), std::invalid_argument);
  CHECK_THROWS(engine->poke(led, 1), std::invalid_argument);    // output
  CHECK_THROWS(engine->poke(count, 1), std::invalid_argument);  // internal
  // The engine owns the master clock (R1): poking it is rejected even though
  // it is an input port.
  CHECK(engine->info(clk).input);
  CHECK_THROWS(engine->poke(clk, 1), std::invalid_argument);

  // --- poke/peek contract: no time, no clock, comb settle ------------------
  CHECK_EQ(engine->now(), 0);
  engine->poke(sw, 5);
  CHECK_EQ(engine->peek(sw), 5);   // just-poked input reads back
  CHECK_EQ(engine->peek(led), 0);  // no clock edge -> count unchanged
  CHECK_EQ(engine->now(), 0);      // peek/poke never advance time
  engine->poke(sw, 0xFF);
  CHECK_EQ(engine->peek(sw), 0xF);  // masked to the 4-bit port width

  // --- step semantics and the shadow-copy invariant ------------------------
  engine->poke(btnC, 1);
  engine->step(2);
  engine->poke(btnC, 0);
  engine->poke(sw, 1);
  engine->step(3);
  CHECK_EQ(engine->peek(led), 3);
  // Module-scope shadows agree with ports-scope storage after stepping: this
  // is the regression test for "poke only through TOP.TOP" (see decision log).
  CHECK_EQ(engine->peek(count), 3);
  CHECK_EQ(engine->peek(engine->lookup("counter.sw")), 1);
  CHECK_EQ(engine->now(), 5);

  // --- trace lifecycle ------------------------------------------------------
  const std::string vcdPath = argv[1];
  CHECK_THROWS(engine->trace(true), std::logic_error);  // no file set yet
  engine->setTraceFile(vcdPath);
  engine->trace(true);  // lazily enabled mid-simulation (now()==5), must work
  engine->step(5);
  engine->trace(false);  // pause + flush
  engine->step(5);
  engine->trace(true);  // resume into the same file
  engine->step(5);
  CHECK_THROWS(engine->setTraceFile("elsewhere.vcd"), std::logic_error);
  CHECK_EQ(engine->now(), 20);
  engine.reset();  // destructor closes the VCD

  const std::string vcd = readFile(vcdPath);
  CHECK(vcd.find("$timescale") != std::string::npos);
  CHECK(vcd.find("1ns") != std::string::npos);  // R1: honest 10 ns cycles
  CHECK(vcd.find("$enddefinitions") != std::string::npos);
  CHECK(vcd.find("count") != std::string::npos);  // hierarchy preserved (R3)
  // Timestamp spacing locks timeInc(5)/half-cycle AND that trace(false)
  // really pauses dumping: enabled at cycle 5 -> first dump #50; the paused
  // window covers cycles 10..14 (#100..#145, absent); resume dumps at #150.
  CHECK(vcd.find("#50") != std::string::npos);
  CHECK(vcd.find("#100") == std::string::npos);
  CHECK(vcd.find("#150") != std::string::npos);

  std::puts("test_engine_contract: PASS");
  return 0;
}
