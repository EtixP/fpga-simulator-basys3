// Independent peripheral contracts: synthetic pins and analytically generated
// serial waveforms, without a Verilated design or regenerated golden data.
#include "board/BoardModel.h"
#include "check.h"

#include <algorithm>
#include <array>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace vb;

namespace {
constexpr uint64_t kGrid = BoardModel::kSampleChunkCycles;
constexpr uint64_t kBit = kUartCyclesPerBit;

// A complete ideal 8N1 frame, independently expressed as intervals. Index 0
// is the start bit, 1..8 are data LSB-first, and index 9 is the stop bit.
bool serialLevel(uint64_t now, uint64_t start, uint64_t bit,
                 const std::vector<uint8_t>& bytes) {
  if (now < start || now - start >= bytes.size() * 10 * bit) return true;
  const uint64_t frame = (now - start) / (10 * bit);
  const uint64_t index = ((now - start) % (10 * bit)) / bit;
  if (index == 0) return false;
  if (index == 9) return true;
  return (bytes[frame] >> (index - 1)) & 1;
}

void uartValidFrames() {
  std::vector<uint8_t> bytes;
  for (uint32_t value = 0; value < 256; ++value)
    bytes.push_back(static_cast<uint8_t>(value));
  // Every phase of the 1000-cycle observation grid, including the smallest
  // documented safe bit duration. The expected bytes come from the stimulus,
  // not from another decoder sharing its timing calculation.
  for (uint64_t bit : {kBit, 4 * kGrid}) {
    for (uint64_t phase = 0; phase < kGrid; ++phase) {
      UartTxDecoder decoder(bit);
      const uint64_t start = 1234 + phase;
      std::vector<uint8_t> received;
      for (uint64_t now = kGrid; now < start + 257 * 10 * bit; now += kGrid) {
        const auto result = decoder.sample(now, serialLevel(now, start, bit, bytes));
        CHECK(!result.framingError);
        if (result.byte) received.push_back(*result.byte);
      }
      CHECK(received == bytes);
    }
  }

  UartRxDriver driver;
  constexpr uint64_t start = 137;
  // Each returned start cycle is the analytical frame start below.
  for (uint64_t frame = 0; frame < bytes.size(); ++frame)
    CHECK_EQ(driver.send(bytes[frame], start), start + frame * 10 * kBit);
  for (uint64_t frame = 0; frame < bytes.size(); ++frame) {
    for (uint64_t bit = 0; bit < 10; ++bit) {
      const uint64_t edgeCycle = start + frame * 10 * kBit + bit * kBit;
      CHECK_EQ(driver.nextEdgeCycle(), edgeCycle);
      const auto edge = driver.advance(edgeCycle);
      CHECK_EQ(edge.byteStart, bit == 0);
      if (edge.byteStart) CHECK_EQ(edge.byte, bytes[frame]);
      const bool expected = bit == 0 ? false : bit == 9 ? true
          : ((bytes[frame] >> (bit - 1)) & 1);
      CHECK_EQ(edge.level, expected);
    }
  }
  CHECK(driver.idle());
  CHECK_EQ(driver.nextEdgeCycle(), UartRxDriver::kIdle);
  // Although all bit edges have been consumed, the previous frame retains
  // the line until its stop bit has lasted one full bit duration.
  const uint64_t freeCycle = start + bytes.size() * 10 * kBit;
  CHECK_EQ(driver.send(0x96, freeCycle - 1), freeCycle);
  CHECK_EQ(driver.nextEdgeCycle(), freeCycle);
  // A byte queued behind it chains one frame later; an idle line starts now.
  CHECK_EQ(driver.send(0x69, freeCycle), freeCycle + 10 * kBit);
  UartRxDriver idle;
  CHECK_EQ(idle.send(0x00, 5), 5);
}

void sevenSegmentBoundaries() {
  SevenSeg display;
  for (uint32_t digit = 0; digit < 4; ++digit) {
    CHECK_EQ(display.segments(0, digit), 0);
    CHECK(!display.dp(0, digit));
    CHECK_EQ(display.lastLit(digit), SevenSeg::kNeverLit);
  }
  constexpr uint64_t sample = 999;
  display.sample(sample, 0x0E, 0x79, false);  // digit 0, lit mask 0x06 = 1 + DP
  CHECK_EQ(display.segments(sample, 0), 0x06);
  CHECK(display.dp(sample, 0));
  CHECK_EQ(display.lastLit(0), sample);
  CHECK_EQ(display.segments(sample, 1), 0);
  // An inactive anode must not overwrite or refresh the latched digit.
  display.sample(sample + 1, 0x0F, 0x00, true);
  CHECK_EQ(display.lastLit(0), sample);
  CHECK_EQ(display.segments(sample + SevenSeg::kPersistCycles, 0), 0x06);
  CHECK(display.dp(sample + SevenSeg::kPersistCycles, 0));
  CHECK_EQ(display.segments(sample + SevenSeg::kPersistCycles + 1, 0), 0);
  CHECK(!display.dp(sample + SevenSeg::kPersistCycles + 1, 0));
  // Active-high raw pins are all dark, and simultaneous active anodes latch
  // the shared cathodes independently.
  display.sample(3'000'000, 0x00, 0x7F, true);
  for (uint32_t digit = 0; digit < 4; ++digit) {
    CHECK_EQ(display.segments(3'000'000, digit), 0);
    CHECK(!display.dp(3'000'000, digit));
    CHECK_EQ(display.lastLit(digit), 3'000'000);
  }
  CHECK_EQ(display.segments(3'000'000, 99), 0);
  CHECK(!display.dp(3'000'000, 99));
  CHECK_EQ(display.lastLit(99), SevenSeg::kNeverLit);
  const std::array<uint8_t, 18> patterns{
      0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F,
      0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71, 0x00, 0x40};
  constexpr char chars[] = "0123456789AbCdEF -";
  for (size_t i = 0; i < patterns.size(); ++i)
    CHECK_EQ(decodeSevenSeg(patterns[i]), chars[i]);
  CHECK_EQ(decodeSevenSeg(0x01), '?');
}

// This fixture advances time arithmetically; outputs are functions of that
// time or explicitly supplied levels. It cannot accidentally reproduce the
// board's stepping, logging, mux fusion or UART algorithms.
class PeripheralEngine final : public SimEngine {
public:
  enum Port : uint32_t { Switch, ButtonInput, Rx, Led, Anode, Segment, Dp, Tx };
  std::array<uint64_t, 8> values{0, 0, 0, 0, 15, 127, 1, 1};
  std::vector<std::pair<uint64_t, uint64_t>> rxPokes;
  std::function<uint64_t(Port, uint64_t)> output;

