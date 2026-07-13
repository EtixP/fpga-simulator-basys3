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

# Board-level state the GUI renders from (switches, LEDs; later peripherals
# slot in as siblings). Engine-agnostic: SimEngine.h + PinBinding only.
add_library(vb_board STATIC board/BoardModel.cpp)
target_link_libraries(vb_board PUBLIC vb_constraints)

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

  add_executable(virtualbasys_gui gui/main.mm gui/BoardWindow.cpp)
  set_source_files_properties(gui/main.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_definitions(virtualbasys_gui PRIVATE
    VB_DEFAULT_XDC="${CMAKE_SOURCE_DIR}/examples/counter.xdc")
  target_link_libraries(virtualbasys_gui PRIVATE
    vb_board vb_engine vb_imgui sim_counter)
endif()
