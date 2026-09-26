// The scripted-stimulus parser, demo CLI, and the engine-free board pieces
// (seven-seg decode + decay boundary, UART decoder/driver): pure functions,
// no SDL, no engine.
#include "board/SevenSeg.h"
#include "board/Uart.h"
#include "check.h"
#include "script/RunOptions.h"
#include "script/Stimulus.h"

#include <vector>

static void checkUartPure() {
  // Decoder: two back-to-back 8N1 frames sampled on the 1000-cycle grid.
  // 0x4B is NOT a bit-reversal palindrome (reversed = 0xD2), so this pins
  // LSB-first order; the second frame starting the instant the first stop
  // bit ends pins the stop-sample position (a late stop sample would read
  // the next start bit low and report a framing error).
  {
    vb::UartTxDecoder dec;
    const auto frameLevel = [](uint64_t off, uint8_t byte) -> bool {
      if (off < vb::kUartCyclesPerBit) return false;     // start
      const uint64_t bit = off / vb::kUartCyclesPerBit;  // 1..8 = data
      if (bit >= 9) return true;                         // stop
      return (byte >> (bit - 1)) & 1;
    };
    const auto level = [&](uint64_t t) -> bool {
      if (t < 5000) return true;
      const uint64_t off = t - 5000;
      const uint64_t frame = 10 * vb::kUartCyclesPerBit;
      if (off < frame) return frameLevel(off, 0x4B);
      if (off < 2 * frame) return frameLevel(off - frame, 0x2C);
      return true;
    };
    std::vector<uint8_t> got;
    for (uint64_t t = 0; t <= 250'000; t += 1000) {
      const auto r = dec.sample(t, level(t));
      CHECK(!r.framingError);
      if (r.byte) got.push_back(*r.byte);
    }
    CHECK_EQ(got.size(), 2);
    CHECK_EQ(got[0], 0x4B);
    CHECK_EQ(got[1], 0x2C);
  }
  // Decoder: a low stop bit is a framing error, not a byte.
  {
    vb::UartTxDecoder dec;
    bool err = false;
    for (uint64_t t = 0; t <= 130'000; t += 1000) {
      // Line low the whole time after "start": stop bit reads low.
      const auto r = dec.sample(t, t < 5000);
      CHECK(!r.byte);
      if (r.framingError) err = true;
    }
    CHECK(err);
  }
  // Driver: edges for 'A' (0x41) queued at cycle 777, then a chained byte.
  {
    vb::UartRxDriver drv;
    CHECK_EQ(drv.nextEdgeCycle(), vb::UartRxDriver::kIdle);
    drv.send(0x41, 777);
    drv.send(0xFF, 777);  // chains at 777 + 10*bit
    CHECK_EQ(drv.nextEdgeCycle(), 777);
    const bool bits[10] = {false, /*0x41 LSB first:*/ true, false, false,
                           false, false, false,  true, false, /*stop*/ true};
    for (int k = 0; k < 10; ++k) {
      const uint64_t edge = 777 + k * vb::kUartCyclesPerBit;
      CHECK_EQ(drv.nextEdgeCycle(), edge);
      const auto e = drv.advance(edge);
      CHECK_EQ(e.level, bits[k]);
      CHECK_EQ(e.byteStart, k == 0);
      if (k == 0) CHECK_EQ(e.byte, 0x41);
    }
    CHECK_EQ(drv.nextEdgeCycle(), 777 + 10 * vb::kUartCyclesPerBit);  // next frame
    CHECK(!drv.idle());
  }
  // Driver: a send during the previous frame's stop-bit tail DEFERS to the
  // frame end instead of truncating the stop bit (panel regression: the
  // truncation silently dropped the previous byte end-to-end).
  {
    vb::UartRxDriver drv;
    drv.send(0x61, 100'000);
    for (int k = 0; k < 10; ++k)
      drv.advance(drv.nextEdgeCycle());  // consume 'a'; last edge at +9*bit
    CHECK(drv.idle());  // queue empty — but the line is busy until +10*bit
    drv.send(0x62, 195'000);  // inside the stop tail (ends at 204'170)
    CHECK_EQ(drv.nextEdgeCycle(), 100'000 + 10 * vb::kUartCyclesPerBit);
  }
}

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
  checkUartPure();
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
    const auto a = vb::parseRunArgs(11, const_cast<char**>(argv), "default.xdc");
    CHECK_EQ(a.errors.size(), 0);
    CHECK(a.xdcPath == "/x.xdc");
    CHECK_EQ(a.run.maxFrames, 90);
    CHECK(a.run.logPath == "out.log");
    CHECK_EQ(a.run.stimulus.size(), 3);
    CHECK(a.run.stimulus[0].name == "SW2" && a.run.stimulus[0].cycle == 0);
    CHECK(a.run.stimulus[1].name == "SW0" && a.run.stimulus[1].cycle == 0);
    CHECK(a.run.stimulus[2].name == "BTNC" && a.run.stimulus[2].cycle == 7);
  }
  // --switches keeps its argument position among --at events of its cycle.
  {
    const char* argv[] = {"demo", "--at", "0:SW0=0", "--switches", "1", "--at", "0:SW0=0"};
    const auto a = vb::parseRunArgs(7, const_cast<char**>(argv), "d.xdc");
    CHECK_EQ(a.errors.size(), 0);
    CHECK_EQ(a.run.stimulus.size(), 3);
    CHECK(!a.run.stimulus[0].value && a.run.stimulus[1].value && !a.run.stimulus[2].value);
  }
  // Default XDC used when --xdc absent; unknown flags are errors.
  {
    const char* argv[] = {"demo", "--bogus"};
    const auto a = vb::parseRunArgs(2, const_cast<char**>(argv), "d.xdc");
    CHECK(a.xdcPath == "d.xdc");
    CHECK_EQ(a.errors.size(), 1);
  }
  // --frames and --switches are validated (panel regression).
  {
    const char* argv[] = {"demo", "--frames", "abc", "--frames", "-7",
                          "--switches", "2x1z", "--switches", "10101010101010101"};
    const auto a = vb::parseRunArgs(9, const_cast<char**>(argv), "d.xdc");
    CHECK_EQ(a.errors.size(), 4);
    CHECK_EQ(a.run.maxFrames, -1);  // untouched by the bad values
  }
  // --send parses CYCLE:TEXT and sorts by cycle; bad forms are errors.
  {
    const char* argv[] = {"demo", "--send", "5000:hi there", "--send", "100:x",
                          "--send", "nocolon", "--send", "12:"};
    const auto a = vb::parseRunArgs(9, const_cast<char**>(argv), "d.xdc");
    CHECK_EQ(a.errors.size(), 2);
    CHECK_EQ(a.run.sends.size(), 2);
    CHECK(a.run.sends[0].cycle == 100 && a.run.sends[0].text == "x");
    CHECK(a.run.sends[1].cycle == 5000 && a.run.sends[1].text == "hi there");
  }

  std::puts("test_script: PASS");
  return 0;
}
