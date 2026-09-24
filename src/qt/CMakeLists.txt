find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_qml_module(vb_qt_adapter STATIC
  URI VirtualBasys.Board
  VERSION 1.0
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/VirtualBasys/Board"
  SOURCES BoardAdapter.h BoardAdapter.cpp
          BoardIoModel.h BoardIoModel.cpp
          SevenSegmentModel.h SevenSegmentModel.cpp
          UartConsoleModel.h UartConsoleModel.cpp VirtualTime.h
          SimulationController.h SimulationController.cpp
)
target_include_directories(vb_qt_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vb_qt_adapter PUBLIC Qt6::Core Qt6::Qml PRIVATE vb_board)

# One Verilator runtime per launcher. Share the launcher and QML sources while
# keeping generated QML output directories distinct between executable targets.
# STARTUP_RESET applies the Reset control's pulse once before the window opens.
function(vb_add_qt_example target design title)
  cmake_parse_arguments(PARSE_ARGV 3 VB_EXAMPLE "STARTUP_RESET" "" "")
  qt_add_executable(${target} main.cpp)
  qt_add_qml_module(${target}
    URI VirtualBasys
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${target}_qml/VirtualBasys"
    IMPORT_PATH "${CMAKE_CURRENT_BINARY_DIR}"
    QML_FILES Main.qml ShellPane.qml EmptyState.qml BoardView.qml
              BoardSwitch.qml BoardLed.qml BoardButton.qml SevenSegmentDigit.qml
              SimulationToolbar.qml UartTerminal.qml
  )
  qt_add_resources(${target} "${target}_constraints"
    PREFIX "/examples"
    BASE "${CMAKE_SOURCE_DIR}/examples"
    FILES "${CMAKE_SOURCE_DIR}/examples/${design}.xdc"
  )
  target_compile_definitions(${target} PRIVATE
    VB_MODEL_HEADER="V${design}.h" VB_MODEL=V${design}
    VB_DESIGN="${design}" VB_DESIGN_TITLE="${title}"
    VB_STARTUP_RESET=$<BOOL:${VB_EXAMPLE_STARTUP_RESET}>)
  target_link_libraries(${target} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2
    vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine sim_${design}
  )
endfunction()

vb_add_qt_example(virtualbasys_qt counter Counter)
vb_add_qt_example(virtualbasys_qt_stopwatch stopwatch Stopwatch)
vb_add_qt_example(virtualbasys_qt_uart uart_echo "UART Echo" STARTUP_RESET)

# QCoreApplication/QQmlEngine tests need no window, platform plugin or renderer.
qt_add_executable(test_qt_board_adapter "${CMAKE_SOURCE_DIR}/tests/test_qt_board_adapter.cpp")
target_include_directories(test_qt_board_adapter PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_qt_board_adapter PRIVATE
  vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine Qt6::Test
)
add_test(NAME qt_board_adapter COMMAND test_qt_board_adapter)
set_tests_properties(qt_board_adapter PROPERTIES TIMEOUT 20)

qt_add_executable(test_qt_uart_console "${CMAKE_SOURCE_DIR}/tests/test_qt_uart_console.cpp")
target_include_directories(test_qt_uart_console PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_qt_uart_console PRIVATE
  vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine Qt6::Test
)
add_test(NAME qt_uart_console COMMAND test_qt_uart_console)
set_tests_properties(qt_uart_console PROPERTIES TIMEOUT 60)

qt_add_executable(test_qt_counter "${CMAKE_SOURCE_DIR}/tests/test_qt_counter.cpp")
target_include_directories(test_qt_counter PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_qt_counter PRIVATE vb_qt_adapter vb_board vb_engine sim_counter)
add_test(NAME qt_counter COMMAND test_qt_counter "${CMAKE_SOURCE_DIR}/examples/counter.xdc")
set_tests_properties(qt_counter PROPERTIES TIMEOUT 20)

# Exercise source QML through real mouse/keyboard events. Embedded module loading
# is checked separately by qt_qml_smoke, so both development and packaging paths run.
qt_add_executable(test_qt_shell "${CMAKE_SOURCE_DIR}/tests/test_qt_shell.cpp")
target_compile_definitions(test_qt_shell PRIVATE VB_QT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_qt_shell PRIVATE
  vb_qt_adapter vb_qt_adapterplugin Qt6::Test Qt6::Quick Qt6::Qml Qt6::QuickControls2
)
add_test(NAME qt_shell COMMAND test_qt_shell)
set_tests_properties(qt_shell PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
  TIMEOUT 60
)

qt_add_executable(test_qt_board_widgets "${CMAKE_SOURCE_DIR}/tests/test_qt_board_widgets.cpp")
target_compile_definitions(test_qt_board_widgets PRIVATE
  VB_QT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}" VB_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_qt_board_widgets PRIVATE
  vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine
  Qt6::Test Qt6::Quick Qt6::Qml Qt6::QuickControls2
)
add_test(NAME qt_board_widgets COMMAND test_qt_board_widgets)
set_tests_properties(qt_board_widgets PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
  TIMEOUT 90
)

