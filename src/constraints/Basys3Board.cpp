#include "constraints/Basys3Board.h"

#include <string>
#include <unordered_map>

namespace vb::basys3 {

namespace {

// Generated from Digilent's Basys-3-Master.xdc (105 pins). Regenerate with a
// mechanical transform if Digilent revs the file; do not hand-edit entries.
const std::unordered_map<std::string, const char*>& pinTable() {
  static const std::unordered_map<std::string, const char*> table = {
    {"W5", "CLK100"},
    {"V17", "SW0"},
    {"V16", "SW1"},
    {"W16", "SW2"},
    {"W17", "SW3"},
    {"W15", "SW4"},
    {"V15", "SW5"},
    {"W14", "SW6"},
    {"W13", "SW7"},
    {"V2", "SW8"},
    {"T3", "SW9"},
    {"T2", "SW10"},
    {"R3", "SW11"},
    {"W2", "SW12"},
    {"U1", "SW13"},
    {"T1", "SW14"},
    {"R2", "SW15"},
    {"U16", "LED0"},
    {"E19", "LED1"},
    {"U19", "LED2"},
    {"V19", "LED3"},
    {"W18", "LED4"},
    {"U15", "LED5"},
    {"U14", "LED6"},
    {"V14", "LED7"},
    {"V13", "LED8"},
    {"V3", "LED9"},
    {"W3", "LED10"},
    {"U3", "LED11"},
    {"P3", "LED12"},
    {"N3", "LED13"},
    {"P1", "LED14"},
    {"L1", "LED15"},
    {"W7", "SEG0"},
    {"W6", "SEG1"},
    {"U8", "SEG2"},
    {"V8", "SEG3"},
    {"U5", "SEG4"},
    {"V5", "SEG5"},
    {"U7", "SEG6"},
    {"V7", "DP"},
    {"U2", "AN0"},
    {"U4", "AN1"},
    {"V4", "AN2"},
    {"W4", "AN3"},
    {"U18", "BTNC"},
    {"T18", "BTNU"},
    {"W19", "BTNL"},
    {"T17", "BTNR"},
    {"U17", "BTND"},
    {"J1", "JA0"},
    {"L2", "JA1"},
    {"J2", "JA2"},
    {"G2", "JA3"},
    {"H1", "JA4"},
    {"K2", "JA5"},
    {"H2", "JA6"},
    {"G3", "JA7"},
    {"A14", "JB0"},
    {"A16", "JB1"},
    {"B15", "JB2"},
    {"B16", "JB3"},
    {"A15", "JB4"},
    {"A17", "JB5"},
    {"C15", "JB6"},
    {"C16", "JB7"},
    {"K17", "JC0"},
    {"M18", "JC1"},
    {"N17", "JC2"},
    {"P18", "JC3"},
    {"L17", "JC4"},
    {"M19", "JC5"},
    {"P17", "JC6"},
    {"R18", "JC7"},
    {"J3", "JXADC0"},
    {"L3", "JXADC1"},
    {"M2", "JXADC2"},
    {"N2", "JXADC3"},
    {"K3", "JXADC4"},
    {"M3", "JXADC5"},
    {"M1", "JXADC6"},
    {"N1", "JXADC7"},
    {"G19", "VGA_R0"},
    {"H19", "VGA_R1"},
    {"J19", "VGA_R2"},
    {"N19", "VGA_R3"},
    {"N18", "VGA_B0"},
    {"L18", "VGA_B1"},
    {"K18", "VGA_B2"},
    {"J18", "VGA_B3"},
    {"J17", "VGA_G0"},
    {"H17", "VGA_G1"},
    {"G17", "VGA_G2"},
    {"D17", "VGA_G3"},
    {"P19", "VGA_HS"},
    {"R19", "VGA_VS"},
    {"B18", "UART_RX"},
    {"A18", "UART_TX"},
    {"C17", "PS2_CLK"},
    {"B17", "PS2_DATA"},
    {"D18", "QSPI_DB0"},
    {"D19", "QSPI_DB1"},
    {"G18", "QSPI_DB2"},
    {"F18", "QSPI_DB3"},
    {"K19", "QSPI_CSN"},
  };
  return table;
}

}  // namespace

const char* resourceForPin(std::string_view packagePin) {
  const auto& table = pinTable();
  const auto it = table.find(std::string(packagePin));
  return it == table.end() ? nullptr : it->second;
}

bool resourceDrivesDesign(std::string_view resource) {
  if (resource.rfind("SW", 0) == 0 || resource.rfind("BTN", 0) == 0) return true;
  return resource == "UART_RX" || resource == "CLK100";
}

}  // namespace vb::basys3
