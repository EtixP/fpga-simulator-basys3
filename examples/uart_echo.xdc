## Pin constraints for examples/uart_echo.v — the used subset of the canonical
## Basys 3 mapping (see src/constraints/data/Basys3_Master.xdc).

## Clock signal
set_property -dict { PACKAGE_PIN W5   IOSTANDARD LVCMOS33 } [get_ports clk]
create_clock -add -name sys_clk_pin -period 10.00 -waveform {0 5} [get_ports clk]

## Buttons
set_property -dict { PACKAGE_PIN U18   IOSTANDARD LVCMOS33 } [get_ports btnC]

## USB-RS232 interface
set_property -dict { PACKAGE_PIN B18   IOSTANDARD LVCMOS33 } [get_ports RsRx]
set_property -dict { PACKAGE_PIN A18   IOSTANDARD LVCMOS33 } [get_ports RsTx]

## LED (transmit-busy indicator)
set_property -dict { PACKAGE_PIN U16   IOSTANDARD LVCMOS33 } [get_ports led]
