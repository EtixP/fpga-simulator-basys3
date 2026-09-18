foreach(design IN ITEMS counter stopwatch uart_echo vga_pattern)
  add_executable(benchmark_${design} benchmark.cpp)
  target_compile_definitions(benchmark_${design} PRIVATE
    VB_MODEL_HEADER="V${design}.h"
    VB_MODEL=V${design}
    VB_DESIGN="${design}"
    VB_XDC="${CMAKE_SOURCE_DIR}/examples/${design}.xdc")
  target_link_libraries(benchmark_${design} PRIVATE vb_board vb_engine sim_${design})
endforeach()
