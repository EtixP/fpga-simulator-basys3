# Engine: compiled ONCE against the model-independent Verilator runtime
# headers. Runtime symbol definitions come from whichever verilate()d design
# library the final executable links (verilate() compiles the runtime into
# its target), so this library must always be linked BEFORE a sim_* library.
add_library(vb_engine STATIC
  engine/SimEngine.cpp
  engine/VerilatorEngine.cpp
)
target_include_directories(vb_engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${VERILATOR_ROOT}/include
  ${VERILATOR_ROOT}/include/vltstd
)
# The stepCapture tap runs once per simulated cycle (1.68M/frame for VGA), so
# it is on the same hot path as the generated model and gets the same -O2 as
# the sim_* libraries — regardless of the project build type, which is often
# left empty (-O0). Without this the VGA demo runs ~2x slower than the
# measured baseline. Correctness is build-type independent; only speed.
target_compile_options(vb_engine PRIVATE -O2)

# Constraints: XDC parsing + board table + pin binding. Engine-independent
# (uses only the SimEngine.h interface header).
add_library(vb_constraints STATIC
  constraints/Xdc.cpp
  constraints/Basys3Board.cpp
  constraints/PinBinding.cpp
)
target_include_directories(vb_constraints PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Board-level state the GUI renders from. Peripherals are composed classes
# (SevenSeg, SimLog; UART/VGA join as siblings). Engine-agnostic: SimEngine.h
# + PinBinding only.
add_library(vb_board STATIC
  board/BoardModel.cpp
  board/SevenSeg.cpp
  board/SimLog.cpp
  board/Uart.cpp
  board/Vga.cpp
)
target_link_libraries(vb_board PUBLIC vb_constraints)
# The VGA frame assembler consumes every captured cycle; -O2 for the same
# hot-path reason as vb_engine (above).
target_compile_options(vb_board PRIVATE -O2)

# Scripted stimulus + demo CLI parsing: pure C++ (no SDL/ImGui), built
# unconditionally so headless tests cover it even with VB_BUILD_GUI=OFF.
add_library(vb_stimulus STATIC gui/Stimulus.cpp gui/DemoCli.cpp)
target_link_libraries(vb_stimulus PUBLIC vb_board)

# --- GUI: Dear ImGui (submodule @ v1.92.8) + SDL2 + Metal -------------------
if(VB_BUILD_GUI AND APPLE)
  enable_language(OBJCXX)
  find_package(SDL2 REQUIRED CONFIG)

  set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
  add_library(vb_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_metal.mm
  )
  target_include_directories(vb_imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_link_libraries(vb_imgui PUBLIC
    SDL2::SDL2
    "-framework Metal" "-framework QuartzCore" "-framework Foundation"
  )
  # The Metal backend requires ARC: upstream builds it with -fobjc-arc, and
  # its __bridge_retained/__bridge_transfer casts are no-ops under MRC
  # (objects would under-retain and dangle).
  set_source_files_properties(${IMGUI_DIR}/backends/imgui_impl_metal.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

  # Window plumbing + draw code, engine-free (sees BoardModel only).
  add_library(vb_gui STATIC gui/GuiApp.mm gui/BoardWindow.cpp)
  set_source_files_properties(gui/GuiApp.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(vb_gui PUBLIC vb_stimulus vb_board vb_imgui)

  # One demo executable per verilated design (decision log); interim until
  # the phase-2 `virtualbasys run` CLI subsumes them.
  add_executable(virtualbasys_counter gui/main_counter.cpp)
  target_compile_definitions(virtualbasys_counter PRIVATE
    VB_DEFAULT_XDC="${CMAKE_SOURCE_DIR}/examples/counter.xdc")
  target_link_libraries(virtualbasys_counter PRIVATE vb_gui vb_engine sim_counter)

  add_executable(virtualbasys_stopwatch gui/main_stopwatch.cpp)
  target_compile_definitions(virtualbasys_stopwatch PRIVATE
    VB_DEFAULT_XDC="${CMAKE_SOURCE_DIR}/examples/stopwatch.xdc")
  target_link_libraries(virtualbasys_stopwatch PRIVATE vb_gui vb_engine sim_stopwatch)

  add_executable(virtualbasys_uart_echo gui/main_uart_echo.cpp)
  target_compile_definitions(virtualbasys_uart_echo PRIVATE
    VB_DEFAULT_XDC="${CMAKE_SOURCE_DIR}/examples/uart_echo.xdc")
  target_link_libraries(virtualbasys_uart_echo PRIVATE vb_gui vb_engine sim_uart_echo)

  add_executable(virtualbasys_vga gui/main_vga.cpp)
  target_compile_definitions(virtualbasys_vga PRIVATE
    VB_DEFAULT_XDC="${CMAKE_SOURCE_DIR}/examples/vga_pattern.xdc")
  target_link_libraries(virtualbasys_vga PRIVATE vb_gui vb_engine sim_vga_pattern)
endif()
