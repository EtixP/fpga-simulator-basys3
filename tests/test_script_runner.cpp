// The exact scheduler used by the frontends, exercised without a window. A tiny
// recording engine exposes the inputs at every rising edge independently
// of the scheduler and the BoardModel structured log.
#include "board/BoardModel.h"
#include "check.h"
#include "constraints/Xdc.h"
#include "script/RunOptions.h"
#include "script/ScriptRunner.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class RecordingEngine final : public vb::SimEngine {
public:
  struct Edge {
    uint64_t cycle, switches;
    bool reset, rx;
    bool operator==(const Edge&) const = default;
  };
  std::vector<Edge> edges;

  vb::SignalId lookup(std::string_view name) override {
    for (size_t i = 0; i < ports_.size(); ++i)
      if (ports_[i].name == name) return static_cast<vb::SignalId>(i);
    return vb::kNoSignal;
  }
  vb::SignalInfo info(vb::SignalId id) const override {
    return ports_.at(static_cast<size_t>(id));
  }
  std::vector<vb::SignalInfo> ports() const override { return ports_; }
  uint64_t now() const override { return cycle_; }
  void step(uint64_t cycles) override {
    for (uint64_t i = 0; i < cycles; ++i) {
      edges.push_back({++cycle_, values_[3], values_[0] != 0, values_[4] != 0});
      values_[2] = values_[0] ? 0 : (values_[2] + values_[3]) & 0xFF;
    }
  }
  uint64_t peek(vb::SignalId id) override { return values_.at(static_cast<size_t>(id)); }
  void poke(vb::SignalId id, uint64_t value) override {
    const auto port = info(id);
    CHECK(port.input);
    CHECK(port.name != "clk");
    values_.at(static_cast<size_t>(id)) = value & ((uint64_t{1} << port.width) - 1);
  }
  void setTraceFile(std::string_view) override {}
  void trace(bool) override {}

private:
  uint64_t cycle_ = 0;
  const std::vector<vb::SignalInfo> ports_{
      {"btnC", 1, true}, {"clk", 1, true}, {"led", 8, false},
      {"sw", 4, true}, {"uart_rx", 1, true}};
  std::array<uint64_t, 5> values_{};
};

vb::PinBinding binding(RecordingEngine& engine) {
  const auto xdc = vb::parseXdc(
      "set_property PACKAGE_PIN W5 [get_ports clk]\n"
      "set_property PACKAGE_PIN U18 [get_ports btnC]\n"
      "set_property PACKAGE_PIN V17 [get_ports {sw[0]}]\n"
      "set_property PACKAGE_PIN V16 [get_ports {sw[1]}]\n"
      "set_property PACKAGE_PIN U16 [get_ports {led[0]}]\n"
      "set_property PACKAGE_PIN B18 [get_ports uart_rx]\n");
  CHECK(xdc.warnings.empty());
  auto bound = vb::PinBinding::bind(xdc, engine);
  CHECK(bound.diagnostics().empty());
  return bound;
}

bool hasLog(const vb::BoardModel& board, const std::string& line) {
  const auto& log = board.structuredLog();
  return std::find(log.begin(), log.end(), line) != log.end();
}