  SignalId lookup(std::string_view name) override {
    for (size_t i = 0; i < metadata_.size(); ++i)
      if (metadata_[i].name == name) return SignalId(i);
    return kNoSignal;
  }
  SignalInfo info(SignalId id) const override { return metadata_.at(size_t(id)); }
  std::vector<SignalInfo> ports() const override { return metadata_; }
  void step(uint64_t cycles) override { now_ += cycles; }
  uint64_t now() const override { return now_; }
  uint64_t peek(SignalId id) override {
    const auto index = static_cast<uint32_t>(id);
    if (index >= values.size()) throw std::invalid_argument("invalid signal");
    return output && index >= Led ? output(Port(index), now_) : values[index];
  }
  void poke(SignalId id, uint64_t value) override {
    const auto index = static_cast<uint32_t>(id);
    if (index > Rx) throw std::invalid_argument("not an input");
    values[index] = value & 1;
    if (index == Rx) rxPokes.emplace_back(now_, value & 1);
  }
  void setTraceFile(std::string_view) override {}
  void trace(bool) override {}

private:
  uint64_t now_ = 0;
  const std::vector<SignalInfo> metadata_{
      {"sw", 1, true}, {"btn", 1, true}, {"rx", 1, true},
      {"led", 1, false}, {"an", 4, false}, {"seg", 7, false},
      {"dp", 1, false}, {"tx", 1, false}};
};

constexpr const char* kXdc = R"(
set_property PACKAGE_PIN V17 [get_ports sw]
set_property PACKAGE_PIN U18 [get_ports btn]
set_property PACKAGE_PIN B18 [get_ports rx]
set_property PACKAGE_PIN A18 [get_ports tx]
set_property PACKAGE_PIN U16 [get_ports led]
set_property PACKAGE_PIN U2 [get_ports {an[0]}]
set_property PACKAGE_PIN U4 [get_ports {an[1]}]
set_property PACKAGE_PIN V4 [get_ports {an[2]}]
set_property PACKAGE_PIN W4 [get_ports {an[3]}]
set_property PACKAGE_PIN W7 [get_ports {seg[0]}]
set_property PACKAGE_PIN W6 [get_ports {seg[1]}]
set_property PACKAGE_PIN U8 [get_ports {seg[2]}]
set_property PACKAGE_PIN V8 [get_ports {seg[3]}]
set_property PACKAGE_PIN U5 [get_ports {seg[4]}]
set_property PACKAGE_PIN V5 [get_ports {seg[5]}]
set_property PACKAGE_PIN U7 [get_ports {seg[6]}]
set_property PACKAGE_PIN V7 [get_ports dp]
)";

