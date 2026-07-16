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
using vb::SimEngine;
using vb::VerilatorEngineOptions;

static std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// The stepCapture override must be observably IDENTICAL to the shared default
// (step(1)+peek loop) — same captured bytes, same now(), same VCD — or the
// phase-3 R4 frame diff is unsound. Also pins the edge cases in the contract.
static void checkStepCapture(const std::string& vcdA, const std::string& vcdB) {
  auto ea = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  auto eb = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  const std::vector<SignalId> ids{ea->lookup("led"), ea->lookup("sw")};  // 16+4 bits
  const std::vector<SignalId> idsB{eb->lookup("led"), eb->lookup("sw")};
  for (SimEngine* e : {static_cast<SimEngine*>(ea.get()), static_cast<SimEngine*>(eb.get())}) {
    e->poke(e->lookup("btnC"), 1);
    e->step(2);
    e->poke(e->lookup("btnC"), 0);
    e->poke(e->lookup("sw"), 3);
  }
  std::vector<uint64_t> bufA(50), bufB(50);
  ea->stepCapture(50, ids, bufA.data());               // override
  eb->SimEngine::stepCapture(50, idsB, bufB.data());   // qualified base = default
  CHECK(bufA == bufB);
  CHECK_EQ(ea->now(), eb->now());
  // Packed layout: led at bits 0..15, sw at 16..19; count += 3 per edge.
  CHECK_EQ(bufA[0] & 0xFFFF, 3);
  CHECK_EQ((bufA[0] >> 16) & 0xF, 3);
  CHECK_EQ(bufA[49] & 0xFFFF, 150);

  // Interleaved poke mid-sequence: still identical.
  ea->poke(ea->lookup("sw"), 7);
  eb->poke(eb->lookup("sw"), 7);
  ea->stepCapture(20, ids, bufA.data());
  eb->SimEngine::stepCapture(20, idsB, bufB.data());
  CHECK(bufA == bufB);

  // Tracing ON: the override's VCD must byte-match the default's.
  auto ta = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  auto tb = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  ta->setTraceFile(vcdA);
  tb->setTraceFile(vcdB);
  ta->trace(true);
  tb->trace(true);
  ta->poke(ta->lookup("sw"), 5);
  tb->poke(tb->lookup("sw"), 5);
  std::vector<uint64_t> t1(30), t2(30);
  const std::vector<SignalId> tia{ta->lookup("led")};
  const std::vector<SignalId> tib{tb->lookup("led")};
  ta->stepCapture(30, tia, t1.data());
  tb->SimEngine::stepCapture(30, tib, t2.data());
  CHECK(t1 == t2);
  ta.reset();
  tb.reset();  // close both VCDs
  CHECK(readFile(vcdA) == readFile(vcdB));

  // Edge cases.
  auto e = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  std::vector<uint64_t> one(1, 0xDEADull);
  e->stepCapture(0, {e->lookup("led")}, one.data());  // no-op
  CHECK_EQ(one[0], 0xDEADull);
  CHECK_EQ(e->now(), 0);
  const SignalId led = e->lookup("led");
  CHECK_THROWS(e->stepCapture(1, {led, led, led, led, led}, one.data()),  // 80 bits
               std::invalid_argument);
  CHECK_THROWS(e->stepCapture(1, {kNoSignal}, one.data()), std::invalid_argument);
  CHECK_EQ(e->now(), 0);  // a rejected capture advanced no time
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
  checkStepCapture(std::string(argv[1]) + ".capA", std::string(argv[1]) + ".capB");

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
