foreach(design IN ITEMS counter stopwatch uart_echo vga_pattern)
  add_executable(benchmark_${design} benchmark.cpp)
  target_compile_definitions(benchmark_${design} PRIVATE
    VB_MODEL_HEADER="V${design}.h"
    VB_MODEL=V${design}
    VB_DESIGN="${design}"
    VB_XDC="${CMAKE_SOURCE_DIR}/examples/${design}.xdc")
  target_link_libraries(benchmark_${design} PRIVATE vb_board vb_engine sim_${design})
endforeach()

if(TARGET vb_qt_adapter)
  add_executable(benchmark_qt_adapter qt_adapter.cpp)
  target_compile_definitions(benchmark_qt_adapter PRIVATE
    VB_XDC="${CMAKE_SOURCE_DIR}/examples/counter.xdc")
  target_link_libraries(benchmark_qt_adapter PRIVATE vb_qt_adapter vb_board vb_engine sim_counter)
endif()
