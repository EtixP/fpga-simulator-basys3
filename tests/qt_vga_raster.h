// A synthetic VGA design with exact 800x525 raster timing at one master
// cycle per pixel and a frame-dependent pattern. Expected images and stamps
// come from raster arithmetic here, never from the monitor or the adapter.
#pragma once

#include "board/BoardModel.h"
#include "check.h"

#include <QImage>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace vga_raster {
constexpr uint64_t kLine = 800;
constexpr uint64_t kFrame = 800 * 525;
constexpr uint64_t kFirstVsync = 490 * kLine;  // first Vsync fall, frame 0

// 12-bit RGB of visible pixel (x, y) in raster frame f. Every color bit
// varies, and frames differ everywhere.
inline uint16_t pattern(uint64_t f, uint32_t x, uint32_t y) {
  const uint32_t r = (x + 3 * f) & 15;
  const uint32_t g = (y + 5 * f) & 15;
  const uint32_t b = (x / 16 + y / 16 + f) & 15;
  return static_cast<uint16_t>(r << 8 | g << 4 | b);
}

// The monitor's frame k spans Vsync falls k+1 and k+2, so its visible lines
// are raster frame k+1 (35 lines after the fall at line 490).
inline std::vector<uint8_t> expectedFrame(uint64_t monitorFrame) {
  std::vector<uint8_t> rgb(640 * 480 * 3);
  for (uint32_t y = 0; y < 480; ++y)
    for (uint32_t x = 0; x < 640; ++x) {
      const uint16_t c = pattern(monitorFrame + 1, x, y);
      const size_t i = (size_t(y) * 640 + x) * 3;
      rgb[i] = uint8_t(((c >> 8) & 15) * 17);
      rgb[i + 1] = uint8_t(((c >> 4) & 15) * 17);
      rgb[i + 2] = uint8_t((c & 15) * 17);
    }
  return rgb;
}

// Stamp of monitor frame k: its closing Vsync fall. Captures label the
// post-edge state of cycle N+1 with stamp N, hence the -1.
inline uint64_t frameStamp(uint64_t k) { return kFirstVsync + (k + 1) * kFrame - 1; }

class RasterEngine final : public vb::SimEngine {
public:
  enum Port : uint32_t { Hs, Vs, Red, Green, Blue, Button };
  bool invertHsync = false;
  unsigned steps = 0;

  vb::SignalId lookup(std::string_view name) override {
    for (size_t i = 0; i < metadata_.size(); ++i)
      if (metadata_[i].name == name) return vb::SignalId(i);
    return vb::kNoSignal;
  }
  vb::SignalInfo info(vb::SignalId id) const override { return metadata_.at(size_t(id)); }
  std::vector<vb::SignalInfo> ports() const override { return metadata_; }
  void step(uint64_t cycles) override { ++steps; now_ += cycles; }
  uint64_t now() const override { return now_; }
  uint64_t peek(vb::SignalId id) override {
    const uint32_t x = uint32_t(now_ % kLine);
    const uint32_t y = uint32_t((now_ / kLine) % 525);
    const uint16_t color = x < 640 && y < 480 ? pattern(now_ / kFrame, x, y) : 0;
    switch (size_t(id)) {
      case Hs: return (x >= 656 && x < 752) == invertHsync;
      case Vs: return !(y >= 490 && y < 492);
      case Red: return (color >> 8) & 15;
      case Green: return (color >> 4) & 15;
      case Blue: return color & 15;
      case Button: return button_;
      default: throw std::invalid_argument("invalid signal");
    }
  }
  void poke(vb::SignalId id, uint64_t value) override {
    if (size_t(id) != Button) throw std::invalid_argument("not an input");
    button_ = value & 1;
  }
  void setTraceFile(std::string_view) override {}
  void trace(bool) override {}

private:
  uint64_t now_ = 0;
  uint64_t button_ = 0;
  const std::vector<vb::SignalInfo> metadata_{
      {"hs", 1, false}, {"vs", 1, false}, {"red", 4, false},
      {"green", 4, false}, {"blue", 4, false}, {"reset", 1, true}};
};

inline std::string xdc(bool allPins) {
  std::string text =
      "set_property PACKAGE_PIN U18 [get_ports reset]\n"
      "set_property PACKAGE_PIN P19 [get_ports hs]\n"
      "set_property PACKAGE_PIN R19 [get_ports vs]\n";
  const char* red[] = {"G19", "H19", "J19", "N19"};
  const char* green[] = {"J17", "H17", "G17", "D17"};
  const char* blue[] = {"N18", "L18", "K18", "J18"};
  for (int bit = 0; bit < 4; ++bit) {
    text += std::string("set_property PACKAGE_PIN ") + red[bit] + " [get_ports {red[" +
            std::to_string(bit) + "]}]\n";
    text += std::string("set_property PACKAGE_PIN ") + green[bit] + " [get_ports {green[" +
            std::to_string(bit) + "]}]\n";
    // Without every VGA pin the board exposes no monitor at all.
    if (allPins || bit != 3)
      text += std::string("set_property PACKAGE_PIN ") + blue[bit] + " [get_ports {blue[" +
              std::to_string(bit) + "]}]\n";
  }
  return text;
}


inline bool imageEquals(const QImage& image, const std::vector<uint8_t>& rgb) {
  if (image.format() != QImage::Format_RGB888 || image.width() != 640 || image.height() != 480)
    return false;
  for (int y = 0; y < 480; ++y)
    if (std::memcmp(image.constScanLine(y), rgb.data() + size_t(y) * 640 * 3, 640 * 3))
      return false;
  return true;
}

}  // namespace vga_raster
