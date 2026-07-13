#pragma once
#include <string_view>

namespace vb {

// Static facts about the Basys 3 board itself — the fixed side of R2's
// binding. The XDC supplies port -> package pin; this table supplies
// package pin -> board resource ("W17" -> "SW3"). Resources are stable
// uppercase ids: SW0..SW15, LED0..LED15, BTNC/U/L/R/D, SEG0..SEG6, DP,
// AN0..AN3, CLK100, VGA_R0..3/G0..3/B0..3, VGA_HS, VGA_VS, UART_RX, UART_TX,
// PS2_CLK, PS2_DATA, QSPI_DB0..3, QSPI_CSN, JA0..JA7, JB0..7, JC0..7,
// JXADC0..7.
namespace basys3 {

// nullptr if the pin is not a user-accessible Basys 3 resource.
const char* resourceForPin(std::string_view packagePin);

// True for resources that drive the design (user pokes them): switches,
// buttons, UART_RX, and the clock. Used to validate that the XDC binds them
// to input ports.
bool resourceDrivesDesign(std::string_view resource);

}  // namespace basys3
}  // namespace vb
