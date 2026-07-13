// BoardModel: the board-level seam the GUI renders from. This is also the
// headless statement of the milestone 1.3 acceptance criterion — a switch
// click must be visible in the LEDs within one GUI frame (setSwitch -> one
// tick -> ledState reflects it).
//
// argv[1] = examples/counter.xdc
#include "Vcounter.h"
#include "board/BoardModel.h"
#include "check.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"

#include <fstream>
#include <sstream>
#include <string>

using vb::BoardModel;
using vb::makeVerilatorEngine;
using vb::parseXdc;
using vb::PinBinding;

static std::string readFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static uint64_t ledValue(const BoardModel& b) {
  uint64_t v = 0;
  for (uint32_t i = 0; i < BoardModel::kLedCount; ++i)
    if (b.ledState(i)) v |= 1ull << i;
  return v;
}

int main(int argc, char** argv) {
  CHECK(argc >= 2);
  auto engine = makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  const vb::XdcDoc doc = parseXdc(readFile(argv[1]));
  BoardModel board(*engine, PinBinding::bind(doc, *engine));

  // counter.xdc binds sw[3:0] and led[15:0]: the virtual board has exactly
  // those resources; the rest don't exist for this design.
  for (uint32_t i = 0; i < 4; ++i) CHECK(board.hasSwitch(i));
  for (uint32_t i = 4; i < 16; ++i) CHECK(!board.hasSwitch(i));
  for (uint32_t i = 0; i < 16; ++i) CHECK(board.hasLed(i));

  // t=0: no edges, LEDs dark.
  CHECK_EQ(board.now(), 0);
  CHECK_EQ(ledValue(board), 0);

  // The acceptance criterion, headless: flip switches, advance ONE frame,
  // LEDs show it. sw=3 -> count += 3 per edge.
  board.setSwitch(0, true);
  board.setSwitch(1, true);
  CHECK(board.switchState(0) && board.switchState(1));
  board.tick(1);
  CHECK_EQ(ledValue(board), 3);
  CHECK(board.ledState(0) && board.ledState(1) && !board.ledState(2));

  // A GUI-sized frame: fixed cycle count, deterministic result regardless of
  // wall clock (R1). count = 3 * 100001 mod 2^16 after this frame.
  board.tick(100'000);
  CHECK_EQ(board.now(), 100'001);
  CHECK_EQ(ledValue(board), (3ull * 100'001) & 0xFFFF);

  // Changing a switch mid-run is visible after the next frame tick.
  const uint64_t before = ledValue(board);
  board.setSwitch(2, true);  // sw: 3 -> 7
  board.tick(1);
  CHECK_EQ(ledValue(board), (before + 7) & 0xFFFF);

  // And OFF again — the other half of every checkbox click.
  board.setSwitch(2, false);  // sw: 7 -> 3
  CHECK(!board.switchState(2));
  board.tick(1);
  CHECK_EQ(ledValue(board), (before + 7 + 3) & 0xFFFF);

  // Switches the design doesn't have: ignored, never throw, read as off.
  board.setSwitch(9, true);
  CHECK(!board.switchState(9));
  board.setSwitch(99, true);  // out of range entirely
  CHECK(!board.ledState(99));

  std::puts("test_board_model: PASS");
  return 0;
}
