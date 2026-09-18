// Milestone 1.6 acceptance: vga_pattern produces a pixel-exact frame-1 dump.
// The expected image is generated PROCEDURALLY from the same bar+border spec
// as the RTL (reviewable code, no opaque binary golden), and an independent
// hand-computed pixel (the yellow bar center = R=0xFF,G=0xFF,B=0x00) is
// asserted DIRECTLY on the assembler output so a generator bug can't validate
// a matching assembler bug.
//
// argv[1] = examples/vga_pattern.xdc
// argv[2] = output BMP path (docs artifact; not a committed golden)
#include "Vvga_pattern.h"
#include "board/Bmp.h"
#include "board/BoardModel.h"
#include "board/Vga.h"
#include "check.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using vb::BoardModel;
using vb::Button;
using vb::makeVerilatorEngine;
using vb::parseXdc;
using vb::PinBinding;
using vb::VgaFrameAssembler;

static constexpr uint32_t W = VgaFrameAssembler::kWidth;   // 640
static constexpr uint32_t H = VgaFrameAssembler::kHeight;  // 480

static std::string readFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// The RTL's pattern, in reviewable code. 8 vertical 80px bars, white sides,
// white top row, blue bottom row (override order matches vga_pattern.v).
static void expectedPixel(uint32_t row, uint32_t col, uint8_t& r, uint8_t& g, uint8_t& b) {
  static const uint16_t bars[8] = {0xFFF, 0xFF0, 0x0FF, 0x0F0,
                                   0xF0F, 0xF00, 0x00F, 0x000};
  uint16_t rgb = bars[col / 80];
  if (col == 0 || col == W - 1) rgb = 0xFFF;  // white sides
  if (row == 0) rgb = 0xFFF;                  // white top
  if (row == H - 1) rgb = 0x00F;              // blue bottom
  r = static_cast<uint8_t>(((rgb >> 8) & 0xF) * 17);
  g = static_cast<uint8_t>(((rgb >> 4) & 0xF) * 17);
  b = static_cast<uint8_t>((rgb & 0xF) * 17);
}

static std::vector<uint8_t> expectedFrame() {
  std::vector<uint8_t> img(static_cast<size_t>(W) * H * 3);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      const size_t idx = (static_cast<size_t>(y) * W + x) * 3;
      expectedPixel(y, x, img[idx], img[idx + 1], img[idx + 2]);
    }
  return img;
}