PinBinding binding(PeripheralEngine& engine) {
  auto result = PinBinding::bind(parseXdc(kXdc), engine);
  CHECK(result.diagnostics().empty());
  return result;
}

struct Observation {
  std::vector<std::string> log;
  std::vector<std::pair<uint64_t, uint64_t>> rx;
  std::vector<uint8_t> tx;
  std::vector<uint64_t> txCycles;
  std::array<uint64_t, 4> lastLit;
};

// Cycle stamps of log lines containing `body`, in emission order.
std::vector<uint64_t> logStamps(const std::vector<std::string>& log, const std::string& body) {
  std::vector<uint64_t> stamps;
  for (const auto& line : log) {
    if (line.rfind("[cycle ", 0) != 0 || line.find(body) == std::string::npos) continue;
    stamps.push_back(std::stoull(line.substr(7)));
  }
  return stamps;
}

Observation runPartition(bool split) {
  PeripheralEngine engine;
  const std::vector<uint8_t> bytes{0x96, 0x01, 0x80, 0xA6};
  engine.output = [&](PeripheralEngine::Port port, uint64_t now) -> uint64_t {
    switch (port) {
      case PeripheralEngine::Led: return engine.values[0] ^ ((now / 1700) & 1);
      case PeripheralEngine::Anode:
        return now < 12'000 ? (15 ^ (1 << ((now / 3000) % 4))) : 15;
      case PeripheralEngine::Segment: return 0x40;  // lit 0
      case PeripheralEngine::Dp: return false;
      case PeripheralEngine::Tx: return serialLevel(now, 12'000, kBit, bytes);
      default: CHECK(false); return 0;
    }
  };
  BoardModel board(engine, binding(engine));
  CHECK(board.hasUartTx() && board.hasUartRx() && board.hasDisplay());
  CHECK_EQ(engine.values[PeripheralEngine::Rx], 1);  // host idle pull-up
  board.setLogEnabled(true);
  for (uint64_t target : {999ull, 1000ull, 1001ull, 7317ull, 2'219'999ull}) {
    while (board.now() < target) {
      const uint64_t part = split ? (board.now() * 13 + 31) % 1493 + 1 : target;
      board.tick(std::min(target - board.now(), part));
    }
    board.setSwitch(0, !board.switchState(0));
    board.setButton(Button::C, !board.buttonState(Button::C));
    if (target < 2000) board.sendUart(0xA6);
    board.tick(0);
  }
  CHECK_EQ(board.now(), 2'219'999);
  CHECK(board.uartTxBytes() == bytes);
  CHECK(board.uartTxByteCycles() == logStamps(board.structuredLog(), "] UART TX 0x"));
  std::array<uint64_t, 4> lastLit;
  for (uint32_t digit = 0; digit < 4; ++digit) {
    CHECK_EQ(board.digitSegments(digit), 0);  // persistence expired
    lastLit[digit] = board.digitLastLit(digit);
  }
  return {board.structuredLog(), engine.rxPokes, board.uartTxBytes(),
          board.uartTxByteCycles(), lastLit};
}

void boardPartitionsAndZeroTick() {
  const auto whole = runPartition(false);
  const auto parts = runPartition(true);
  CHECK(whole.log == parts.log);
  CHECK(whole.rx == parts.rx);
  CHECK(whole.tx == parts.tx);
  CHECK(whole.txCycles == parts.txCycles);
  CHECK(whole.lastLit == parts.lastLit);

  PeripheralEngine engine;
  BoardModel board(engine, binding(engine));
  board.setLogEnabled(true);
  const auto initialInputs = engine.values;
  for (uint32_t invalid : {5u, 99u, 0xFFFFFFFFu}) {
    const auto button = static_cast<Button>(invalid);
    CHECK(!board.hasButton(button));
    CHECK(!board.buttonState(button));
    board.setButton(button, true);
    board.setButton(button, false);
  }
  CHECK(engine.values == initialInputs);
  const size_t headers = board.structuredLog().size();
  board.tick(0);
  CHECK_EQ(board.now(), 0);
  CHECK_EQ(board.structuredLog().size(), headers);
  board.sendUart(0x96);
  CHECK_EQ(engine.values[PeripheralEngine::Rx], 1);  // queued until tick
  board.tick(0);
  CHECK_EQ(board.now(), 0);
  CHECK_EQ(engine.values[PeripheralEngine::Rx], 0);
  CHECK(board.structuredLog().back() == "[cycle 0] UART RX 0x96 '.'");
  const auto edges = engine.rxPokes;
  const auto log = board.structuredLog();
  board.tick(0);
  CHECK(engine.rxPokes == edges);
  CHECK(board.structuredLog() == log);
}

void boardGridAndLogOrder() {
  PeripheralEngine engine;
  engine.values[PeripheralEngine::Anode] = 0x00;
  engine.values[PeripheralEngine::Segment] = 0x40;
  engine.values[PeripheralEngine::Dp] = 0;
  engine.values[PeripheralEngine::Led] = 1;
  BoardModel board(engine, binding(engine));
  board.setLogEnabled(true);
  const size_t headers = board.structuredLog().size();
  board.tick(999);
  CHECK_EQ(board.digitChar(0), ' ');
  CHECK(board.ledState(0));  // direct view; log is still grid-observed
  CHECK_EQ(board.structuredLog().size(), headers);
  board.tick(1);
  board.sendUart(0x96);
  board.tick(0);
  board.setSwitch(0, true);
  board.setSwitch(0, true);  // no repeated transition
  const std::vector<std::string> expected{
      "[cycle 1000] SSEG[0] ' '->'0'",
      "[cycle 1000] SSEG[0].dp 0->1",
      "[cycle 1000] SSEG[1] ' '->'0'",
      "[cycle 1000] SSEG[1].dp 0->1",
      "[cycle 1000] SSEG[2] ' '->'0'",
      "[cycle 1000] SSEG[2].dp 0->1",
      "[cycle 1000] SSEG[3] ' '->'0'",
      "[cycle 1000] SSEG[3].dp 0->1",
      "[cycle 1000] LED[0] 0->1",
      "[cycle 1000] UART RX 0x96 '.'",
      "[cycle 1000] SW0 0->1"};
  CHECK(std::vector<std::string>(board.structuredLog().begin() + headers,
                                 board.structuredLog().end()) == expected);

  board.setLogEnabled(false);
  engine.values[PeripheralEngine::Led] = 0;
  board.tick(1000);
  CHECK_EQ(board.structuredLog().size(), headers + expected.size());
  board.clearLog();
  CHECK_EQ(board.structuredLog().size(), headers);
  board.setLogEnabled(true);
  board.tick(1000);
  CHECK_EQ(board.structuredLog().size(), headers);  // no synthetic snapshot
  engine.values[PeripheralEngine::Led] = 1;
  board.tick(1000);
  CHECK_EQ(board.structuredLog().size(), headers + 1);
  CHECK(board.structuredLog().back() == "[cycle 4000] LED[0] 0->1");
}

void uartSameStampOrder() {
  PeripheralEngine engine;
  const std::vector<uint8_t> bytes{0x96};
  engine.output = [&](PeripheralEngine::Port port, uint64_t now) -> uint64_t {
    const bool changed = now >= 101'000;
    switch (port) {
      case PeripheralEngine::Led: return !changed;
      case PeripheralEngine::Anode: return 0x0E;
      case PeripheralEngine::Segment: return changed ? 0x79 : 0x40;
      case PeripheralEngine::Dp: return changed;
      case PeripheralEngine::Tx: return serialLevel(now, 1001, kBit, bytes);
      default: CHECK(false); return 0;
    }
  };
  BoardModel board(engine, binding(engine));
  board.setLogEnabled(true);
  board.tick(100'000);
  CHECK(board.uartTxBytes().empty());
  const size_t previous = board.structuredLog().size();
  board.tick(1000);
  board.sendUart(0xA6);
  board.tick(0);
  const std::vector<std::string> expected{
      "[cycle 101000] SSEG[0] '0'->'1'",
      "[cycle 101000] SSEG[0].dp 1->0",
      "[cycle 101000] LED[0] 1->0",
      "[cycle 101000] UART TX 0x96 '.'",
      "[cycle 101000] UART RX 0xA6 '.'"};
  CHECK(std::vector<std::string>(board.structuredLog().begin() + previous,
                                 board.structuredLog().end()) == expected);
}

// The frontend's UART stamps are the board's own log stamps, recorded even
// while logging is off, for bytes and for each framing error.
void uartBoardStamps() {
  const std::vector<uint8_t> bytes{0x00, 0x96, 0xFF};
  const std::vector<uint64_t> badStarts{400'000, 750'000};
  for (bool logging : {true, false}) {
    PeripheralEngine engine;
    engine.output = [&](PeripheralEngine::Port port, uint64_t now) -> uint64_t {
      switch (port) {
        case PeripheralEngine::Tx:
          // Two frames whose stop bits are low, around four valid frames.
          for (const uint64_t bad : badStarts)
            if (now >= bad && now < bad + 10 * kBit)
              return now >= bad + 9 * kBit ? false : serialLevel(now, bad, kBit, {0x5A});
          return serialLevel(now, 12'345, kBit, bytes)
              && serialLevel(now, 600'000, kBit, {0x41});
        case PeripheralEngine::Led: return 0;
        case PeripheralEngine::Anode: return 15;
        case PeripheralEngine::Segment: return 127;
        case PeripheralEngine::Dp: return 1;
        default: CHECK(false); return 0;
      }
    };
    BoardModel board(engine, binding(engine));
    board.setLogEnabled(true);
    CHECK(board.uartTxByteCycles().empty());
    CHECK(board.uartTxFramingErrorCycles().empty());
    board.tick(100'001);
    // Queued behind nothing, then chained; one sent during the stop tail.
    const uint64_t first = board.sendUart('a');
    const uint64_t second = board.sendUart('b');
    CHECK_EQ(first, 100'001);
    CHECK_EQ(second, 100'001 + 10 * kBit);
    board.tick(second + 9 * kBit + 17 - board.now());
    const uint64_t deferred = board.sendUart('c');
    CHECK_EQ(deferred, second + 10 * kBit);
    board.setLogEnabled(logging);
    board.tick(900'000 - board.now());

    const std::vector<uint8_t> expected{0x00, 0x96, 0xFF, 0x41};
    CHECK(board.uartTxBytes() == expected);
    CHECK_EQ(board.uartTxByteCycles().size(), expected.size());
    // Independent grid oracle: first grid at/after the start edge, then the
    // first grid at/after start + 9.5 bits.
    const auto stopSample = [](uint64_t start) {
      const uint64_t observed = (start + kGrid - 1) / kGrid * kGrid;
      return (observed + 9 * kBit + kBit / 2 + kGrid - 1) / kGrid * kGrid;
    };
    for (size_t i = 0; i < 3; ++i)
      CHECK_EQ(board.uartTxByteCycles()[i], stopSample(12'345 + i * 10 * kBit));
    CHECK_EQ(board.uartTxByteCycles()[3], stopSample(600'000));
    const std::vector<uint64_t> errors{stopSample(badStarts[0]), stopSample(badStarts[1])};
    CHECK(board.uartTxFramingErrorCycles() == errors);
    const auto& log = board.structuredLog();
    if (logging) {
      CHECK(board.uartTxByteCycles() == logStamps(log, "] UART TX 0x"));
      CHECK(logStamps(log, "UART TX framing error") == errors);
    } else {
      // Only events before logging was disabled were recorded.
      CHECK(logStamps(log, "UART TX framing error").empty());
    }
    const std::vector<uint64_t> starts = logging
        ? std::vector<uint64_t>{first, second, deferred} : std::vector<uint64_t>{first, second};
    CHECK(logStamps(log, "] UART RX 0x") == starts);
  }

  // An unbound receive line neither queues nor reports a start cycle.
  PeripheralEngine engine;
  BoardModel txOnly(engine, PinBinding::bind(
      parseXdc("set_property PACKAGE_PIN A18 [get_ports tx]\n"), engine));
  CHECK(txOnly.hasUartTx() && !txOnly.hasUartRx());
  CHECK_EQ(txOnly.sendUart('x'), BoardModel::kNoUartCycle);
  txOnly.tick(0);
  CHECK(engine.rxPokes.empty());
}

void uartMalformedFrames() {
  UartTxDecoder decoder;
  std::vector<uint8_t> received;
  const std::vector<uint8_t> byte{0x96};
  // A one-grid glitch is shorter than half a valid start bit. It must not
  // become a fabricated 0xFF, and the receiver must recover for a real byte.
  for (uint64_t now = kGrid; now < 350'000; now += kGrid) {
    const bool level = now == kGrid ? false : serialLevel(now, 200'000, kBit, byte);
    const auto result = decoder.sample(now, level);
    CHECK(!result.framingError);
    if (result.byte) received.push_back(*result.byte);
  }
  CHECK(received == byte);

  UartTxDecoder badStop;
  received.clear();
  unsigned errors = 0;
  for (uint64_t now = kGrid; now < 400'000; now += kGrid) {
    bool level = serialLevel(now, 2000, kBit, byte);
    if (now >= 2000 + 9 * kBit && now < 2000 + 10 * kBit) level = false;
    if (now >= 200'000) level = serialLevel(now, 200'000, kBit, byte);
    const auto result = badStop.sample(now, level);
    if (result.framingError) ++errors;
    if (result.byte) received.push_back(*result.byte);
  }
  CHECK_EQ(errors, 1);
  CHECK(received == byte);
}
}  // namespace

int main() {
  uartValidFrames();
  sevenSegmentBoundaries();
  boardPartitionsAndZeroTick();
  boardGridAndLogOrder();
  uartSameStampOrder();
  uartBoardStamps();
  std::puts("test_peripheral_contract: valid frames, persistence, board grid/log contracts PASS");
  uartMalformedFrames();
  std::puts("test_peripheral_contract: PASS");
}
