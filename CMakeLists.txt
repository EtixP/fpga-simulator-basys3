cmake_minimum_required(VERSION 3.24)
project(VirtualBasys CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Homebrew Verilator ships verilator-config.cmake; provides verilate() and
# VERILATOR_ROOT. Versions pinned in docs/versions.md.
find_package(verilator REQUIRED)

option(VB_BUILD_QT_GUI "Build the Qt Quick frontend" OFF)
option(VB_BUILD_BENCHMARKS "Build headless and optional Qt performance benchmarks" OFF)

enable_testing()
add_subdirectory(examples)  # verilated design libraries (sim_*)
add_subdirectory(src)       # engine, constraints, board, script, Qt frontend
add_subdirectory(tests)

if(NOT VB_BUILD_QT_GUI)
  message(STATUS "VirtualBasys: headless build; configure with -DVB_BUILD_QT_GUI=ON for the Qt launchers")
endif()

if(VB_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()
