# One verilated static library per example design; each executable links
# exactly one of these (see CLAUDE.md decision log).
#
# OPT_FAST/OPT_GLOBAL -O2: generated model code is never debugged at source
# level, and the default (empty) build type would otherwise run it at -O0 —
# measured 3.3 vs 20 Mcycles/s on the stopwatch, i.e. a ~90 s vs ~15 s golden
# test.
add_library(sim_counter STATIC)
verilate(sim_counter
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/counter.v
  TOP_MODULE counter
  OPT_FAST "-O2" OPT_GLOBAL "-O2"
  VERILATOR_ARGS --public-flat-rw --trace-vcd --timescale 1ns/1ns -Wall
)

add_library(sim_stopwatch STATIC)
verilate(sim_stopwatch
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/stopwatch.v
  TOP_MODULE stopwatch
  OPT_FAST "-O2" OPT_GLOBAL "-O2"
  VERILATOR_ARGS --public-flat-rw --trace-vcd --timescale 1ns/1ns -Wall
)

add_library(sim_uart_echo STATIC)
verilate(sim_uart_echo
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/uart_echo.v
  TOP_MODULE uart_echo
  OPT_FAST "-O2" OPT_GLOBAL "-O2"
  VERILATOR_ARGS --public-flat-rw --trace-vcd --timescale 1ns/1ns -Wall
)
