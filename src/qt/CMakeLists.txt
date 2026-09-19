find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_qml_module(vb_qt_adapter STATIC
  URI VirtualBasys.Board
  VERSION 1.0
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/VirtualBasys/Board"
  SOURCES BoardAdapter.h BoardAdapter.cpp
          BoardIoModel.h BoardIoModel.cpp
          SevenSegmentModel.h SevenSegmentModel.cpp
)
target_include_directories(vb_qt_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vb_qt_adapter PUBLIC Qt6::Core Qt6::Qml PRIVATE vb_board)

qt_add_executable(virtualbasys_qt main.cpp)
qt_add_qml_module(virtualbasys_qt
  URI VirtualBasys
  VERSION 1.0
  IMPORT_PATH "${CMAKE_CURRENT_BINARY_DIR}"
  QML_FILES Main.qml
)
target_link_libraries(virtualbasys_qt PRIVATE
  Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2
  vb_qt_adapter vb_qt_adapterplugin
)

# QCoreApplication/QQmlEngine tests need no window, platform plugin or renderer.
qt_add_executable(test_qt_board_adapter "${CMAKE_SOURCE_DIR}/tests/test_qt_board_adapter.cpp")
target_include_directories(test_qt_board_adapter PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_qt_board_adapter PRIVATE
  vb_qt_adapter vb_qt_adapterplugin vb_board vb_engine Qt6::Test
)
add_test(NAME qt_board_adapter COMMAND test_qt_board_adapter)
set_tests_properties(qt_board_adapter PROPERTIES TIMEOUT 20)

qt_add_executable(test_qt_counter "${CMAKE_SOURCE_DIR}/tests/test_qt_counter.cpp")
target_include_directories(test_qt_counter PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(test_qt_counter PRIVATE vb_qt_adapter vb_board vb_engine sim_counter)
add_test(NAME qt_counter COMMAND test_qt_counter "${CMAKE_SOURCE_DIR}/examples/counter.xdc")
set_tests_properties(qt_counter PROPERTIES TIMEOUT 20)

# Exercise embedded QML/imports and an actual rendered frame without a display.
add_test(NAME qt_qml_smoke COMMAND virtualbasys_qt --smoke-test)
set_tests_properties(qt_qml_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  TIMEOUT 20
)
