# One verilated static library per example design; each executable links
# exactly one of these (see CLAUDE.md decision log).
add_library(sim_counter STATIC)
verilate(sim_counter
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/counter.v
  TOP_MODULE counter
  VERILATOR_ARGS --public-flat-rw --trace-vcd --timescale 1ns/1ns -Wall
)
