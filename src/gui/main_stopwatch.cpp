#include "Vstopwatch.h"
#include "gui/DemoMain.h"

int main(int argc, char** argv) {
  return vb::runDemo<Vstopwatch>(argc, argv, "stopwatch",
                                 "VirtualBasys — Basys 3 (stopwatch demo)",
                                 VB_DEFAULT_XDC);
}
