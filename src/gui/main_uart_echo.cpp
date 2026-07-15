#include "Vuart_echo.h"
#include "gui/DemoMain.h"

int main(int argc, char** argv) {
  return vb::runDemo<Vuart_echo>(argc, argv, "uart_echo",
                                 "VirtualBasys — Basys 3 (UART echo demo)",
                                 VB_DEFAULT_XDC);
}
