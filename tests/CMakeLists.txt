# Conventions (see CLAUDE.md decision log):
#  - exactly ONE verilate()d library per test executable — verilate() compiles
#    the Verilator runtime into its target, so linking two duplicates it
#  - test data locations are passed as absolute paths via add_test arguments,
#    so binaries work under ctest and when run by hand
#  - assertions come from tests/check.h (never <cassert> — NDEBUG)

# --- XDC parser: no Verilator involved -----------------------------------
add_executable(test_xdc test_xdc.cpp)
target_link_libraries(test_xdc PRIVATE vb_constraints)
add_test(NAME xdc COMMAND test_xdc
  "${CMAKE_SOURCE_DIR}/src/constraints/data/Basys3_Master.xdc"
  "${CMAKE_SOURCE_DIR}/tests/data/Basys-3-Master-pristine.xdc"
)

# --- verilated design library: one per example design ---------------------
add_library(sim_counter STATIC)
verilate(sim_counter
  SOURCES ${CMAKE_SOURCE_DIR}/examples/counter.v
  TOP_MODULE counter
  VERILATOR_ARGS --public-flat-rw --trace-vcd --timescale 1ns/1ns -Wall
)

# --- milestone 1.1-1.2 acceptance test ------------------------------------
add_executable(test_counter_headless test_counter_headless.cpp)
target_link_libraries(test_counter_headless PRIVATE
  vb_constraints vb_engine sim_counter
)
add_test(NAME counter_headless COMMAND test_counter_headless
  "${CMAKE_SOURCE_DIR}/examples/counter.xdc"
  "${CMAKE_SOURCE_DIR}/tests/data/counter_swapped.xdc"
  "${CMAKE_SOURCE_DIR}/src/constraints/data/Basys3_Master.xdc"
)

# --- SimEngine contract on VerilatorEngine ---------------------------------
add_executable(test_engine_contract test_engine_contract.cpp)
target_link_libraries(test_engine_contract PRIVATE vb_engine sim_counter)
add_test(NAME engine_contract COMMAND test_engine_contract
  "${CMAKE_CURRENT_BINARY_DIR}/engine_contract_trace.vcd"
)

# --- inout ports are contractually unsupported (own design + executable) ---
# Fixture, not a shipped example, so no -Wall (R5 defers tri-state).
add_library(sim_ioport STATIC)
verilate(sim_ioport
  SOURCES ${CMAKE_SOURCE_DIR}/tests/data/ioport.v
  TOP_MODULE ioport
  VERILATOR_ARGS --public-flat-rw --trace-vcd --timescale 1ns/1ns
)
add_executable(test_inout_contract test_inout_contract.cpp)
target_link_libraries(test_inout_contract PRIVATE vb_engine sim_ioport)
add_test(NAME inout_contract COMMAND test_inout_contract)
