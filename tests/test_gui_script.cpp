// The scripted-stimulus parser, demo CLI, and the engine-free board pieces
// (seven-seg decode + decay boundary): pure functions, no SDL, no engine.
#include "board/SevenSeg.h"
#include "check.h"
#include "gui/GuiApp.h"
#include "gui/Stimulus.h"

static void checkSevenSegPure() {
  // Decode table: every mapped pattern, plus '?' for garbage.
  struct Row { uint8_t lit; char c; };
  constexpr Row rows[] = {
      {0x00, ' '}, {0x3F, '0'}, {0x06, '1'}, {0x5B, '2'}, {0x4F, '3'},
      {0x66, '4'}, {0x6D, '5'}, {0x7D, '6'}, {0x07, '7'}, {0x7F, '8'},
      {0x6F, '9'}, {0x77, 'A'}, {0x7C, 'b'}, {0x39, 'C'}, {0x5E, 'd'},
      {0x79, 'E'}, {0x71, 'F'}, {0x40, '-'},
  };
  for (const Row& r : rows) CHECK_EQ(vb::decodeSevenSeg(r.lit), r.c);
  CHECK_EQ(vb::decodeSevenSeg(0x24), '?');  // raw active-low '2' — polarity trap

  // Decay boundary is INCLUSIVE (see SevenSeg.h): lit at exactly
  // lastLit + kPersistCycles, dark one cycle later.
  vb::SevenSeg ss;
  ss.sample(/*now=*/1000, /*anBits=*/0b1110, /*segBits=*/~uint8_t{0x3F}, /*dp=*/true);
  CHECK_EQ(ss.segments(1000, 0), 0x3F);
  CHECK_EQ(ss.segments(1000 + vb::SevenSeg::kPersistCycles, 0), 0x3F);
  CHECK_EQ(ss.segments(1001 + vb::SevenSeg::kPersistCycles, 0), 0);
  CHECK_EQ(ss.lastLit(1), vb::SevenSeg::kNeverLit);  // digit 1 never strobed
  CHECK_EQ(ss.segments(1000, 1), 0);
}

int main() {
  checkSevenSegPure();
  // Valid forms parse and sort by cycle (stable).
  {
    const auto p = vb::parseStimulus(
        {"2000000:BTNU=1", "0:SW3=1", "100:SW15=0", "4000000:BTNU=0"});
    CHECK_EQ(p.errors.size(), 0);
    CHECK_EQ(p.events.size(), 4);
    CHECK(p.events[0].name == "SW3" && p.events[0].cycle == 0 && p.events[0].value);
    CHECK(p.events[1].name == "SW15" && p.events[1].cycle == 100 && !p.events[1].value);
    CHECK(p.events[2].name == "BTNU" && p.events[2].cycle == 2000000);
    CHECK(p.events[3].cycle == 4000000 && !p.events[3].value);
  }
  // Same-cycle events keep argv order, so last-wins-per-name holds at apply
  // time.
  {
    const auto p = vb::parseStimulus({"5:SW0=1", "5:SW0=0"});
    CHECK_EQ(p.events.size(), 2);
    CHECK(p.events[0].value && !p.events[1].value);
  }
  // Malformed entries are individual errors; nothing partial is kept.
  {
    const auto p = vb::parseStimulus({"nocolon", "10:SW1", "x:SW1=1", "10:SW99=1",
                                      "10:BTNX=1", "10:SW1=2"});
    CHECK_EQ(p.events.size(), 0);
    CHECK_EQ(p.errors.size(), 6);
  }
  // Demo CLI: flags, --switches sugar (rightmost char = SW0), --at merge.
  {
    const char* argv[] = {"demo", "--xdc",      "/x.xdc", "--frames", "90",
                          "--log", "out.log",   "--switches", "0101",
                          "--at",  "7:BTNC=1"};
    const auto a = vb::parseDemoArgs(11, const_cast<char**>(argv), "default.xdc");
    CHECK_EQ(a.errors.size(), 0);
    CHECK(a.xdcPath == "/x.xdc");
    CHECK_EQ(a.gui.maxFrames, 90);
    CHECK(a.gui.logPath == "out.log");
    CHECK_EQ(a.gui.stimulus.size(), 3);
    CHECK(a.gui.stimulus[0].name == "SW2" && a.gui.stimulus[0].cycle == 0);
    CHECK(a.gui.stimulus[1].name == "SW0" && a.gui.stimulus[1].cycle == 0);
    CHECK(a.gui.stimulus[2].name == "BTNC" && a.gui.stimulus[2].cycle == 7);
  }
  // Default XDC used when --xdc absent; unknown flags are errors.
  {
    const char* argv[] = {"demo", "--bogus"};
    const auto a = vb::parseDemoArgs(2, const_cast<char**>(argv), "d.xdc");
    CHECK(a.xdcPath == "d.xdc");
    CHECK_EQ(a.errors.size(), 1);
  }

  std::puts("test_gui_script: PASS");
  return 0;
}
