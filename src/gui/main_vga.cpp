#include "Vvga_pattern.h"
#include "gui/DemoMain.h"

int main(int argc, char** argv) {
  // The VGA frame is ~1.68M cycles; one VGA frame per rendered GUI frame keeps
  // the display frame-accurate (R1: frame pacing follows sim speed, ~10 fps
  // with the honest speed banner — see docs/versions.md).
  return vb::runDemo<Vvga_pattern>(argc, argv, "vga_pattern",
                                   "VirtualBasys — Basys 3 (VGA pattern demo)",
                                   VB_DEFAULT_XDC, /*cyclesPerFrame=*/1'700'000);
}
