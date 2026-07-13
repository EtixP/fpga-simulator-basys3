# Engine: compiled ONCE against the model-independent Verilator runtime
# headers. Runtime symbol definitions come from whichever verilate()d design
# library the final executable links (verilate() compiles the runtime into
# its target), so this library must always be linked BEFORE a sim_* library.
add_library(vb_engine STATIC
  engine/VerilatorEngine.cpp
)
target_include_directories(vb_engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${VERILATOR_ROOT}/include
  ${VERILATOR_ROOT}/include/vltstd
)

# Constraints: XDC parsing + board table + pin binding. Engine-independent
# (uses only the SimEngine.h interface header).
add_library(vb_constraints STATIC
  constraints/Xdc.cpp
  constraints/Basys3Board.cpp
  constraints/PinBinding.cpp
)
target_include_directories(vb_constraints PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
