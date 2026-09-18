// Independent monitor-contract tests: synthetic syncs use absolute raster
// coordinates, without the shipped RTL or the assembler's porch constants.
// Intermediate color levels distinguish every physical color bit; all tests
// are headless and require no concrete SimEngine implementation.
#include "board/Vga.h"
#include "check.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using Monitor = vb::VgaFrameAssembler;
constexpr uint64_t kRasterPixels = 800 * 525;
constexpr Monitor::Lanes kPacked{
    .hsync = 12, .vsync = 13,
    .red = {8, 9, 10, 11}, .green = {4, 5, 6, 7}, .blue = {0, 1, 2, 3}};
// A sparse, reordered layout spanning the complete uint64_t word. In
// particular, the highest bit must remain a usable lane, not a sign bit.
constexpr Monitor::Lanes kScrambled{
    .hsync = 31, .vsync = 0,
    .red = {63, 5, 42, 17}, .green = {1, 60, 9, 35}, .blue = {22, 4, 53, 11}};

uint64_t packColor(uint16_t color, const Monitor::Lanes& lanes) {
  uint64_t word = 0;
  for (unsigned bit = 0; bit < 4; ++bit) {
    word |= uint64_t((color >> (8 + bit)) & 1) << lanes.red[bit];
    word |= uint64_t((color >> (4 + bit)) & 1) << lanes.green[bit];
    word |= uint64_t((color >> bit) & 1) << lanes.blue[bit];
  }
  return word;
}

std::vector<uint64_t> waveform(unsigned cpp, const Monitor::Lanes& lanes,
                               bool invertH = false, bool invertV = false,
                               bool syncOrigin = false) {
  std::vector<uint64_t> samples(3 * kRasterPixels * cpp);
  for (uint64_t t = 0; t < samples.size(); ++t) {
    // The second representation puts both falling edges at raster (0,0),
    // with visible data after sync/backporch. Both are valid 800x525 timing.
    const unsigned x = ((t / cpp) % 800 + (syncOrigin ? 656 : 0)) % 800;
    const unsigned y = (((t / cpp) / 800) % 525 + (syncOrigin ? 490 : 0)) % 525;
    uint16_t color = 0;  // blanking is black
    if (x < 640 && y < 480) {
      color = static_cast<uint16_t>(((y & 15) << 8) | ((x & 15) << 4) |
                                    ((x / 16) & 15));
      if (y == 0) color = 0xFFF;
      if (y == 479) color = 0x00F;
    }
    const bool hs = !(x >= 656 && x < 752);
    const bool vs = !(y >= 490 && y < 492);
    samples[t] = packColor(color, lanes) |
                 (uint64_t(hs != invertH) << lanes.hsync) |
                 (uint64_t(vs != invertV) << lanes.vsync);
  }
  return samples;
}

void feed(Monitor& monitor, const std::vector<uint64_t>& samples,
          size_t begin, size_t end, bool segmented = true, uint64_t origin = 0) {
  // Coprime chunks, tiny chunks, and chunks straddling scanline/grid edges.
  constexpr std::array<uint32_t, 9> chunks{1, 997, 2, 3199, 7, 1000, 5, 799, 4093};
  size_t part = 0;
  for (size_t pos = begin; pos < end; ++part) {
    const size_t requested = segmented ? chunks[part % chunks.size()] : end - pos;
    const auto count = static_cast<uint32_t>(std::min(requested, end - pos));
    monitor.consume(origin + pos, samples.data() + pos, count);
    pos += count;
  }
}

void checkImage(const Monitor& monitor) {
  const auto& rgb = monitor.framebuffer();
  CHECK_EQ(rgb.size(), 640 * 480 * 3);
  // Absolute anchors are independent of the whole-image oracle below.
  const size_t anchor = (18 * 640 + 115) * 3;
  CHECK_EQ(rgb[anchor], 0x22);
  CHECK_EQ(rgb[anchor + 1], 0x33);
  CHECK_EQ(rgb[anchor + 2], 0x77);
  CHECK_EQ(rgb[0], 0xFF);
  CHECK_EQ(rgb[1], 0xFF);
  CHECK_EQ(rgb[2], 0xFF);
  const size_t bottom = 479 * 640 * 3;
  CHECK_EQ(rgb[bottom], 0);
  CHECK_EQ(rgb[bottom + 1], 0);
  CHECK_EQ(rgb[bottom + 2], 0xFF);
  for (unsigned y = 0; y < 480; ++y) {
    for (unsigned x = 0; x < 640; ++x) {
      const size_t pos = (y * 640 + x) * 3;
      const unsigned red = y == 0 ? 255 : y == 479 ? 0 : (y % 16) * 17;
      const unsigned green = y == 0 ? 255 : y == 479 ? 0 : (x % 16) * 17;
      const unsigned blue = y == 0 || y == 479 ? 255 : ((x / 16) % 16) * 17;
      CHECK_EQ(rgb[pos], red);
      CHECK_EQ(rgb[pos + 1], green);
      CHECK_EQ(rgb[pos + 2], blue);
    }
  }
}

