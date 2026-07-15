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

# --- milestone 1.1-1.2 acceptance test ------------------------------------
# (sim_counter comes from examples/CMakeLists.txt)
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

# --- BoardModel: the milestone 1.3 acceptance criterion, headless ----------
add_executable(test_board_model test_board_model.cpp)
target_link_libraries(test_board_model PRIVATE vb_stimulus vb_board vb_engine sim_counter)
add_test(NAME board_model COMMAND test_board_model
  "${CMAKE_SOURCE_DIR}/examples/counter.xdc"
)

# --- stimulus/CLI parsing: pure functions, no SDL, no engine ---------------
add_executable(test_gui_script test_gui_script.cpp)
target_link_libraries(test_gui_script PRIVATE vb_stimulus)
add_test(NAME gui_script COMMAND test_gui_script)

# --- milestone 1.4 acceptance: stopwatch golden log (3 simulated seconds) --
add_executable(test_stopwatch_golden test_stopwatch_golden.cpp)
target_link_libraries(test_stopwatch_golden PRIVATE vb_board vb_engine sim_stopwatch)
add_test(NAME stopwatch_golden COMMAND test_stopwatch_golden
  "${CMAKE_SOURCE_DIR}/examples/stopwatch.xdc"
  "${CMAKE_SOURCE_DIR}/tests/golden/stopwatch_3s.log"
)
# ~15 s at the sim libs' -O2; 10x headroom for CI runners.
set_tests_properties(stopwatch_golden PROPERTIES TIMEOUT 180)

# --- milestone 1.5 acceptance: uart_echo round-trips "hello", byte-exact ----
add_executable(test_uart_echo test_uart_echo.cpp)
target_link_libraries(test_uart_echo PRIVATE vb_board vb_engine sim_uart_echo)
add_test(NAME uart_echo COMMAND test_uart_echo
  "${CMAKE_SOURCE_DIR}/examples/uart_echo.xdc"
  "${CMAKE_SOURCE_DIR}/tests/golden/uart_echo.log"
)
set_tests_properties(uart_echo PROPERTIES TIMEOUT 60)

# --- milestone 1.4 acceptance: VCD opens in real tools ----------------------
find_program(SURFER_EXECUTABLE surfer)
add_executable(test_stopwatch_vcd test_stopwatch_vcd.cpp)
target_link_libraries(test_stopwatch_vcd PRIVATE vb_engine sim_stopwatch)
add_test(NAME stopwatch_vcd_structural COMMAND test_stopwatch_vcd structural
  "${CMAKE_CURRENT_BINARY_DIR}/stopwatch_trace.vcd"
)
set_tests_properties(stopwatch_vcd_structural PROPERTIES TIMEOUT 120)
add_test(NAME stopwatch_vcd_surfer COMMAND test_stopwatch_vcd surfer
  "${CMAKE_CURRENT_BINARY_DIR}/stopwatch_trace.vcd"
  "${SURFER_EXECUTABLE}"
)
# Reuses the structural test's trace; absence of surfer reads SKIP, not PASS.
set_tests_properties(stopwatch_vcd_surfer PROPERTIES
  DEPENDS stopwatch_vcd_structural SKIP_RETURN_CODE 77 TIMEOUT 120)

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
