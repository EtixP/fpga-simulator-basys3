#include "Vcounter.h"
#include "gui/DemoMain.h"

int main(int argc, char** argv) {
  return vb::runDemo<Vcounter>(argc, argv, "counter",
                               "VirtualBasys — Basys 3 (counter demo)",
                               VB_DEFAULT_XDC);
}