void checkRatesLanesAndChunks() {
  CHECK_EQ(Monitor::kWidth, 640);
  CHECK_EQ(Monitor::kHeight, 480);
  for (unsigned cpp : {1u, 2u, 3u, 4u, 5u}) {
    for (const auto& lanes : {kPacked, kScrambled}) {
      const auto samples = waveform(cpp, lanes);
      Monitor whole(lanes), segmented(lanes);
      // Absolute origin need not be aligned with any porch, line, or grid.
      feed(whole, samples, 0, samples.size(), false, 17);
      feed(segmented, samples, 0, samples.size(), true, 17);
      CHECK(whole.ok());
      CHECK(segmented.ok());
      CHECK_EQ(whole.completedFrames(), 2);
      CHECK_EQ(whole.measuredCyclesPerPixel(), cpp);
      CHECK_EQ(whole.lastFrameCycle(), 17 + (2 * 525 + 490) * 800 * cpp);
      CHECK_EQ(segmented.completedFrames(), whole.completedFrames());
      CHECK_EQ(segmented.lastFrameCycle(), whole.lastFrameCycle());
      CHECK(segmented.status() == whole.status());
      CHECK(segmented.framebuffer() == whole.framebuffer());
      checkImage(whole);
    }
  }
}

void checkCompletionBoundary() {
  constexpr unsigned cpp = 3;
  const auto samples = waveform(cpp, kPacked);
  Monitor monitor(kPacked);
  const size_t secondFall = (525 + 490) * 800 * cpp;
  feed(monitor, samples, 0, secondFall);
  CHECK_EQ(monitor.completedFrames(), 0);
  // Empty consume is a no-op even exactly at a frame boundary.
  monitor.consume(secondFall, nullptr, 0);
  CHECK_EQ(monitor.completedFrames(), 0);
  feed(monitor, samples, secondFall, secondFall + 1);
  CHECK_EQ(monitor.completedFrames(), 1);
  CHECK_EQ(monitor.lastFrameCycle(), secondFall);
  CHECK(monitor.ok());
  checkImage(monitor);
}

void checkCoincidentSyncOrigin() {
  for (unsigned cpp : {1u, 3u, 4u}) {
    const auto samples = waveform(cpp, kScrambled, false, false, true);
    Monitor monitor(kScrambled);
    feed(monitor, samples, 0, samples.size());
    CHECK(monitor.ok());
    CHECK_EQ(monitor.completedFrames(), 2);
    CHECK_EQ(monitor.lastFrameCycle(), 2 * kRasterPixels * cpp);
    checkImage(monitor);
  }
}

void checkMalformedAndRecovery() {
  constexpr unsigned cpp = 4;
  const size_t twoRasters = 2 * kRasterPixels * cpp;
  // One horizontal falling edge delayed by a single master cycle creates
  // nonintegral line periods. The next clean frame must clear the diagnosis.
  {
    auto samples = waveform(cpp, kPacked);
    const size_t edge = ((525 + 100) * 800 + 656) * cpp;
    samples[edge] |= uint64_t{1} << kPacked.hsync;
    Monitor monitor(kPacked);
    feed(monitor, samples, 0, twoRasters);
    CHECK_EQ(monitor.completedFrames(), 1);
    CHECK(!monitor.ok());
    CHECK(monitor.status().find("not a whole multiple") != std::string::npos);
    feed(monitor, samples, twoRasters, samples.size());
    CHECK(monitor.ok());
    checkImage(monitor);
  }
  // Dropping one complete Hsync pulse produces an apparent cpp change;
  // it must not silently interpolate the missing scanline.
  {
    auto samples = waveform(cpp, kPacked);
    const size_t edge = ((525 + 100) * 800 + 656) * cpp;
    for (size_t t = edge; t < edge + 96 * cpp; ++t)
      samples[t] |= uint64_t{1} << kPacked.hsync;
    Monitor monitor(kPacked);
    feed(monitor, samples, 0, twoRasters);
    CHECK(!monitor.ok());
    CHECK(monitor.status().find("cycles-per-pixel changed") != std::string::npos);
    feed(monitor, samples, twoRasters, samples.size());
    CHECK(monitor.ok());
    checkImage(monitor);
  }
  // A one-cycle-late Vsync edge keeps the same 525 Hsyncs but violates the
  // frame duration. Counting scanlines alone must not certify this frame.
  {
    auto samples = waveform(cpp, kPacked);
    const size_t edge = (525 + 490) * 800 * cpp;
    samples[edge] |= uint64_t{1} << kPacked.vsync;
    Monitor monitor(kPacked);
    feed(monitor, samples, 0, twoRasters);
    CHECK(!monitor.ok());
    CHECK(monitor.status().find("frame period") != std::string::npos);
  }
}

void checkPolarityAndRecovery(bool vertical) {
  constexpr unsigned cpp = 4;
  Monitor monitor(kPacked);
  const auto bad = waveform(cpp, kPacked, !vertical, vertical);
  feed(monitor, bad, 0, bad.size());
  CHECK(monitor.completedFrames() > 0);
  CHECK(!monitor.ok());  // Regression: inverted Vsync used to report healthy.
  const std::string signal = vertical ? "vsync" : "hsync";
  CHECK(monitor.status().find(signal) != std::string::npos);
  CHECK(monitor.status().find("active-HIGH") != std::string::npos);
  const auto clean = waveform(cpp, kPacked);
  feed(monitor, clean, 0, clean.size(), true, bad.size());
  CHECK(monitor.ok());
  checkImage(monitor);
}

}  // namespace

int main() {
  checkRatesLanesAndChunks();
  checkCompletionBoundary();
  checkCoincidentSyncOrigin();
  checkMalformedAndRecovery();
  checkPolarityAndRecovery(false);
  std::puts("test_vga_contract: valid timing, lanes, chunks, malformed sync, Hsync PASS");
  std::fflush(stdout);
  checkPolarityAndRecovery(true);
  std::puts("test_vga_contract: PASS");
}
