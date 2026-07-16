#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace vb {

// Write a 24-bit BMP from a top-origin RGB888 buffer (row 0 = TOP, byte order
// R,G,B). BMP is natively bottom-up BGR, so this function flips rows and
// swaps channels INTERNALLY — callers always pass RGB888 top-origin. Keeping
// that contract in one place is what stops the "R<->B swap / vertical flip
// hidden in a golden-vs-golden compare" trap: the GUI texture path and this
// writer both consume the same RGB888/top-origin framebuffer.
inline bool writeBmpRGB888(const std::string& path, const std::vector<uint8_t>& rgb,
                           uint32_t w, uint32_t h) {
  if (rgb.size() != static_cast<size_t>(w) * h * 3) return false;
  const uint32_t rowBytes = ((w * 3 + 3) / 4) * 4;
  const uint32_t imageSize = rowBytes * h;
  uint8_t header[54] = {'B', 'M'};
  auto put32 = [&](int off, uint32_t v) {
    header[off] = v & 0xFF;
    header[off + 1] = (v >> 8) & 0xFF;
    header[off + 2] = (v >> 16) & 0xFF;
    header[off + 3] = (v >> 24) & 0xFF;
  };
  put32(2, 54 + imageSize);
  put32(10, 54);
  put32(14, 40);
  put32(18, w);
  put32(22, h);
  header[26] = 1;
  header[28] = 24;
  put32(34, imageSize);
  std::ofstream out(path, std::ios::binary);
  if (!out.good()) return false;
  out.write(reinterpret_cast<char*>(header), 54);
  std::string row(rowBytes, '\0');
  for (int32_t y = static_cast<int32_t>(h) - 1; y >= 0; --y) {  // bottom-up
    const uint8_t* src = rgb.data() + static_cast<size_t>(y) * w * 3;
    for (uint32_t x = 0; x < w; ++x) {
      row[x * 3 + 0] = static_cast<char>(src[x * 3 + 2]);  // B
      row[x * 3 + 1] = static_cast<char>(src[x * 3 + 1]);  // G
      row[x * 3 + 2] = static_cast<char>(src[x * 3 + 0]);  // R
    }
    out.write(row.data(), rowBytes);
  }
  return out.good();
}

}  // namespace vb
