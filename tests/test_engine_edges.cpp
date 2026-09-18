#include "Vengine_edges.h"
#include "check.h"
#include "engine/VerilatorEngine.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

static auto make() {
  return vb::makeVerilatorEngine<Vengine_edges>({.topModule = "engine_edges"});
}
static uint64_t value(vb::SimEngine& e, const char* name) {
  return e.peek(e.lookup(name));
}
static void checkCounts(vb::SimEngine& e, uint64_t cycles) {
  CHECK_EQ(e.now(), cycles);
  CHECK_EQ(value(e, "pos_count"), 5 + cycles);
  CHECK_EQ(value(e, "neg_count"), 11 + (cycles ? cycles - 1 : 0));
  CHECK_EQ(value(e, "derived_count"), (cycles + 1) / 2);
}
int main(int argc, char** argv) {
  CHECK_EQ(argc, 2);
  auto e = make();
  checkCounts(*e, 0);
  CHECK_EQ(value(*e, "clk"), 0);
  CHECK_EQ(value(*e, "din"), 0);
  CHECK_EQ(value(*e, "async_count"), 0);
  CHECK_EQ(value(*e, "sampled"), 0);
  CHECK_EQ(value(*e, "comb"), 0xA6);
  CHECK_EQ(value(*e, "mem0"), 0x96);
  CHECK_EQ(value(*e, "mem1"), 0x3B);
  CHECK(e->lookup("too_wide") == vb::kNoSignal);
  CHECK(e->lookup("engine_edges.memory") == vb::kNoSignal);
  e->poke(e->lookup("din"), 0x196);
  e->step(0);
  checkCounts(*e, 0);
  CHECK_EQ(value(*e, "comb"), 0x30);
  CHECK_EQ(value(*e, "sampled"), 0);
  e->step(1);
  checkCounts(*e, 1);
  CHECK_EQ(value(*e, "clk"), 1);
  CHECK_EQ(value(*e, "sampled"), 0x30);
  e->poke(e->lookup("din"), 0x21);
  e->step(999);
  checkCounts(*e, 1000);
  CHECK_EQ(value(*e, "sampled"), 0x87);

  // A reset edge may settle during peek; it never advances master time.
  e->poke(e->lookup("rst"), 1);
  CHECK_EQ(value(*e, "async_count"), 0);
  CHECK_EQ(e->now(), 1000);
  e->poke(e->lookup("rst"), 0);
  CHECK_EQ(value(*e, "async_count"), 0);
  e->step(1);
  CHECK_EQ(value(*e, "async_count"), 1);
  checkCounts(*e, 1001);

  e->poke(e->lookup("in16"), ~0ull);
  e->poke(e->lookup("in32"), 0xFEDCBA9876543210ull);
  e->poke(e->lookup("in64"), 0xFEDCBA9876543210ull);
  CHECK_EQ(value(*e, "out16"), 0xFFFF);
  CHECK_EQ(value(*e, "out32"), 0x76543210);
  CHECK_EQ(value(*e, "out64"), 0xFEDCBA9876543210ull);
  uint64_t packed = 0;
  e->stepCapture(1, {e->lookup("out64")}, &packed);
  CHECK_EQ(packed, 0xFEDCBA9876543210ull);
  const auto before = e->now();
  CHECK_THROWS(e->stepCapture(0, {vb::kNoSignal}, &packed), std::invalid_argument);
  CHECK_THROWS(e->SimEngine::stepCapture(0, {vb::kNoSignal}, &packed), std::invalid_argument);
  CHECK_EQ(e->now(), before);

  // Whole versus irregular partitions, distinct engine-scoped handles.
  auto whole = make();
  auto split = make();
  whole->step(1'000'003);
  for (const uint64_t n : {0ull, 1ull, 999ull, 31ull, 98'972ull, 900'000ull})
    split->step(n);
  checkCounts(*whole, 1'000'003);
  checkCounts(*split, 1'000'003);
  for (const auto& p : whole->ports())
    CHECK_EQ(whole->peek(whole->lookup(p.name)), split->peek(split->lookup(p.name)));

  // Destroy out of construction order, fail another constructor, then use
  // survivors: no context or symbol storage may belong to the dead engine.
  e.reset();
  CHECK_THROWS(vb::makeVerilatorEngine<Vengine_edges>({.topModule="wrong"}),
               std::invalid_argument);
  whole->step(7);
  split->step(2);
  checkCounts(*whole, 1'000'010);
  checkCounts(*split, 1'000'005);
  whole.reset();
  split->poke(split->lookup("din"), 0x52);
  split->step(1);
  CHECK_EQ(value(*split, "sampled"), 0xF4);

  // Independent timestamp oracle (no second engine generating expected VCD).
  auto trace = make();
  const std::string path = argv[1];
  trace->setTraceFile(path + "/missing/trace.vcd");
  CHECK_THROWS(trace->trace(true), std::runtime_error);
  trace->setTraceFile(path);
  trace->trace(true);
  trace->step(2);
  trace->poke(trace->lookup("din"), 3);
  (void)value(*trace, "comb");  // no trace event or master edge
  trace->step(0);
  trace->trace(false);
  trace->step(2);
  trace->trace(true);
  trace->step(1);
  trace.reset();
  std::ifstream in(path);
  CHECK(in.good());
  std::vector<uint64_t> stamps;
  std::string line;
  while (std::getline(in, line))
    if (!line.empty() && line.front() == '#') stamps.push_back(std::stoull(line.substr(1)));
  CHECK(stamps == std::vector<uint64_t>({0, 5, 10, 15, 40, 45}));
  std::puts("test_engine_edges: PASS");
}
