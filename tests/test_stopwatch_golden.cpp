// Milestone 1.4 acceptance: the stopwatch structured log matches the golden
// for 3 simulated seconds (300,000,000 cycles), including correct digit-mux
// behavior at the ~1 kHz refresh rate — all observed through BoardModel, the
// same entry points the GUI uses.
//
// argv[1] = examples/stopwatch.xdc
// argv[2] = tests/golden/stopwatch_3s.log
// argv[3] = "--regen" (optional): write the golden instead of comparing.
//           Regens must be hand-verified against the derivations below and
//           reviewed via the category-count assertions, which always run.
#include "Vstopwatch.h"
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

// Mirrored from stopwatch.v — the golden arithmetic depends on these.
constexpr uint64_t kDebounce = 1'000'000;
constexpr uint64_t kCenti = 1'000'000;
constexpr uint64_t kDwell = 25'000;
// How stale the fused display can be: a BCD change becomes visible when its
// digit is next strobed (worst case one full rotation, 4*kDwell) AND the next
// grid crossing samples it.
constexpr uint64_t kDisplayLatency = 4 * kDwell + BoardModel::kSampleChunkCycles;

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

  auto engine = makeVerilatorEngine<Vstopwatch>({.topModule = "stopwatch"});
  const vb::XdcDoc xdc = parseXdc(readFile(argv[1]));
  CHECK_EQ(xdc.warnings.size(), 0);
  BoardModel board(*engine, PinBinding::bind(xdc, *engine));
  board.setLogEnabled(true);

  // ---- scripted timeline (documented derivations) --------------------------
  // Poke at cycle P -> the design first samples it at edge P+1. Debounce
  // recurrence (2 sync FFs + counter + toggle FF):
  //   btnU_ff2 flips after edge P+2; the disagreement counter starts at edge
  //   P+3 and hits DEBOUNCE-1 pre-edge at P+DEBOUNCE+2, so btnU_db flips at
  //   edge P+DEBOUNCE+2 and `running` toggles one edge later:
  //   running flips at edge P + DEBOUNCE + 3.

  // A 500k-cycle tap: shorter than DEBOUNCE, so it must be swallowed — the
  // regression for "an instantaneous pulse would be debounced away".
  runTo(board, 100'000);
  board.setButton(Button::U, true);
  runTo(board, 600'000);
  board.setButton(Button::U, false);

  runTo(board, 1'700'000);
  CHECK(!board.ledState(0));  // the tap must NOT have started the watch
  // Display shows 00.00 by now (all four digits strobed within the first
  // rotation; content latched at grid crossings 1000/25000/50000/75000).
  for (uint32_t i = 0; i < 4; ++i) CHECK_EQ(board.digitChar(i), '0');
  CHECK(board.digitDp(2));   // the SS.CC point
  CHECK(!board.digitDp(0) && !board.digitDp(1) && !board.digitDp(3));
  // Persistence of vision: digit 2 is not being strobed at this instant
  // (cycle 1.7M is inside digit 0's window) yet its fused content is lit.
  CHECK(board.digitLastLit(2) < board.now());
  CHECK(board.digitSegments(2) != 0);

  // The real press: held 2M cycles >= DEBOUNCE + sync margin.
  // running rises at edge 2'000'000 + 1'000'003 = 3'000'003; led[0] follows
  // combinationally and is observed at the next grid crossing, 3'001'000.
  // First centisecond tick: running-start + kCenti = 4'000'003, digit 0
  // content observed '0'->'1' at 4'001'000 (cycle 4'000'003 lies inside
  // digit 0's anode window; subsequent ticks land there too, so SSEG[0]
  // lines are spaced exactly 1'000'000 apart).
  runTo(board, 2'000'000);
  board.setButton(Button::U, true);
  runTo(board, 4'000'000);
  board.setButton(Button::U, false);

  // Run 1: centi ticks at 3'000'003 + k*1'000'000 for k=1..151 (k=152 would
  // land at 155'000'003, but reset is asserted from edge 155'000'001).
  // Display at reset time: 151 centis = 01.51.
  runTo(board, 155'000'000);
  CHECK_EQ(board.digitChar(0), '1');
  CHECK_EQ(board.digitChar(1), '5');
  CHECK_EQ(board.digitChar(2), '1');
  CHECK_EQ(board.digitChar(3), '0');
  CHECK(board.ledState(0));

  // Reset: 500k-cycle btnC hold — long enough to observe (time clears, watch
  // stops, mux parks on digit 0), short enough that digits 1-3 stay inside
  // the 2M persistence window: NO decay-blank lines enter the golden.
  board.setButton(Button::C, true);
  runTo(board, 155'500'000);
  board.setButton(Button::C, false);

  // Restart: running rises at 157'000'000 + 1'000'003 = 158'000'003.
  runTo(board, 157'000'000);
  board.setButton(Button::U, true);
  runTo(board, 159'000'000);
  board.setButton(Button::U, false);

  // Run 2: centi ticks at 158'000'003 + k*1'000'000 for k=1..141.
  // 3 simulated seconds exactly:
  runTo(board, 300'000'000);
  CHECK_EQ(board.now(), 300'000'000);

  // Final display: 141 centis = 01.41, still running.
  CHECK_EQ(board.digitChar(0), '1');
  CHECK_EQ(board.digitChar(1), '4');
  CHECK_EQ(board.digitChar(2), '1');
  CHECK_EQ(board.digitChar(3), '0');
  CHECK(board.ledState(0));
  CHECK(board.digitDp(2));

  // Digit-mux correctness at the ~1 kHz refresh: every digit was strobed
  // within one rotation + one chunk of "now" — a mux that stopped rotating,
  // rotated grossly slow, or parked would fail this hard bound.
  for (uint32_t i = 0; i < 4; ++i) {
    CHECK(board.digitLastLit(i) != vb::SevenSeg::kNeverLit);
    CHECK(board.now() - board.digitLastLit(i) <= kDisplayLatency);
  }

  // ---- category counts (keep golden regens reviewable) ---------------------
  // SSEG[0]: initial ' '->'0' + 151 (run 1) + reset '1'->'0' + 141 (run 2)
  const auto& log = board.structuredLog();
  CHECK_EQ(countContaining(log, "] SSEG[0] '"), 1 + 151 + 1 + 141);
  // SSEG[1]: initial + 15 wraps (k=10..150) + reset '5'->'0' + 14 (k=10..140)
  CHECK_EQ(countContaining(log, "] SSEG[1] '"), 1 + 15 + 1 + 14);
  // SSEG[2]: initial + '0'->'1' (k=100) + reset '1'->'0' + '0'->'1' (k=100)
  CHECK_EQ(countContaining(log, "] SSEG[2] '"), 1 + 1 + 1 + 1);
  // SSEG[3]: initial ' '->'0' only (s1 never advances in 3 s)
  CHECK_EQ(countContaining(log, "] SSEG[3] '"), 1);
  CHECK_EQ(countContaining(log, "].dp "), 1);  // digit 2's point, once
  CHECK_EQ(countContaining(log, "] LED[0] "), 3);  // start, reset, restart
  CHECK_EQ(countContaining(log, "] BTNU "), 6);
  CHECK_EQ(countContaining(log, "] BTNC "), 2);
  // Spot-check the exact-cycle input stamps and derived output stamps.
  CHECK_EQ(countContaining(log, "[cycle 2000000] BTNU 0->1"), 1);
  CHECK_EQ(countContaining(log, "[cycle 3001000] LED[0] 0->1"), 1);
  CHECK_EQ(countContaining(log, "[cycle 4001000] SSEG[0] '0'->'1'"), 1);

  // ---- golden comparison / regeneration -------------------------------------
  std::string actual;
  for (const auto& line : log) actual += line + '\n';
  if (regen) {
    std::ofstream out(argv[2], std::ios::binary);
    out << actual;
    CHECK(out.good());
    std::printf("test_stopwatch_golden: REGENERATED %s (%zu lines)\n", argv[2],
                log.size());
  } else {
    const std::string golden = readFile(argv[2]);
    if (actual != golden) {
      std::istringstream a(actual), g(golden);
      std::string la, lg;
      size_t n = 0;
      while (true) {
        const bool ha = static_cast<bool>(std::getline(a, la));
        const bool hg = static_cast<bool>(std::getline(g, lg));
        ++n;
        if (!ha && !hg) break;
        if (la != lg || ha != hg) {
          std::fprintf(stderr, "golden mismatch at line %zu:\n  actual: %s\n  golden: %s\n",
                       n, ha ? la.c_str() : "<eof>", hg ? lg.c_str() : "<eof>");
          return 1;
        }
      }
      std::fprintf(stderr, "golden mismatch (length)\n");
      return 1;
    }
  }

  // ---- decay / persistence coverage (separate instance; NOT in the golden,
  // so retuning kPersistCycles never churns the hand-verified file) ----------
  {
    auto engine2 = makeVerilatorEngine<Vstopwatch>({.topModule = "stopwatch"});
    const vb::XdcDoc xdc2 = parseXdc(readFile(argv[1]));
    BoardModel b2(*engine2, PinBinding::bind(xdc2, *engine2));
    runTo(b2, 1'000'000);  // all four digits lit '0'
    for (uint32_t i = 0; i < 4; ++i) CHECK_EQ(b2.digitChar(i), '0');
    // Hold reset: the mux parks on digit 0, so digits 1-3 stop being strobed
    // and must decay dark once the persistence window expires...
    b2.setButton(Button::C, true);
    runTo(b2, 1'000'000 + vb::SevenSeg::kPersistCycles + 300'000);
    CHECK_EQ(b2.digitChar(0), '0');  // parked digit still strobed
    for (uint32_t i = 1; i < 4; ++i) CHECK_EQ(b2.digitSegments(i), 0);
    // ...and relight within two rotations of releasing it.
    b2.setButton(Button::C, false);
    b2.tick(200'000);
    for (uint32_t i = 0; i < 4; ++i) CHECK_EQ(b2.digitChar(i), '0');
  }

  std::puts("test_stopwatch_golden: PASS");
  return 0;
}