# Keep one Verilated runtime per executable while exercising the same QML board.
foreach(design counter stopwatch)
  qt_add_executable(test_qt_board_${design}_ui
    "${CMAKE_SOURCE_DIR}/tests/test_qt_board_${design}_ui.cpp")
  target_compile_definitions(test_qt_board_${design}_ui PRIVATE
    VB_QT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}" VB_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  target_link_libraries(test_qt_board_${design}_ui PRIVATE
    vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine sim_${design}
    Qt6::Test Qt6::Quick Qt6::Qml Qt6::QuickControls2
  )
  add_test(NAME qt_board_${design}_ui COMMAND test_qt_board_${design}_ui)
  set_tests_properties(qt_board_${design}_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
    TIMEOUT 180
  )
endforeach()

# Exercise embedded QML/imports and an actual rendered frame without a display.
add_test(NAME qt_qml_smoke COMMAND virtualbasys_qt --smoke-test)
set_tests_properties(qt_qml_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  TIMEOUT 20
)

foreach(variant IN ITEMS preview stopwatch uart)
  if(variant STREQUAL "preview")
    add_test(NAME qt_qml_${variant}_smoke COMMAND virtualbasys_qt --preview --smoke-test)
  else()
    add_test(NAME qt_qml_${variant}_smoke COMMAND virtualbasys_qt_${variant} --smoke-test)
  endif()
  set_tests_properties(qt_qml_${variant}_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 20)
endforeach()

qt_add_executable(test_qt_simulation_controller
  "${CMAKE_SOURCE_DIR}/tests/test_qt_simulation_controller.cpp")
target_compile_definitions(test_qt_simulation_controller PRIVATE VB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_qt_simulation_controller PRIVATE
  vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine sim_counter Qt6::Test)
add_test(NAME qt_simulation_controller COMMAND test_qt_simulation_controller)
set_tests_properties(qt_simulation_controller PROPERTIES TIMEOUT 60)

qt_add_executable(test_qt_simulation_stopwatch
  "${CMAKE_SOURCE_DIR}/tests/test_qt_simulation_stopwatch.cpp")
target_include_directories(test_qt_simulation_stopwatch PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(test_qt_simulation_stopwatch PRIVATE VB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_qt_simulation_stopwatch PRIVATE
  vb_qt_adapter vb_board vb_engine sim_stopwatch)
add_test(NAME qt_simulation_stopwatch COMMAND test_qt_simulation_stopwatch)
set_tests_properties(qt_simulation_stopwatch PROPERTIES TIMEOUT 180)

qt_add_executable(test_qt_simulation_ui "${CMAKE_SOURCE_DIR}/tests/test_qt_simulation_ui.cpp")
target_compile_definitions(test_qt_simulation_ui PRIVATE
  VB_QT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}" VB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_qt_simulation_ui PRIVATE
  vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine sim_counter
  Qt6::Test Qt6::Quick Qt6::Qml Qt6::QuickControls2)
add_test(NAME qt_simulation_ui COMMAND test_qt_simulation_ui)
set_tests_properties(qt_simulation_ui PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
  TIMEOUT 90)

# The UART terminal against the real uart_echo RTL, through actual input events.
qt_add_executable(test_qt_uart_ui "${CMAKE_SOURCE_DIR}/tests/test_qt_uart_ui.cpp")
target_include_directories(test_qt_uart_ui PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(test_qt_uart_ui PRIVATE
  VB_QT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}" VB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_qt_uart_ui PRIVATE
  vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine sim_uart_echo
  Qt6::Test Qt6::Quick Qt6::Qml Qt6::QuickControls2)
add_test(NAME qt_uart_ui COMMAND test_qt_uart_ui)
set_tests_properties(qt_uart_ui PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
  TIMEOUT 120)

# Keep imported Qt dependencies in this directory's scope, including benchmarks.
if(VB_BUILD_BENCHMARKS)
  foreach(design IN ITEMS counter stopwatch uart_echo)
    qt_add_executable(benchmark_qt_control_${design} "${CMAKE_SOURCE_DIR}/bench/qt_controller.cpp")
    set_target_properties(benchmark_qt_control_${design} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench")
    target_compile_definitions(benchmark_qt_control_${design} PRIVATE
      VB_MODEL_HEADER="V${design}.h" VB_MODEL=V${design} VB_DESIGN="${design}"
      VB_XDC="${CMAKE_SOURCE_DIR}/examples/${design}.xdc"
      VB_QT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(benchmark_qt_control_${design} PRIVATE
      vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine sim_${design}
      Qt6::Test Qt6::Quick Qt6::Qml Qt6::QuickControls2)
  endforeach()
endif()