int main(int argc, char** argv) {
  CHECK(argc >= 3);
  auto engine = makeVerilatorEngine<Vvga_pattern>({.topModule = "vga_pattern"});
  const vb::XdcDoc xdc = parseXdc(readFile(argv[1]));
  CHECK_EQ(xdc.warnings.size(), 0);
  BoardModel board(*engine, PinBinding::bind(xdc, *engine));
  CHECK(board.hasVga());

  // Pinned reset: hold btnC for 10 cycles, release at cycle 10. Counting
  // starts from the release; the frame-completion stamps below are measured
  // relative to it and asserted exactly (the determinism anchor).
  board.setButton(Button::C, true);
  board.tick(10);
  board.setButton(Button::C, false);

  // Run past the 3rd vsync falling edge so frame 1 (2nd completed period,
  // edges 2->3) is available. Reset adds 11 cycles to the perf-lens baseline
  // (falls at 1,568,000 / 3,248,000 / 4,928,000 from counting-start).
  board.tick(5'050'000 - board.now());

  std::printf("frames=%llu last-frame-cycle=%llu cpp=%u ok=%d status='%s'\n",
              (unsigned long long)board.vgaCompletedFrames(),
              (unsigned long long)board.vgaLastFrameCycle(), board.vgaCyclesPerPixel(),
              board.vgaOk() ? 1 : 0, board.vgaStatus().c_str());

  CHECK(board.vgaCompletedFrames() >= 2);  // frame 1 available
  CHECK(board.vgaOk());
  CHECK_EQ(board.vgaCyclesPerPixel(), 4);  // measured /4 divider, not assumed
  // Determinism anchor: frame 1 completes at the 3rd vsync falling edge. Pure
  // counting-start gives 4,928,000 (490 lines x 3200 + 2 x 1,680,000). The
  // +13 = 10 (btnC reset hold) + 4 (CE_DIV pixel-enable fill before the first
  // pix_ce) - 1 (stepCapture labels out[i] at segStart+i, the post-edge state
  // of now()=segStart+i+1).
  CHECK_EQ(board.vgaLastFrameCycle(), 4'928'013);

  const std::vector<uint8_t>& got = board.vgaFramebuffer();
  CHECK_EQ(got.size(), static_cast<size_t>(W) * H * 3);

  // Independent hand-anchored checks (NOT via the generator), asserted
  // directly on the assembler output so a matching bug in both the RTL and
  // the generator can't hide:
  //  - yellow bar center (row 240, col 120) = R=FF,G=FF,B=00 pins all three
  //    channels; yellow's distinct R and B catch an R<->B swap.
  //  - the border rows pin vertical ORIENTATION: top must be white, bottom
  //    must be blue (a flip present in both RTL and generator is still caught
  //    by these absolute constants).
  const size_t yellow = (static_cast<size_t>(240) * W + 120) * 3;
  CHECK_EQ(got[yellow + 0], 0xFF);
  CHECK_EQ(got[yellow + 1], 0xFF);
  CHECK_EQ(got[yellow + 2], 0x00);
  const size_t topMid = (static_cast<size_t>(0) * W + 320) * 3;
  CHECK(got[topMid + 0] == 0xFF && got[topMid + 1] == 0xFF && got[topMid + 2] == 0xFF);
  const size_t botMid = (static_cast<size_t>(H - 1) * W + 320) * 3;
  CHECK(got[botMid + 0] == 0x00 && got[botMid + 1] == 0x00 && got[botMid + 2] == 0xFF);

  // Full pixel-exact comparison against the procedural expected frame.
  const std::vector<uint8_t> want = expectedFrame();
  if (got != want) {
    for (size_t i = 0; i + 2 < got.size(); i += 3)
      if (got[i] != want[i] || got[i + 1] != want[i + 1] || got[i + 2] != want[i + 2]) {
        const size_t px = i / 3;
        std::fprintf(stderr,
                     "first mismatch at (row %zu, col %zu): got %02X%02X%02X "
                     "want %02X%02X%02X\n",
                     px / W, px % W, got[i], got[i + 1], got[i + 2], want[i],
                     want[i + 1], want[i + 2]);
        break;
      }
    vb::writeBmpRGB888(argv[2], got, W, H);  // dump for inspection
    return 1;
  }

  CHECK(vb::writeBmpRGB888(argv[2], got, W, H));

  // Prove the hand-anchored checks above actually BITE: inject each bug into
  // the real assembler output and confirm the exact assertions we run on
  // `got` would fail. (This exercises the assembler output, unlike a
  // reference-vs-itself check.)
  {
    std::vector<uint8_t> swapped = got;
    for (size_t i = 0; i + 2 < swapped.size(); i += 3)
      std::swap(swapped[i], swapped[i + 2]);  // R<->B
    // The yellow canary would fire: R is no longer 0xFF (it's the swapped B=0).
    CHECK(swapped[yellow + 0] != 0xFF);
    CHECK_EQ(swapped[yellow + 2], 0xFF);

    std::vector<uint8_t> flipped(got.size());
    for (uint32_t y = 0; y < H; ++y)
      std::copy_n(&got[(static_cast<size_t>(H - 1 - y) * W) * 3], W * 3,
                  &flipped[(static_cast<size_t>(y) * W) * 3]);
    // The top-row anchor would fire: row 0 is now the blue bottom, not white.
    CHECK(!(flipped[topMid + 0] == 0xFF && flipped[topMid + 1] == 0xFF &&
            flipped[topMid + 2] == 0xFF));
  }

  // Actual Verilator + BoardModel partition invariance, not only a monitor
  // fed already captured samples. Coprime tick boundaries must preserve
  // post-edge capture, completed-frame index/stamp and the entire image.
  auto splitEngine = makeVerilatorEngine<Vvga_pattern>({.topModule = "vga_pattern"});
  BoardModel split(*splitEngine, PinBinding::bind(xdc, *splitEngine));
  split.setButton(Button::C, true);
  split.tick(3);
  split.tick(7);
  split.setButton(Button::C, false);
  const uint64_t chunks[] = {1, 997, 3201, 19, 65537};
  size_t part = 0;
  while (split.now() < board.now())
    split.tick(std::min(chunks[part++ % 5], board.now() - split.now()));
  CHECK_EQ(split.now(), board.now());
  CHECK_EQ(split.vgaCompletedFrames(), board.vgaCompletedFrames());
  CHECK_EQ(split.vgaLastFrameCycle(), board.vgaLastFrameCycle());
  CHECK_EQ(split.vgaCyclesPerPixel(), board.vgaCyclesPerPixel());
  CHECK(split.vgaStatus() == board.vgaStatus());
  CHECK(split.vgaFramebuffer() == got);

  std::puts("test_vga_pattern: PASS");
  return 0;
}