void checkEarlyEventsAndHorizon() {
  RecordingEngine engine;
  vb::BoardModel board(engine, binding(engine));
  vb::RunOptions opts;
  opts.logPath = "enabled-without-writing-a-file";
  opts.stimulus = {{0, "BTNC", false}, {0, "SW0", true}, {8, "SW0", false},
                   {16, "SW1", true}, {17, "SW1", false},
                   {1016, "SW0", true}, {1016, "SW0", false},
                   {1017, "SW0", true}};
  opts.sends = {{0, "K"}, {8, "!"}, {16, "?"}, {200'000, "later"}};
  vb::ScriptCursor cursor;
  vb::initializeScriptedRun(board, opts, cursor);
  CHECK_EQ(board.now(), 16);
  CHECK_EQ(engine.peek(engine.lookup("led")), 8);
  CHECK_EQ(cursor.event, 4);
  CHECK_EQ(cursor.send, 3);
  CHECK(hasLog(board, "[cycle 0] BTNC 0->1"));
  CHECK(hasLog(board, "[cycle 0] BTNC 1->0"));
  CHECK(hasLog(board, "[cycle 0] SW0 0->1"));
  CHECK(hasLog(board, "[cycle 8] SW0 1->0"));
  CHECK(hasLog(board, "[cycle 16] SW1 0->1"));
  CHECK(hasLog(board, "[cycle 0] UART RX 0x4B 'K'"));
  for (size_t i = 0; i < engine.edges.size(); ++i) {
    CHECK_EQ(engine.edges[i].cycle, i + 1);
    CHECK_EQ(engine.edges[i].switches, i < 8 ? 1u : 0u);
    CHECK(!engine.edges[i].reset);
    CHECK(!engine.edges[i].rx);  // exact-cycle UART start at zero
  }

  vb::advanceScripted(board, opts, cursor, 1000);
  CHECK_EQ(board.now(), 1016);
  CHECK_EQ(engine.peek(engine.lookup("led")), 10);
  CHECK_EQ(engine.edges[16].switches, 2);  // cycle16 input first sampled at17
  CHECK_EQ(engine.edges[17].switches, 0);
  CHECK_EQ(cursor.event, 7);  // both endpoint events were applied, in order
  CHECK(!board.switchState(0));
  CHECK(hasLog(board, "[cycle 1016] SW0 0->1"));
  CHECK(hasLog(board, "[cycle 1016] SW0 1->0"));
  vb::advanceScripted(board, opts, cursor, 1);
  CHECK_EQ(engine.peek(engine.lookup("led")), 10);  // endpoint input awaits edge1018
  CHECK(board.switchState(0));
  vb::advanceScripted(board, opts, cursor, 1);
  CHECK_EQ(engine.peek(engine.lookup("led")), 11);
  CHECK_EQ(cursor.send, 3);  // later send remains queued in the script

  CHECK(!vb::applyStimulus(board, {board.now(), "SW15", true}));
  CHECK(!vb::applyStimulus(board, {board.now(), "BTNU", true}));
  CHECK(!vb::applyStimulus(board, {board.now(), "missing", true}));
}

void checkResetAndZeroFrames() {
  {
    RecordingEngine engine;
    vb::BoardModel board(engine, binding(engine));
    vb::RunOptions opts;
    opts.logPath = "enabled";
    vb::ScriptCursor cursor;
    vb::initializeScriptedRun(board, opts, cursor);
    CHECK_EQ(board.now(), 16);
    CHECK(!board.buttonState(vb::Button::C));
    CHECK(hasLog(board, "[cycle 0] BTNC 0->1"));
    CHECK(hasLog(board, "[cycle 16] BTNC 1->0"));
    for (const auto& edge : engine.edges) CHECK(edge.reset);
  }
  {
    RecordingEngine engine;
    vb::BoardModel board(engine, binding(engine));
    vb::RunOptions opts;
    opts.stimulus = {{8, "BTNC", true}, {20, "BTNC", false}};
    vb::ScriptCursor cursor;
    vb::initializeScriptedRun(board, opts, cursor);
    CHECK(board.buttonState(vb::Button::C));  // explicit hold survives automatic tail
    vb::advanceScripted(board, opts, cursor, 5);
    for (size_t i = 0; i < 20; ++i) CHECK(engine.edges[i].reset);
    CHECK(!engine.edges[20].reset);
  }
  {
    RecordingEngine engine;
    vb::BoardModel board(engine, binding(engine));
    vb::RunOptions opts;
    opts.maxFrames = 0;
    opts.logPath = "enabled";
    opts.stimulus = {{0, "SW0", true}};
    opts.sends = {{0, "K"}};
    vb::ScriptCursor cursor;
    vb::initializeScriptedRun(board, opts, cursor);
    CHECK_EQ(board.now(), 0);
    CHECK(engine.edges.empty());
    CHECK_EQ(cursor.event, 0);
    CHECK_EQ(cursor.send, 0);
    CHECK(!board.buttonState(vb::Button::C));
    CHECK(!board.switchState(0));
    for (const auto& line : board.structuredLog()) CHECK(line.starts_with('#'));
  }
}

void checkChunkInvariance() {
  vb::RunOptions opts;
  opts.logPath = "enabled";
  opts.stimulus = {{0, "SW0", true}, {7, "SW0", false}, {16, "SW1", true},
                   {17, "SW1", false}, {999, "SW0", true}, {1000, "SW0", false},
                   {10417, "SW1", true}, {199999, "SW1", false}};
  opts.sends = {{0, "K"}, {8, "!"}, {16, "?"}, {150000, "L"}};
  RecordingEngine a, b;
  vb::BoardModel whole(a, binding(a)), split(b, binding(b));
  vb::ScriptCursor ca, cb;
  vb::initializeScriptedRun(whole, opts, ca);
  vb::initializeScriptedRun(split, opts, cb);
  vb::advanceScripted(whole, opts, ca, 220000);
  constexpr std::array<uint64_t, 6> chunks{1, 997, 3, 1000, 10417, 19};
  for (size_t part = 0; split.now() < whole.now(); ++part)
    vb::advanceScripted(split, opts, cb,
        std::min(chunks[part % chunks.size()], whole.now() - split.now()));
  CHECK(a.edges == b.edges);
  CHECK(whole.structuredLog() == split.structuredLog());
  CHECK_EQ(ca.event, cb.event);
  CHECK_EQ(ca.send, cb.send);
  CHECK_THROWS(vb::advanceScripted(whole, opts, ca, std::numeric_limits<uint64_t>::max()),
               std::overflow_error);
  CHECK_EQ(whole.now(), 220016);
}

}  // namespace

int main() {
  checkEarlyEventsAndHorizon();
  checkResetAndZeroFrames();
  checkChunkInvariance();
  std::puts("test_script_runner: PASS");
}
