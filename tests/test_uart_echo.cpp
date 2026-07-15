// Milestone 1.5 acceptance: uart_echo round-trips "hello" at 9600 baud in
// turbo mode, byte-exact in the structured log — driven entirely through
// BoardModel (sendUartText queues bytes; RX bits are exact-cycle pokes; TX
// bytes are decoded on the observation grid).
//
// argv[1] = examples/uart_echo.xdc
// argv[2] = tests/golden/uart_echo.log
// argv[3] = "--regen" (optional)
#include "Vuart_echo.h"
#include "board/BoardModel.h"
#include "check.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using vb::BoardModel;
using vb::Button;
using vb::makeVerilatorEngine;
using vb::parseXdc;
using vb::PinBinding;

constexpr uint64_t kBit = vb::kUartCyclesPerBit;   // 10'417
constexpr uint64_t kFrame = 10 * kBit;             // 104'170 (start+8+stop)

static std::string readFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void runTo(BoardModel& b, uint64_t cycle) {
  CHECK(cycle >= b.now());
  b.tick(cycle - b.now());
}

static size_t countContaining(const std::vector<std::string>& lines,
                              const std::string& needle) {
  size_t n = 0;
  for (const auto& l : lines)
    if (l.find(needle) != std::string::npos) ++n;
  return n;
}

int main(int argc, char** argv) {
  CHECK(argc >= 3);
  const bool regen = argc > 3 && std::strcmp(argv[3], "--regen") == 0;

  auto engine = makeVerilatorEngine<Vuart_echo>({.topModule = "uart_echo"});
  const vb::XdcDoc xdc = parseXdc(readFile(argv[1]));
  CHECK_EQ(xdc.warnings.size(), 0);
  BoardModel board(*engine, PinBinding::bind(xdc, *engine));
  CHECK(board.hasUartTx() && board.hasUartRx());
  board.setLogEnabled(true);

  // R6: reset the design's synchronizers before use — the engine settles t=0
  // with all inputs low (2-state zero-init), so without a reset the RX
  // synchronizer would see a spurious start bit at power-on.
  board.setButton(Button::C, true);
  runTo(board, 1'000);
  board.setButton(Button::C, false);

  // Line idles high (BoardModel drove UART_RX high at construction).
  runTo(board, 100'000);
  board.sendUartText("hello");
  // RX frames chain back-to-back: start bits poked at exactly
  //   100'000 + k*104'170  for k=0..4  (each frame = 10 bits x 10'417).
  // The design detects each start 3 edges later (poke effective at N+1 plus
  // a 2FF synchronizer), samples bit centers from there, and echoes each
  // byte after its stop-bit center — TX bytes appear on the grid roughly one
  // frame after each RX start.
  runTo(board, 1'200'000);

  // --- acceptance: byte-exact round-trip ------------------------------------
  const std::vector<uint8_t> expected{'h', 'e', 'l', 'l', 'o'};
  CHECK_EQ(board.uartTxBytes().size(), 5);
  CHECK(board.uartTxBytes() == expected);

  const auto& log = board.structuredLog();
  // RX input events at their exact start-bit cycles.
  CHECK_EQ(countContaining(log, "[cycle 100000] UART RX 0x68 'h'"), 1);
  CHECK_EQ(countContaining(log, "[cycle 204170] UART RX 0x65 'e'"), 1);
  CHECK_EQ(countContaining(log, "[cycle 308340] UART RX 0x6C 'l'"), 1);
  CHECK_EQ(countContaining(log, "[cycle 412510] UART RX 0x6C 'l'"), 1);
  CHECK_EQ(countContaining(log, "[cycle 516680] UART RX 0x6F 'o'"), 1);
  // Each byte decoded exactly once on the TX side, no framing errors.
  CHECK_EQ(countContaining(log, "] UART TX 0x68 'h'"), 1);
  CHECK_EQ(countContaining(log, "] UART TX 0x65 'e'"), 1);
  CHECK_EQ(countContaining(log, "] UART TX 0x6C 'l'"), 2);
  CHECK_EQ(countContaining(log, "] UART TX 0x6F 'o'"), 1);
  CHECK_EQ(countContaining(log, "framing error"), 0);
  // led = tx_busy: the 1-cycle gaps between chained echo frames are shorter
  // than one observation chunk, so the log sees a single on/off pair — the
  // "observation record, not exhaustive" semantics doing exactly its job.
  CHECK_EQ(countContaining(log, "] LED[0] "), 2);
  CHECK_EQ(countContaining(log, "] BTNC "), 2);

  // --- golden comparison / regeneration --------------------------------------
  std::string actual;
  for (const auto& line : log) actual += line + '\n';
  if (regen) {
    std::ofstream out(argv[2], std::ios::binary);
    out << actual;
    CHECK(out.good());
    std::printf("test_uart_echo: REGENERATED %s (%zu lines)\n", argv[2], log.size());
  } else {
    const std::string golden = readFile(argv[2]);
    if (actual != golden) {
      std::fprintf(stderr, "golden mismatch (%zu vs %zu bytes)\n", actual.size(),
                   golden.size());
      return 1;
    }
  }

  // --- panel regressions (separate instance; not part of the golden) --------
  {
    auto engine2 = makeVerilatorEngine<Vuart_echo>({.topModule = "uart_echo"});
    const vb::XdcDoc xdc2 = parseXdc(readFile(argv[1]));
    BoardModel b2(*engine2, PinBinding::bind(xdc2, *engine2));
    b2.setButton(Button::C, true);
    runTo(b2, 1'000);
    b2.setButton(Button::C, false);
    runTo(b2, 100'000);

    // A send during the first frame's stop-bit tail must defer, not truncate:
    // both bytes echo, none silently dropped.
    b2.sendUart('a');
    runTo(b2, 195'000);  // inside 'a's stop bit (frame ends at 204'170)
    b2.sendUart('b');    // must start at 204'170, not 195'000
    runTo(b2, 600'000);
    const std::vector<uint8_t> both{'a', 'b'};
    CHECK(b2.uartTxBytes() == both);

    // Engine-bypass hardening: advancing the engine directly while RX edges
    // are pending must not hang the next tick (overdue edges apply late).
    b2.sendUart('c');
    engine2->step(5);  // bypasses BoardModel::tick — edges now overdue
    b2.tick(10);       // must return promptly
    runTo(b2, 900'000);
    CHECK_EQ(b2.uartTxBytes().size(), 3);  // 'c' still echoed
  }

  std::puts("test_uart_echo: PASS");
  return 0;
}
