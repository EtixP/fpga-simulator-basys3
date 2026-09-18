find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(virtualbasys_qt main.cpp)
qt_add_qml_module(virtualbasys_qt
  URI VirtualBasys
  VERSION 1.0
  QML_FILES Main.qml
)
target_link_libraries(virtualbasys_qt PRIVATE
  Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2
)

# Exercise embedded QML/imports and an actual rendered frame without a display.
add_test(NAME qt_qml_smoke COMMAND virtualbasys_qt --smoke-test)
set_tests_properties(qt_qml_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  TIMEOUT 20
)
