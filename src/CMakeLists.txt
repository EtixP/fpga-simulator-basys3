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

# Board-level state the frontend renders from. Peripherals are composed classes
# (SevenSeg, SimLog, UART, VGA). Engine-agnostic: SimEngine.h + PinBinding only.
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

# Scripted runs: stimulus, launcher CLI parsing and the scheduler. Pure C++
# over BoardModel, built unconditionally so headless tests cover it without
# the Qt frontend.
add_library(vb_script STATIC script/Stimulus.cpp script/RunArgs.cpp script/ScriptRunner.cpp)
target_link_libraries(vb_script PUBLIC vb_board)

# Design loading: builds a user design into a loadable module at run time
# (src/design/module) and loads it through the SimEngine interface. Qt-free.
# It never links vb_engine: each module carries the one Verilator runtime of
# its process. Needs Verilator, CMake and the compiler at run time.
add_library(vb_design STATIC
  design/DesignBuilder.cpp
  design/DesignModule.cpp
  design/Process.cpp
  design/VerilatorJson.cpp
)
target_include_directories(vb_design PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set(VB_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
include("${CMAKE_SOURCE_DIR}/src/design/ModuleAbi.cmake")
target_compile_definitions(vb_design
  PUBLIC VB_ENGINE_INTERFACE_HASH="${VB_ENGINE_INTERFACE_HASH}"
  PRIVATE VB_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
          VB_TOOL_VERILATOR="${VERILATOR_ROOT}/bin/verilator"
          VB_TOOL_VERILATOR_DIR="${verilator_DIR}"
          VB_TOOL_CMAKE="${CMAKE_COMMAND}"
          VB_TOOL_CXX="${CMAKE_CXX_COMPILER}")
target_link_libraries(vb_design PUBLIC ${CMAKE_DL_LIBS})

# Keep Qt discovery and autogen settings local to this optional frontend.
if(VB_BUILD_QT_GUI)
  add_subdirectory(qt)
endif()
